#ifndef TEXT_ATLAS_HPP
#define TEXT_ATLAS_HPP

#include <vulkan/vulkan.h>
#include <ft2build.h>
#include FT_FREETYPE_H
#include <unordered_map>
#include <vector>
#include <cstdint>

struct GlyphInfo {
    float u0, v0, u1, v1;   // normalized UVs
    int   width, height;    // px
    int   bearingX, bearingY;
    int   advance;          // px
};

struct FontAtlasCPU {
    std::vector<uint8_t> pixels; // R channel, width*height
    uint32_t width = 0, height = 0;
    int ascent = 0, descent = 0, line_gap = 0;
    std::unordered_map<uint32_t,GlyphInfo> glyphs; // codepoint -> metrics
};

struct FontAtlasGPU {
    VkImage        image  = VK_NULL_HANDLE;
    VkDeviceMemory memory = VK_NULL_HANDLE;
    VkImageView    view   = VK_NULL_HANDLE;
    VkFormat       format = VK_FORMAT_UNDEFINED;
    uint32_t width = 0, height = 0;
};

// Build CPU atlas from a trusted font.
// Returns false on FreeType failure.
bool build_cpu_font_atlas(FT_Library ft, const char* font_path,
                          uint32_t pixel_height,
                          FontAtlasCPU& out,
                          int pad = 1,
                          const std::vector<uint32_t>& codepoints = {});

// Build a complete GPU atlas (image+view+sampler(if not present)) and upload pixels internally.
// On success, 'out' is ready in SHADER_READ_ONLY_OPTIMAL.
// Cleanup is on caller for fail case
//
// note that not all formats are sensible because we use 1 pixel of padding
// this is fine for most sensible one but be aware to avoid bleed
VkResult build_font_atlas_gpu(VkDevice device, VkPhysicalDevice phys,
                              VkQueue queue, uint32_t queueFamily,
                              VkFormat fmt,
                              const FontAtlasCPU& cpu,
                              FontAtlasGPU& out);


// Destroy GPU resources created by build_font_atlas_gpu
inline void destroy_gpu_font_atlas(VkDevice dev, FontAtlasGPU& gpu) {
    // if (gpu.sampler) vkDestroySampler(dev, gpu.sampler, nullptr);
    if (gpu.view)    vkDestroyImageView(dev, gpu.view, nullptr);
    if (gpu.image)   vkDestroyImage(dev, gpu.image, nullptr);
    if (gpu.memory)  vkFreeMemory(dev, gpu.memory, nullptr);
    gpu = {};
}

// Tiny helper if you want font px from screen size
inline uint32_t choose_font_px_for_screen(VkExtent2D screen,float scale = 1.0f/10.0f) {
    float s = float((screen.width < screen.height) ? screen.width : screen.height);
    uint32_t px = (uint32_t)(s * scale);
    if (px < 16)  px = 16;
    if (px > 128) px = 128;
    return px;
}

#endif // TEXT_ATLAS_HPP
