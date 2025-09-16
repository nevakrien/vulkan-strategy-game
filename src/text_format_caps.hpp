#ifndef TEXT_FORMAT_CAPS_HPP
#define TEXT_FORMAT_CAPS_HPP

#include <vulkan/vulkan.h>
#include <array>

inline bool _txt_supports_img(VkPhysicalDevice phys, VkFormat fmt) {
    VkImageFormatProperties p{};
    return vkGetPhysicalDeviceImageFormatProperties(
        phys, fmt, VK_IMAGE_TYPE_2D, VK_IMAGE_TILING_OPTIMAL,
        VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT,
        0, &p
    ) == VK_SUCCESS;
}

inline bool _txt_supports_linear(VkPhysicalDevice phys, VkFormat fmt) {
    VkFormatProperties fp{}; vkGetPhysicalDeviceFormatProperties(phys, fmt, &fp);
    return (fp.optimalTilingFeatures & VK_FORMAT_FEATURE_SAMPLED_IMAGE_FILTER_LINEAR_BIT) != 0;
}

struct TextFormatCaps {
    VkFormat format = VK_FORMAT_UNDEFINED;
    VkFilter filter = VK_FILTER_NEAREST; // LINEAR if allowed, else NEAREST
};

inline bool pick_text_caps(VkPhysicalDevice phys, TextFormatCaps& out) {
    const std::array<VkFormat,3> cands = {
        VK_FORMAT_R8_UNORM,
        VK_FORMAT_R8G8B8A8_UNORM,
        VK_FORMAT_B8G8R8A8_UNORM
    };
    for (VkFormat f : cands) {
        if (!_txt_supports_img(phys, f)) continue;
        out.format = f;
        out.filter = _txt_supports_linear(phys, f) ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
        return true;
    }
    return false;
}

#endif // TEXT_FORMAT_CAPS_HPP
