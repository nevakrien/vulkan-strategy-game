#include "text_render.hpp"

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
