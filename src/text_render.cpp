#include "text_render.hpp"
#include <array>
#include <cassert>
#include <cstring>

//
// --- sampler ---
VkResult build_text_sampler(VkSampler* out,VkFilter filter,VkDevice device){
    VkSamplerCreateInfo sci{};
    sci.sType = VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO;
    sci.magFilter = filter; sci.minFilter = filter;
    sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST;
    sci.addressModeU = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeV = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
    sci.mipLodBias = 0.0f;
    sci.anisotropyEnable = VK_FALSE;
    sci.maxAnisotropy = 1.0f;
    sci.compareEnable = VK_FALSE;
    sci.compareOp = VK_COMPARE_OP_ALWAYS;
    sci.minLod = 0.0f; sci.maxLod = 0.0f;
    sci.borderColor = VK_BORDER_COLOR_INT_OPAQUE_BLACK;
    sci.unnormalizedCoordinates = VK_FALSE;
    return vkCreateSampler(device, &sci, nullptr, out);
}

int measure_text_x_px(const FontAtlasCPU& cpu, std::string_view s){
	int w=0;
    for (unsigned char ch : s){
        auto it = cpu.glyphs.find((uint32_t)ch);
        if (it == cpu.glyphs.end()) return -1;
        w += it->second.advance;
    }
    return w;
}


inline std::array<RightTriangle,2>
make_triangles_from_rect(float x0,float y0,float x1,float y1) {
    float dx = x1 - x0, dy = y1 - y0;
    return {{
        RightTriangle{ x0,     y0,     +dx, +dy },  // base at min corner
        RightTriangle{ x1,     y1,     -dx, -dy },  // base at max corner
    }};
}

// inline std::array<RightTriangle,2>
// make_triangles_from_rect_skewed(float x0,float y0,float x1,float y1) {
//     float dx = x1 - x0, dy = y1 - y0;
//     return {{
//         RightTriangle{ x0,     y1,     +dx, -dy },
//         RightTriangle{ x1,     y0,     -dx, +dy },
//     }};
// }

// Build per-triangle instances for a whole line (two TriPair per glyph)
void text_line_tripairs(std::vector<TriPair>& out,
                        std::string_view s,
                        float x, float y,        // pen origin
                        float sx, float sy,      // text scale
                        const FontAtlasCPU& cpu)
{
    out.reserve(out.size() + 2*s.size());
    float pen = x;

    for (unsigned char ch : s) {
        auto it = cpu.glyphs.find((uint32_t)ch);
        assert(it != cpu.glyphs.end());
        const GlyphInfo& gi = it->second;

        // advance in *pixels* 
        float adv_px = float(gi.advance);

        // screen quad (baseline at y; flip/adjust for your coord system)
        float x0 = pen + gi.bearingX * sx;
        float y0 = y  + gi.bearingY * sy;
        float dx = gi.width  * sx;
        float dy = -gi.height * sy;

        auto scr2 = make_triangles_from_rect(x0, y0, x0+dx, y0+dy);
        auto uv2  = make_triangles_from_rect(gi.u0, gi.v0, gi.u1, gi.v1);

        out.push_back(TriPair{ scr2[0], uv2[0] });
        out.push_back(TriPair{ scr2[1], uv2[1] });

        pen += adv_px * sx;
    }
}



VkResult TextRenderer::build_pipeline_(VkDevice device,
                                         VkRenderPass rp,
                                         VkShaderModule vs, VkShaderModule fs,
                                         const VkViewport& vp, const VkRect2D& sc)
{
   	// set=0: EMPTY layout (0 bindings)
    VkDescriptorSetLayoutCreateInfo l0{ VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO };
    l0.bindingCount = 0;
    VK_CHECK(vkCreateDescriptorSetLayout(device, &l0, nullptr, &m_set0_empty));

    // set=1: combined image sampler for FS
    VkDescriptorSetLayoutBinding b1 =
        render::desc_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    auto l1 = render::desc_layout_info({ &b1, 1 });
    VK_CHECK(vkCreateDescriptorSetLayout(device, &l1, nullptr, &m_set1_atlas));

    // pipeline layout: [ set0_empty, set1_atlas ] + FS push-constant vec4
    VkPushConstantRange pc{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float)*4 };
    std::array<VkDescriptorSetLayout,2> sets{ m_set0_empty, m_set1_atlas };
    auto pl = render::layout_info(sets, { &pc, 1 });
    VK_CHECK(vkCreatePipelineLayout(device, &pl, nullptr, &m_layout));

    // vertex input: one per-instance binding (binding 0)
    VkVertexInputBindingDescription bind{
        .binding   = 0,
        .stride    = sizeof(TriInstance),
        .inputRate = VK_VERTEX_INPUT_RATE_INSTANCE
    };
    VkVertexInputAttributeDescription attrs[4] = {
        { .location=0, .binding=0, .format=VK_FORMAT_R32G32_SFLOAT, .offset=0  },  // in_screen_base
        { .location=1, .binding=0, .format=VK_FORMAT_R32G32_SFLOAT, .offset=8  },  // in_screen_side
        { .location=2, .binding=0, .format=VK_FORMAT_R32G32_SFLOAT, .offset=16 },  // in_uv_base
        { .location=3, .binding=0, .format=VK_FORMAT_R32G32_SFLOAT, .offset=24 }   // in_uv_side
    };
    auto vin   = render::vertex_input_info({ &bind, 1 }, attrs);
    auto ia    = render::input_assembly_info(VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST, VK_FALSE);
    auto vpst  = render::viewport_state_info_static({ &vp, 1 }, { &sc, 1 });
    auto rs    = render::rasterization_state_info(/*cull*/VK_CULL_MODE_NONE);
    auto ms    = render::multisample_state_info();
    VkPipelineColorBlendAttachmentState att[1] = { render::alpha_blend };
    auto cb    = render::color_blend_state(att);
    auto stages= render::fragment_vertex_stage_info(fs, vs);

    return render::create_graphics_pipeline(
        m_pipeline, device, stages, &vpst, m_layout, rp,
        rs, cb, vin, ia, ms
    );
}

