#ifndef TEXT_FORMAT_CAPS_HPP
#define TEXT_FORMAT_CAPS_HPP

// text_format_caps.hpp  (header-only helpers)

// NOTE: we intentionally avoid leading underscores in identifiers.
// These are tiny inline helpers meant for internal selection logic.

#include <array>
#include <span>
#include <vulkan/vulkan.h>

// --- Internal capability checks ---------------------------------------------

inline bool text_supports_sampled_transfer_dst(VkPhysicalDevice phys, VkFormat fmt) {
    VkImageFormatProperties props{};
    return vkGetPhysicalDeviceImageFormatProperties(
        phys, fmt,
        VK_IMAGE_TYPE_2D,
        VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        0, &props
    ) == VK_SUCCESS;
}

inline bool text_supports_linear_filter(VkPhysicalDevice phys, VkFormat fmt) {
    VkFormatProperties fp{};
    vkGetPhysicalDeviceFormatProperties(phys, fmt, &fp);
    return (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
}

inline bool text_supports_filter(VkPhysicalDevice phys, VkFormat fmt, VkFilter f) {
    // NEAREST is always OK for sampled images that pass text_supports_sampled_transfer_dst.
    // LINEAR requires the format feature bit.
    return (f == VK_FILTER_NEAREST) || text_supports_linear_filter(phys, fmt);
}

// --- Selection helpers (format first, then filter) ---------------------------

// Pick the first format from `preferred` that is usable as a sampled,
// transfer-dst 2D image (optimal tiling).
inline bool pick_text_format(VkPhysicalDevice phys,
                             VkFormat& outFormat,
                             std::span<const VkFormat> preferred)
{
    for (VkFormat f : preferred) {
        if (text_supports_sampled_transfer_dst(phys, f)) {
            outFormat = f;
            return true;
        }
    }
    return false;
}

// Overload with a sensible default preference order.
// R8 is ideal for coverage atlases; RGBA8/BGRA8 are fallbacks.
inline bool pick_text_format(VkPhysicalDevice phys, VkFormat& outFormat) {
    constexpr std::array<VkFormat,3> kDefault = {
        VK_FORMAT_R8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM
    };
    return pick_text_format(phys, outFormat, std::span<const VkFormat>(kDefault.data(), kDefault.size()));
}

// Choose the filter for a *given* format. If `preferred` isn’t supported,
// it falls back to the safest option (NEAREST).
inline VkFilter pick_text_filter(VkPhysicalDevice phys,
                                 VkFormat fmt,
                                 VkFilter preferred = VK_FILTER_LINEAR)
{
    return text_supports_filter(phys, fmt, preferred) ? preferred : VK_FILTER_NEAREST;
}

// Convenience: returns true if (format chosen) and outputs the best filter
// given a preference. This is optional; use only if you like the pair API.
inline bool pick_text_format_and_filter(VkPhysicalDevice phys,
                                        VkFormat& outFormat,
                                        VkFilter& outFilter,
                                        VkFilter preferred = VK_FILTER_LINEAR)
{
    if (!pick_text_format(phys, outFormat)) return false;
    outFilter = pick_text_filter(phys, outFormat, preferred);
    return true;
}


#endif // TEXT_FORMAT_CAPS_HPP
