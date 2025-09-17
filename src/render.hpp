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

    // Present a single image index from a swapchain, waiting on render_finished.
    // Returns the VkResult from vkQueuePresentKHR.
    VkResult present_one(
        VkQueue presentQueue,
        VkSwapchainKHR swapchain,
        uint32_t imageIndex
    ) const;

    // Submit this->buffers[imageIndex] with the standard “imageAvailable -> draw -> renderFinished” chain.
    // If fence == VK_NULL_HANDLE, uses in_flight_fence by default.
    VkResult submit_one(
        VkQueue queue,
        uint32_t imageIndex,
        const CommandResources& cmd,
        VkPipelineStageFlags waitDstStage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT,
        VkFence fence = VK_NULL_HANDLE
    ) const;
};

// struct GpuArena {
//     VkBufferCreateInfo info;
//     VkBuffer        buffer   = VK_NULL_HANDLE;
//     VkDeviceMemory  mem      = VK_NULL_HANDLE;
//     VkDeviceSize    used     = 0;
//     VkDeviceSize    capacity = 0;

//     //
    

//     //allways reallocs to the current spec
//     VkResult realloc_buffer(VkPhysicalDevice phys,VkDevice device,size_t size);
//     VkResult maybe_grow(VkPhysicalDevice phys,VkDevice device,size_t size){
//         VkMemory
//     }

//     void destroy(VkDevice device); 
// };

#endif // RENDER_HPP