VkResult TextRenderer::ensure_vb_capacity_(VkDevice device,
                                             VkPhysicalDevice phys,
                                             VkDeviceSize bytes)
{
    if (bytes == 0) bytes = sizeof(TriInstance); // avoid zero size edge case
    if (m_vb && bytes <= m_vbCap) return VK_SUCCESS;

    if (m_vbPtr) { vkUnmapMemory(device, m_vbMem); m_vbPtr = nullptr; }
    if (m_vb)    { vkDestroyBuffer(device, m_vb, nullptr); m_vb = VK_NULL_HANDLE; }
    if (m_vbMem) { vkFreeMemory(device, m_vbMem, nullptr); m_vbMem = VK_NULL_HANDLE; }
    m_vbCap = 0;

    VkBufferCreateInfo bi{};
    bi.sType=VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
    bi.size = bytes;
    bi.usage = VK_BUFFER_USAGE_VERTEX_BUFFER_BIT;
    bi.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
    if (vkCreateBuffer(device, &bi, nullptr, &m_vb)) return VK_ERROR_INITIALIZATION_FAILED;

    VkMemoryRequirements mr{}; vkGetBufferMemoryRequirements(device, m_vb, &mr);
    uint32_t mt =  render::find_mem_type(phys, mr.memoryTypeBits,
        VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT);
    if (mt == UINT32_MAX) return VK_ERROR_MEMORY_MAP_FAILED;

    VkMemoryAllocateInfo ai{};
    ai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
    ai.allocationSize = mr.size; ai.memoryTypeIndex = mt;
    if (vkAllocateMemory(device, &ai, nullptr, &m_vbMem)) return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    if (vkBindBufferMemory(device, m_vb, m_vbMem, 0))      return VK_ERROR_MEMORY_MAP_FAILED;

    if (vkMapMemory(device, m_vbMem, 0, VK_WHOLE_SIZE, 0, &m_vbPtr)) return VK_ERROR_MEMORY_MAP_FAILED;
    m_vbCap = mr.size;
    return VK_SUCCESS;
}

// ---------------------- Public API ----------------------

