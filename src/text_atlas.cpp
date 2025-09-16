// src/text_atlas.cpp
#include "text_atlas.hpp"

#include <algorithm>
#include <cmath>
#include <cstring>

// -----------------------------
// helpers (internal)
// -----------------------------

static std::vector<uint32_t> ascii_set() {
    std::vector<uint32_t> v; v.reserve(96);
    for (uint32_t c = 32; c <= 126; ++c) v.push_back(c);
    v.push_back('?');
    return v;
}

static uint32_t next_pow2(uint32_t v) {
    if (v == 0) return 1;
    v--; v|=v>>1; v|=v>>2; v|=v>>4; v|=v>>8; v|=v>>16;
    return v+1;
}

static uint32_t find_mem_type(uint32_t typeBits, VkMemoryPropertyFlags req, VkPhysicalDevice phys) {
    VkPhysicalDeviceMemoryProperties mp{}; vkGetPhysicalDeviceMemoryProperties(phys, &mp);
    for (uint32_t i=0;i<mp.memoryTypeCount;++i)
        if ((typeBits & (1u<<i)) && ((mp.memoryTypes[i].propertyFlags & req) == req))
            return i;
    return ~0u;
}

// Classic barrier path (no sync2)
static void classic_layout_transition(VkCommandBuffer cb, VkImage img,
                                      VkImageLayout oldLayout, VkImageLayout newLayout) {
    VkImageMemoryBarrier b{};
    b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER;
    b.oldLayout = oldLayout;
    b.newLayout = newLayout;
    b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
    b.image = img;
    b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
    b.subresourceRange.baseMipLevel = 0;
    b.subresourceRange.levelCount   = 1;
    b.subresourceRange.baseArrayLayer = 0;
    b.subresourceRange.layerCount     = 1;

    VkPipelineStageFlags srcStage = VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT;
    VkPipelineStageFlags dstStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
    if (oldLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL && newLayout == VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL) {
        srcStage = VK_PIPELINE_STAGE_TRANSFER_BIT;
        dstStage = VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT;
        b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
        b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT;
    } else if (oldLayout == VK_IMAGE_LAYOUT_UNDEFINED && newLayout == VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL) {
        b.srcAccessMask = 0;
        b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT;
    }

    vkCmdPipelineBarrier(cb,
        srcStage, dstStage,
        0,
        0, nullptr,
        0, nullptr,
        1, &b);
}

// -----------------------------
// FreeType CPU atlas build
// -----------------------------

bool build_cpu_font_atlas(FT_Library ft, const char* font_path,
                          uint32_t pixel_height,
                          FontAtlasCPU& out,
                          int pad,
                          const std::vector<uint32_t>& codepoints)
{
    FT_Face face = nullptr;
    if (FT_New_Face(ft, font_path, 0, &face)) return false;
    FT_Set_Pixel_Sizes(face, 0, pixel_height);

    out.ascent   = face->size->metrics.ascender  >> 6;
    out.descent  = -(face->size->metrics.descender >> 6);
    out.line_gap = (face->size->metrics.height   >> 6) - (out.ascent + out.descent);

    const auto cps = codepoints.empty() ? ascii_set() : codepoints;

    struct Tmp {
        uint32_t cp; int w,h,bx,by,adv; std::vector<uint8_t> pix;
    };
    std::vector<Tmp> glyphs; glyphs.reserve(cps.size());

    size_t totalPx = 0;
    for (uint32_t cp : cps) {
        if (FT_Load_Char(face, cp, FT_LOAD_RENDER)) continue;
        auto* g = face->glyph;
        const int w = int(g->bitmap.width);
        const int h = int(g->bitmap.rows);
        const int pitch = g->bitmap.pitch;

        Tmp t{};
        t.cp = cp; t.w=w; t.h=h; t.bx=g->bitmap_left; t.by=g->bitmap_top; t.adv=int(g->advance.x >> 6);
        t.pix.resize(size_t(std::max(0,w)) * std::max(0,h));

        //copy without the padding
        if (w > 0 && h > 0) {
            const uint8_t* base = g->bitmap.buffer;
            if (pitch >= 0) {
                for (int y=0; y<h; ++y)
                    std::memcpy(t.pix.data()+size_t(y)*w, base+size_t(y)*pitch, size_t(w));
            } else {
                for (int y=0; y<h; ++y) {
                    const uint8_t* srcRow = base + size_t(h-1-y) * size_t(-pitch);
                    std::memcpy(t.pix.data()+size_t(y)*w, srcRow, size_t(w));
                }
            }
        }

        glyphs.push_back(std::move(t));
        totalPx += size_t(std::max(1,w)) * std::max(1,h);
    }

    const uint32_t targetArea = uint32_t(totalPx + totalPx/8);
    const uint32_t estSide = next_pow2(uint32_t(std::ceil(std::sqrt(double(targetArea)))));
    const uint32_t atlasW  = std::clamp(estSide, 256u, 2048u);

    int penX=pad, penY=pad, rowH=0;
    for (auto& t : glyphs) {
        if (penX + t.w + pad > int(atlasW)) { penX = pad; penY += rowH + pad; rowH = 0; }
        rowH = std::max(rowH, t.h);
        penX += t.w + pad;
    }
    uint32_t atlasH = next_pow2(uint32_t(penY + rowH + pad));
    atlasH = std::clamp(atlasH, 32u, 4096u);

    out.width = atlasW; out.height = atlasH;
    out.pixels.assign(size_t(atlasW) * atlasH, 0u);
    out.glyphs.clear(); out.glyphs.reserve(glyphs.size());

    penX=pad; penY=pad; rowH=0;
    for (auto& t : glyphs) {
        if (penX + t.w + pad > int(atlasW)) { penX = pad; penY += rowH + pad; rowH = 0; }
        if (t.w > 0 && t.h > 0) {
            for (int y=0; y<t.h; ++y) {
                std::memcpy(&out.pixels[size_t(penX) + size_t(penY + y) * atlasW],
                            t.pix.data() + size_t(y)*t.w, size_t(t.w));
            }
            rowH = std::max(rowH, t.h);
        }

        GlyphInfo gi{};
        gi.u0 = float(penX) / atlasW;            gi.v0 = float(penY) / atlasH;
        gi.u1 = float(penX + t.w) / atlasW;      gi.v1 = float(penY + t.h) / atlasH;
        gi.width=t.w; gi.height=t.h; gi.bearingX=t.bx; gi.bearingY=t.by; gi.advance=t.adv;
        out.glyphs.emplace(t.cp, gi);

        penX += t.w + pad;
    }

    FT_Done_Face(face);
    return true;
}

