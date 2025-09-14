#ifndef RENDER_HPP
#define RENDER_HPP

#include <platform.hpp>
#include <vector>

struct RenderTargets {
    VkRenderPass                render_pass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>  framebuffers;

    void init(
        VkDevice device,
        VkFormat colorFormat,
        VkExtent2D extent,
        const std::vector<VkImageView>& imageViews,
        VkImageLayout       finalLayout   = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR,
        VkAttachmentLoadOp  loadOp        = VK_ATTACHMENT_LOAD_OP_CLEAR,
        VkAttachmentStoreOp storeOp       = VK_ATTACHMENT_STORE_OP_STORE,
        VkImageLayout       initialLayout = VK_IMAGE_LAYOUT_UNDEFINED
    );

    void shutdown(VkDevice device);
    bool valid() const { return render_pass != VK_NULL_HANDLE; }
};

struct CommandResources {
    VkCommandPool                pool    = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> buffers;

    void init(
        VkDevice device,
        uint32_t queueFamilyIndex,
        uint32_t count,
        VkCommandPoolCreateFlags poolFlags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
        VkCommandBufferLevel level         = VK_COMMAND_BUFFER_LEVEL_PRIMARY
    );

    void shutdown(VkDevice device);
    bool valid() const { return pool != VK_NULL_HANDLE; }
};

struct FrameSync {
    VkSemaphore image_available = VK_NULL_HANDLE;
    VkSemaphore render_finished = VK_NULL_HANDLE;
    VkFence     in_flight_fence = VK_NULL_HANDLE;

    void init(VkDevice device);
    void shutdown(VkDevice device);

    bool valid() const {
        return image_available != VK_NULL_HANDLE &&
               render_finished != VK_NULL_HANDLE &&
               in_flight_fence != VK_NULL_HANDLE;
    }
};

#endif // RENDER_HPP
