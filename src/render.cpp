#include "render.hpp"

static void build_color_only_renderpass_and_fbos(
    VkDevice device,
    VkFormat colorFormat,
    VkExtent2D extent,
    const std::vector<VkImageView>& imageViews,
    VkAttachmentLoadOp  loadOp,
    VkAttachmentStoreOp storeOp,
    VkImageLayout       initialLayout,
    VkImageLayout       finalLayout,
    VkRenderPass*       out_rp,
    std::vector<VkFramebuffer>* out_fbos
) {
    VkAttachmentDescription color{};
    color.format         = colorFormat;
    color.samples        = VK_SAMPLE_COUNT_1_BIT;
    color.loadOp         = loadOp;
    color.storeOp        = storeOp;
    color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
    color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
    color.initialLayout  = initialLayout;
    color.finalLayout    = finalLayout;

    VkAttachmentReference color_ref{};
    color_ref.attachment = 0;
    color_ref.layout     = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;

    VkSubpassDescription subpass{};
    subpass.pipelineBindPoint    = VK_PIPELINE_BIND_POINT_GRAPHICS;
    subpass.colorAttachmentCount = 1;
    subpass.pColorAttachments    = &color_ref;

    VkSubpassDependency dep{};
    dep.srcSubpass    = VK_SUBPASS_EXTERNAL;
    dep.dstSubpass    = 0;
    dep.srcStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.dstStageMask  = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
    dep.srcAccessMask = 0;
    dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT;

    VkRenderPassCreateInfo rp{};
    rp.sType           = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO;
    rp.attachmentCount = 1;
    rp.pAttachments    = &color;
    rp.subpassCount    = 1;
    rp.pSubpasses      = &subpass;
    rp.dependencyCount = 1;
    rp.pDependencies   = &dep;

    VK_CHECK(vkCreateRenderPass(device, &rp, nullptr, out_rp));

    out_fbos->resize(imageViews.size());
    for (size_t i = 0; i < imageViews.size(); ++i) {
        VkImageView attachments[] = { imageViews[i] };

        VkFramebufferCreateInfo fb{};
        fb.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
        fb.renderPass      = *out_rp;
        fb.attachmentCount = 1;
        fb.pAttachments    = attachments;
        fb.width           = extent.width;
        fb.height          = extent.height;
        fb.layers          = 1;

        VK_CHECK(vkCreateFramebuffer(device, &fb, nullptr, &(*out_fbos)[i]));
    }
}

void RenderTargets::init(
    VkDevice device,
    VkFormat colorFormat,
    VkExtent2D extent,
    const std::vector<VkImageView>& imageViews,
    VkImageLayout       finalLayout,
    VkAttachmentLoadOp  loadOp,
    VkAttachmentStoreOp storeOp,
    VkImageLayout       initialLayout
) {
    shutdown(device);
    build_color_only_renderpass_and_fbos(
        device, colorFormat, extent, imageViews,
        loadOp, storeOp, initialLayout, finalLayout,
        &render_pass, &framebuffers
    );
}

void RenderTargets::shutdown(VkDevice device) {
    for (VkFramebuffer fb : framebuffers) {
        if (fb) vkDestroyFramebuffer(device, fb, nullptr);
    }
    framebuffers.clear();
    if (render_pass) {
        vkDestroyRenderPass(device, render_pass, nullptr);
        render_pass = VK_NULL_HANDLE;
    }
}

// --- CommandResources ---

void CommandResources::init(
    VkDevice device,
    uint32_t queueFamilyIndex,
    uint32_t count,
    VkCommandPoolCreateFlags poolFlags,
    VkCommandBufferLevel level
) {
    shutdown(device);

    VkCommandPoolCreateInfo pci{};
    pci.sType            = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
    pci.queueFamilyIndex = queueFamilyIndex;
    pci.flags            = poolFlags;

    VK_CHECK(vkCreateCommandPool(device, &pci, nullptr, &pool));

    buffers.resize(count);
    if (count > 0) {
        VkCommandBufferAllocateInfo ai{};
        ai.sType              = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool        = pool;
        ai.level              = level;
        ai.commandBufferCount = count;
        VK_CHECK(vkAllocateCommandBuffers(device, &ai, buffers.data()));
    }
}

void CommandResources::shutdown(VkDevice device) {
    if (pool) {
        if (!buffers.empty()) {
            vkFreeCommandBuffers(device, pool,
                                 static_cast<uint32_t>(buffers.size()),
                                 buffers.data());
            buffers.clear();
        }
        vkDestroyCommandPool(device, pool, nullptr);
        pool = VK_NULL_HANDLE;
    } else {
        buffers.clear();
    }
}

// --- FrameSync ---

void FrameSync::init(VkDevice device) {
    shutdown(device);

    VkSemaphoreCreateInfo si{};
    si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
    VK_CHECK(vkCreateSemaphore(device, &si, nullptr, &image_available));
    VK_CHECK(vkCreateSemaphore(device, &si, nullptr, &render_finished));

    VkFenceCreateInfo fi{};
    fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
    fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
    VK_CHECK(vkCreateFence(device, &fi, nullptr, &in_flight_fence));
}

void FrameSync::shutdown(VkDevice device) {
    if (in_flight_fence) { vkDestroyFence(device, in_flight_fence, nullptr); in_flight_fence = VK_NULL_HANDLE; }
    if (render_finished) { vkDestroySemaphore(device, render_finished, nullptr); render_finished = VK_NULL_HANDLE; }
    if (image_available) { vkDestroySemaphore(device, image_available, nullptr); image_available = VK_NULL_HANDLE; }
}