VkResult TextRenderer::create(VkDevice device,
                                VkRenderPass renderPass,
                                VkShaderModule vs, VkShaderModule fs,
                                const VkViewport& viewport,
                                const VkRect2D&  scissor,
                                VkImageView atlasView,
                                VkSampler   atlasSampler)
{
    m_atlasView    = atlasView;
    m_atlasSampler = atlasSampler;

    // pipeline + layout
    if (auto r = build_pipeline_(device, renderPass, vs, fs, viewport, scissor)) return r;

    // descriptor pool & set for atlas
    VkDescriptorPoolSize poolSize = render::desc_pool_size(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
    auto pinfo = render::desc_pool_info({ &poolSize, 1 }, /*max_sets*/1);
    if (vkCreateDescriptorPool(device, &pinfo, nullptr, &m_pool)) return VK_ERROR_INITIALIZATION_FAILED;

    auto ainfo = render::desc_alloc_info(m_pool, { &m_set1_atlas, 1 });
    if (vkAllocateDescriptorSets(device, &ainfo, &m_ds)) return VK_ERROR_INITIALIZATION_FAILED;

    VkDescriptorImageInfo ii = render::desc_image_info(m_atlasSampler, m_atlasView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkWriteDescriptorSet w = render::desc_write_image(m_ds, 0, &ii);
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);

    // VB will be allocated on first draw
    return VK_SUCCESS;
}

VkResult TextRenderer::record_draw(VkDevice device,
                                   VkPhysicalDevice /*phys*/,
                                   VkCommandBuffer cb,
                                   std::span<const TriPair> pairs,
                                   const float rgba[4])
{
    if (pairs.empty()) return VK_SUCCESS;

    const VkDeviceSize bytes   = VkDeviceSize(pairs.size()) * sizeof(TriInstance);
    const VkDeviceSize dst_off = m_frameUsed;

    // Safety: we must have reserved enough for the whole frame beforehand.
    // Growing here would invalidate earlier recorded draws that bound m_vb.
    if (dst_off + bytes > m_vbCap) {
        // Either assert in debug or return a hard error in release.
        DEBUG_ASSERT(!"TextRenderer: frame ran out of reserved VB space; call reserve_instances() with a larger count");
        return VK_ERROR_OUT_OF_DEVICE_MEMORY;
    }

    // Pack TriPair -> TriInstance *directly* into mapped memory at dst_off
    auto* base_ptr = static_cast<std::byte*>(m_vbPtr) + dst_off;
    auto* out      = reinterpret_cast<TriInstance*>(base_ptr);

    for (uint32_t i = 0; i < pairs.size(); ++i) {
        const RightTriangle& s = pairs[i].screen;
        const RightTriangle& u = pairs[i].uv;
        out[i] = TriInstance{ s.x0, s.y0, s.dx, s.dy, u.x0, u.y0, u.dx, u.dy };
    }

    // Bind & draw using the per-draw vertex buffer *offset*
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_layout, 1, 1, &m_ds, 0, nullptr);
    vkCmdPushConstants(cb, m_layout, VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float) * 4, rgba);

    VkBuffer buf = m_vb;
    VkDeviceSize bind_off = dst_off;
    vkCmdBindVertexBuffers(cb, 0, 1, &buf, &bind_off);
    vkCmdDraw(cb, /*vertexCount*/3, /*instanceCount*/pairs.size(), 0, 0);

    // Advance append pointer for the next draw this frame
    m_frameUsed += bytes;

    // If memory is NON-coherent, you'd need to flush the written range here.
    // (Your allocation uses HOST_COHERENT, so no flush is necessary.)
    return VK_SUCCESS;
}


VkResult TextRenderer::record_draw_line(VkDevice device,
                                          VkPhysicalDevice phys,
                                          VkCommandBuffer cb,
                                          std::string_view s,
                                          float x, float y,
                                          float sx, float sy,
                                          const FontAtlasCPU& cpu,
                                          const float rgba[4])
{
    std::vector<TriPair> pairs;
    text_line_tripairs(pairs, s, x, y, sx, sy, cpu);
    return record_draw(device, phys, cb, pairs, rgba);
}

void TextRenderer::destroy(VkDevice device)
{
    if (m_vbPtr) { vkUnmapMemory(device, m_vbMem); m_vbPtr=nullptr; }
    if (m_vb)    vkDestroyBuffer(device, m_vb, nullptr);
    if (m_vbMem) vkFreeMemory(device, m_vbMem, nullptr);

    if (m_pool)  vkDestroyDescriptorPool(device, m_pool, nullptr);
    if (m_pipeline) vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_layout)   vkDestroyPipelineLayout(device, m_layout, nullptr);
    if (m_set0_empty)vkDestroyDescriptorSetLayout(device, m_set0_empty, nullptr);
    if (m_set1_atlas)vkDestroyDescriptorSetLayout(device, m_set1_atlas, nullptr);

    m_vb = VK_NULL_HANDLE; m_vbMem = VK_NULL_HANDLE; m_vbCap = 0;
    m_set0_empty = VK_NULL_HANDLE; m_set1_atlas = VK_NULL_HANDLE;
    m_layout = VK_NULL_HANDLE; m_pipeline = VK_NULL_HANDLE;
    m_pool = VK_NULL_HANDLE; m_ds = VK_NULL_HANDLE;
    m_atlasView = VK_NULL_HANDLE; m_atlasSampler = VK_NULL_HANDLE;
}

VkResult TextRenderer::maybe_realloc_instances(VkDevice device,
                                           VkPhysicalDevice phys,
                                           uint32_t instance_capacity)
{
    VkDeviceSize bytes = VkDeviceSize(instance_capacity) * sizeof(TriInstance);
    return ensure_vb_capacity_(device, phys, bytes);
}

VkResult TextRenderer::frame_start(VkDevice device,
                                        VkPhysicalDevice phys,
                                        uint32_t instance_capacity)
{	
	// Start a new frame worth of appends.
    m_frameUsed = 0;
	return maybe_realloc_instances(device,phys,instance_capacity);
}