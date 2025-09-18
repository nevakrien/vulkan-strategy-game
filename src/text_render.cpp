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
void text_line_draw_info(std::vector<TriPair>& out,
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

    // set=1: combined image sampler for FS
    VkDescriptorSetLayoutBinding b1 =
        render::desc_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT);
    auto l1 = render::desc_layout_info({ &b1, 1 });
    VK_CHECK(vkCreateDescriptorSetLayout(device, &l1, nullptr, &m_set));

    // pipeline layout: [ set0_empty, set1_atlas ] + FS push-constant vec4
    VkPushConstantRange pc{ VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float)*4 };
    auto pl = render::layout_info({ &m_set, 1 }, { &pc, 1 });
    VK_CHECK(vkCreatePipelineLayout(device, &pl, nullptr, &m_layout));

    // vertex input: one per-instance binding (binding 0)
    VkVertexInputBindingDescription bind{
        .binding   = 0,
        .stride    = sizeof(TriPair),
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

    auto ainfo = render::desc_alloc_info(m_pool, { &m_set, 1 });
    if (vkAllocateDescriptorSets(device, &ainfo, &m_ds)) return VK_ERROR_INITIALIZATION_FAILED;

    VkDescriptorImageInfo ii = render::desc_image_info(m_atlasSampler, m_atlasView,
        VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
    VkWriteDescriptorSet w = render::desc_write_image(m_ds, 0, &ii);
    vkUpdateDescriptorSets(device, 1, &w, 0, nullptr);

    // VB will be allocated on first draw
    return VK_SUCCESS;
}

VkResult TextRenderer::record_draw( VkCommandBuffer cb,
                                    MappedArena& arena,
                                    std::span<const TriPair> pairs,
                                    const float rgba[4])
{
    if (pairs.empty()) return VK_SUCCESS;

    // sanity: the arena must be usable as a vertex buffer
    arena.assert_matches(VK_BUFFER_USAGE_VERTEX_BUFFER_BIT);

    const VkDeviceSize bytes = VkDeviceSize(pairs.size()) * sizeof(TriPair);

    // allocate + copy into the persistently-mapped arena
    UploadAlloc a{};
    VkResult r = arena.allocAndWrite(pairs.data(), bytes, a, /*align=*/16);
    if (r != VK_SUCCESS) {
        // Caller owns growth policy (arena.realloc). We just report OOM.
        return r; // VK_ERROR_OUT_OF_DEVICE_MEMORY if not enough room
    }

    // bind pipeline + descriptors
    vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, m_pipeline);
    vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS,
                            m_layout, 0, 1, &m_ds, 0, nullptr);

    // push color
    vkCmdPushConstants(cb, m_layout, VK_SHADER_STAGE_FRAGMENT_BIT,
                       0, sizeof(float)*4, rgba);

    // bind vertex buffer at the arena offset
    VkBuffer vb = a.buffer;
    VkDeviceSize vbOff = a.offset;
    vkCmdBindVertexBuffers(cb, 0, 1, &vb, &vbOff);

    // each TriPair = one instance of a 3-vertex draw
    vkCmdDraw(cb, /*vertexCount*/3, /*instanceCount*/uint32_t(pairs.size()),
              /*firstVertex*/0, /*firstInstance*/0);

    return VK_SUCCESS;
}

VkResult TextRenderer::record_draw_line( VkCommandBuffer cb,
                                         MappedArena& arena,
                                         std::string_view s,
                                         float x, float y,
                                         float sx, float sy,
                                         const FontAtlasCPU& cpu,
                                         const float rgba[4])
{
    std::vector<TriPair> pairs;
    text_line_draw_info(pairs, s, x, y, sx, sy, cpu);
    return record_draw(cb, arena, pairs, rgba);
}

void TextRenderer::destroy(VkDevice device)
{

    if (m_pool)  vkDestroyDescriptorPool(device, m_pool, nullptr);
    if (m_pipeline) vkDestroyPipeline(device, m_pipeline, nullptr);
    if (m_layout)   vkDestroyPipelineLayout(device, m_layout, nullptr);
    if (m_set)vkDestroyDescriptorSetLayout(device, m_set, nullptr);

    m_set = VK_NULL_HANDLE;
    m_layout = VK_NULL_HANDLE; m_pipeline = VK_NULL_HANDLE;
    m_pool = VK_NULL_HANDLE; m_ds = VK_NULL_HANDLE;
    m_atlasView = VK_NULL_HANDLE; m_atlasSampler = VK_NULL_HANDLE;
}