//this function can be optimized by taking in cb and fence
VkResult build_font_atlas_gpu(VkDevice device, VkPhysicalDevice phys,
                              VkQueue queue, uint32_t queueFamily,
                              VkFormat fmt,
                              const FontAtlasCPU& cpu,
                              FontAtlasGPU& out)
{
    if (cpu.width == 0 || cpu.height == 0 || cpu.pixels.empty())
        return VK_ERROR_INITIALIZATION_FAILED;

    // declare all resources up-front to avoid goto-crossing-initialization
    VkResult       r          = VK_SUCCESS;
    VkBuffer       staging    = VK_NULL_HANDLE;
    VkDeviceMemory stagingMem = VK_NULL_HANDLE;
    VkCommandPool  pool       = VK_NULL_HANDLE;
    VkCommandBuffer cb        = VK_NULL_HANDLE;
    VkFence        fence      = VK_NULL_HANDLE;

    // reset 'out' and stamp dimensions/format early (safe to re-stamp on success)
    out = {};
    out.format = fmt;
    out.width  = cpu.width;
    out.height = cpu.height;


    // --- image ---
    {
        VkImageCreateInfo ici{};
        ici.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO;
        ici.imageType = VK_IMAGE_TYPE_2D;
        ici.format = fmt;
        ici.extent = { cpu.width, cpu.height, 1 };
        ici.mipLevels = 1;
        ici.arrayLayers = 1;
        ici.samples = VK_SAMPLE_COUNT_1_BIT;
        ici.tiling  = VK_IMAGE_TILING_OPTIMAL;
        ici.usage   = VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
        ici.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        ici.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
        r = vkCreateImage(device, &ici, nullptr, &out.image); if (r) goto FAIL;

        VkMemoryRequirements mr{}; vkGetImageMemoryRequirements(device, out.image, &mr);
        uint32_t mt = find_mem_type(mr.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, phys);
        if (mt == ~0u) { r = VK_ERROR_MEMORY_MAP_FAILED; goto FAIL; }

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = mt;
        r = vkAllocateMemory(device, &mai, nullptr, &out.memory); if (r) goto FAIL;
        r = vkBindImageMemory(device, out.image, out.memory, 0); if (r) goto FAIL;
    }

    // --- view ---
    {
        VkImageViewCreateInfo iv{};
        iv.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
        iv.image = out.image;
        iv.viewType = VK_IMAGE_VIEW_TYPE_2D;
        iv.format = fmt;
        iv.components = {
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
            VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY
        };
        iv.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        iv.subresourceRange.baseMipLevel = 0;
        iv.subresourceRange.levelCount   = 1;
        iv.subresourceRange.baseArrayLayer = 0;
        iv.subresourceRange.layerCount     = 1;
        r = vkCreateImageView(device, &iv, nullptr, &out.view); if (r) goto FAIL;
    }

    // --- staging buffer ---
    {
        VkBufferCreateInfo bci{};
        bci.sType = VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO;
        bci.size = cpu.pixels.size();
        bci.usage = VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
        bci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
        r = vkCreateBuffer(device, &bci, nullptr, &staging); if (r) goto FAIL;

        VkMemoryRequirements mr{}; vkGetBufferMemoryRequirements(device, staging, &mr);
        uint32_t mt = find_mem_type(mr.memoryTypeBits,
                                    VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT,
                                    phys);
        if (mt == ~0u) { r = VK_ERROR_MEMORY_MAP_FAILED; goto FAIL; }

        VkMemoryAllocateInfo mai{};
        mai.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO;
        mai.allocationSize = mr.size;
        mai.memoryTypeIndex = mt;
        r = vkAllocateMemory(device, &mai, nullptr, &stagingMem); if (r) goto FAIL;
        r = vkBindBufferMemory(device, staging, stagingMem, 0); if (r) goto FAIL;

        void* mapped = nullptr;
        r = vkMapMemory(device, stagingMem, 0, VK_WHOLE_SIZE, 0, &mapped); if (r) goto FAIL;
        std::memcpy(mapped, cpu.pixels.data(), cpu.pixels.size());
        vkUnmapMemory(device, stagingMem);
    }

    // --- transient pool/cb ---
    {
        VkCommandPoolCreateInfo pci{};
        pci.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pci.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT | VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        pci.queueFamilyIndex = queueFamily;
        r = vkCreateCommandPool(device, &pci, nullptr, &pool); if (r) goto UPLOAD_CLEANUP;

        VkCommandBufferAllocateInfo cai{};
        cai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        cai.commandPool = pool;
        cai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        cai.commandBufferCount = 1;
        r = vkAllocateCommandBuffers(device, &cai, &cb); if (r) goto UPLOAD_CLEANUP;

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
        r = vkBeginCommandBuffer(cb, &bi); if (r) goto UPLOAD_CLEANUP;

        // sync2 available?
        PFN_vkCmdPipelineBarrier2 pfnBarrier2 =
            reinterpret_cast<PFN_vkCmdPipelineBarrier2>(
                vkGetDeviceProcAddr(device, "vkCmdPipelineBarrier2"));

        // UNDEFINED -> TRANSFER_DST
        if (pfnBarrier2) {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask = VK_PIPELINE_STAGE_2_TOP_OF_PIPE_BIT;
            b.srcAccessMask= 0;
            b.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            b.dstAccessMask= VK_ACCESS_2_TRANSFER_WRITE_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED;
            b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = out.image;
            b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.baseMipLevel = 0;
            b.subresourceRange.levelCount   = 1;
            b.subresourceRange.baseArrayLayer = 0;
            b.subresourceRange.layerCount     = 1;

            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;

            pfnBarrier2(cb, &dep);
        } else {
            classic_layout_transition(cb, out.image,
                                      VK_IMAGE_LAYOUT_UNDEFINED,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL);
        }

        // copy buffer -> image
        VkBufferImageCopy region{};
        region.bufferOffset = 0;
        region.bufferRowLength   = 0;
        region.bufferImageHeight = 0;
        region.imageSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
        region.imageSubresource.mipLevel = 0;
        region.imageSubresource.baseArrayLayer = 0;
        region.imageSubresource.layerCount = 1;
        region.imageOffset = {0,0,0};
        region.imageExtent = { cpu.width, cpu.height, 1 };
        vkCmdCopyBufferToImage(cb, staging, out.image,
                               VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);

        // TRANSFER_DST -> SHADER_READ_ONLY
        if (pfnBarrier2) {
            VkImageMemoryBarrier2 b{};
            b.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
            b.srcStageMask  = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
            b.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
            b.dstStageMask  = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
            b.dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
            b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
            b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
            b.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
            b.image = out.image;
            b.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
            b.subresourceRange.baseMipLevel = 0;
            b.subresourceRange.levelCount   = 1;
            b.subresourceRange.baseArrayLayer = 0;
            b.subresourceRange.layerCount     = 1;

            VkDependencyInfo dep{};
            dep.sType = VK_STRUCTURE_TYPE_DEPENDENCY_INFO;
            dep.imageMemoryBarrierCount = 1;
            dep.pImageMemoryBarriers = &b;

            pfnBarrier2(cb, &dep);
        } else {
            classic_layout_transition(cb, out.image,
                                      VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL,
                                      VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL);
        }

        r = vkEndCommandBuffer(cb); if (r) goto UPLOAD_CLEANUP;

        // submit + wait
        VkFenceCreateInfo fci{};
        fci.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        r = vkCreateFence(device, &fci, nullptr, &fence); if (r) goto UPLOAD_CLEANUP;

        VkSubmitInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        si.commandBufferCount = 1;
        si.pCommandBuffers = &cb;

        r = vkQueueSubmit(queue, 1, &si, fence);
        if (!r) r = vkWaitForFences(device, 1, &fence, VK_TRUE, UINT64_MAX);
        vkDestroyFence(device, fence, nullptr); fence = VK_NULL_HANDLE;
        if (r) goto UPLOAD_CLEANUP;
    }

UPLOAD_CLEANUP:
    if (cb)   vkFreeCommandBuffers(device, pool, 1, &cb);
    if (pool) vkDestroyCommandPool(device, pool, nullptr);
    if (staging != VK_NULL_HANDLE) vkDestroyBuffer(device, staging, nullptr);
    if (stagingMem != VK_NULL_HANDLE) vkFreeMemory(device, stagingMem, nullptr);

    if (r) goto FAIL;
    return VK_SUCCESS;

FAIL:
    // destroy_gpu_font_atlas(device, out);
    return r;
}

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
