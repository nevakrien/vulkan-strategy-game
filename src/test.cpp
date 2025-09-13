// smoke_test.cpp
#include <vector>
#include <cstdio>
#include <cstring>
#include <cassert>
#include <vulkan/vulkan.h>

#include "platform.hpp"

static int run_vulkan_smoke_test_clear_black()
{
    std::vector<VkImageView>    swapchain_image_views;
    VkRenderPass                render_pass = VK_NULL_HANDLE;
    std::vector<VkFramebuffer>  framebuffers;

    VkCommandPool               command_pool = VK_NULL_HANDLE;
    std::vector<VkCommandBuffer> command_buffers;

    VkSemaphore image_available = VK_NULL_HANDLE;
    VkSemaphore render_finished = VK_NULL_HANDLE;
    VkFence     in_flight_fence = VK_NULL_HANDLE;

    // Image views
    {
        const size_t n = g_vulkan.swapchain_images.size();
        swapchain_image_views.resize(n);
        for (size_t i = 0; i < n; ++i) {
            VkImageViewCreateInfo info{};
            info.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO;
            info.image = g_vulkan.swapchain_images[i];
            info.viewType = VK_IMAGE_VIEW_TYPE_2D;
            info.format = g_vulkan.swapchain_format;
            info.components = {
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY,
                VK_COMPONENT_SWIZZLE_IDENTITY, VK_COMPONENT_SWIZZLE_IDENTITY
            };
            info.subresourceRange.aspectMask     = VK_IMAGE_ASPECT_COLOR_BIT;
            info.subresourceRange.baseMipLevel   = 0;
            info.subresourceRange.levelCount     = 1;
            info.subresourceRange.baseArrayLayer = 0;
            info.subresourceRange.layerCount     = 1;
            VK_CHECK(vkCreateImageView(g_vulkan.device, &info, nullptr, &swapchain_image_views[i]));
        }
    }

    // Render pass
    {
        VkAttachmentDescription color{};
        color.format         = g_vulkan.swapchain_format;
        color.samples        = VK_SAMPLE_COUNT_1_BIT;
        color.loadOp         = VK_ATTACHMENT_LOAD_OP_CLEAR;
        color.storeOp        = VK_ATTACHMENT_STORE_OP_STORE;
        color.stencilLoadOp  = VK_ATTACHMENT_LOAD_OP_DONT_CARE;
        color.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE;
        color.initialLayout  = VK_IMAGE_LAYOUT_UNDEFINED;
        color.finalLayout    = VK_IMAGE_LAYOUT_PRESENT_SRC_KHR;

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

        VK_CHECK(vkCreateRenderPass(g_vulkan.device, &rp, nullptr, &render_pass));
    }

    // Framebuffers
    {
        const size_t n = swapchain_image_views.size();
        framebuffers.resize(n);
        for (size_t i = 0; i < n; ++i) {
            VkImageView attachments[] = { swapchain_image_views[i] };
            VkFramebufferCreateInfo fb{};
            fb.sType           = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO;
            fb.renderPass      = render_pass;
            fb.attachmentCount = 1;
            fb.pAttachments    = attachments;
            fb.width           = g_vulkan.swapchain_extent.width;
            fb.height          = g_vulkan.swapchain_extent.height;
            fb.layers          = 1;
            VK_CHECK(vkCreateFramebuffer(g_vulkan.device, &fb, nullptr, &framebuffers[i]));
        }
    }

    // Command pool + buffers
    {
        VkCommandPoolCreateInfo pool{};
        pool.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO;
        pool.queueFamilyIndex = g_vulkan.graphics_family;
        pool.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT;
        VK_CHECK(vkCreateCommandPool(g_vulkan.device, &pool, nullptr, &command_pool));

        command_buffers.resize(framebuffers.size());
        VkCommandBufferAllocateInfo ai{};
        ai.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO;
        ai.commandPool = command_pool;
        ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
        ai.commandBufferCount = static_cast<uint32_t>(command_buffers.size());
        VK_CHECK(vkAllocateCommandBuffers(g_vulkan.device, &ai, command_buffers.data()));
    }

    // Record clear-to-black
    {
        for (size_t i = 0; i < command_buffers.size(); ++i) {
            VkCommandBufferBeginInfo begin{};
            begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
            begin.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
            VK_CHECK(vkBeginCommandBuffer(command_buffers[i], &begin));

            VkClearValue clear{};
            clear.color = {{0.f, 0.f, 0.f, 1.f}};

            VkRenderPassBeginInfo rpbi{};
            rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
            rpbi.renderPass  = render_pass;
            rpbi.framebuffer = framebuffers[i];
            rpbi.renderArea.offset = {0, 0};
            rpbi.renderArea.extent = g_vulkan.swapchain_extent;
            rpbi.clearValueCount   = 1;
            rpbi.pClearValues      = &clear;

            vkCmdBeginRenderPass(command_buffers[i], &rpbi, VK_SUBPASS_CONTENTS_INLINE);
            vkCmdEndRenderPass(command_buffers[i]);

            VK_CHECK(vkEndCommandBuffer(command_buffers[i]));
        }
    }

    // Sync
    {
        VkSemaphoreCreateInfo si{};
        si.sType = VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO;
        VK_CHECK(vkCreateSemaphore(g_vulkan.device, &si, nullptr, &image_available));
        VK_CHECK(vkCreateSemaphore(g_vulkan.device, &si, nullptr, &render_finished));

        VkFenceCreateInfo fi{};
        fi.sType = VK_STRUCTURE_TYPE_FENCE_CREATE_INFO;
        fi.flags = VK_FENCE_CREATE_SIGNALED_BIT;
        VK_CHECK(vkCreateFence(g_vulkan.device, &fi, nullptr, &in_flight_fence));
    }

    // Render loop
    while (!platform_should_quit()) {
        VK_CHECK(vkWaitForFences(g_vulkan.device, 1, &in_flight_fence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(g_vulkan.device, 1, &in_flight_fence));

        uint32_t image_index = 0;
        VkResult acq = vkAcquireNextImageKHR(
            g_vulkan.device, g_vulkan.swapchain, UINT64_MAX,
            image_available, VK_NULL_HANDLE, &image_index);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) break;
        VK_CHECK(acq);

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount   = 1;
        submit.pWaitSemaphores      = &image_available;
        submit.pWaitDstStageMask    = &wait_stage;
        submit.commandBufferCount   = 1;
        submit.pCommandBuffers      = &command_buffers[image_index];
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores    = &render_finished;
        VK_CHECK(vkQueueSubmit(g_vulkan.graphics_queue, 1, &submit, in_flight_fence));

        VkPresentInfoKHR present{};
        present.sType = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores    = &render_finished;
        present.swapchainCount     = 1;
        present.pSwapchains        = &g_vulkan.swapchain;
        present.pImageIndices      = &image_index;

        VkResult pres = vkQueuePresentKHR(g_vulkan.present_queue, &present);
        if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) break;
        VK_CHECK(pres);
    }

    // Cleanup
    VK_CHECK(vkDeviceWaitIdle(g_vulkan.device));

    if (in_flight_fence)  vkDestroyFence    (g_vulkan.device, in_flight_fence,  nullptr);
    if (render_finished)  vkDestroySemaphore(g_vulkan.device, render_finished,  nullptr);
    if (image_available)  vkDestroySemaphore(g_vulkan.device, image_available,  nullptr);

    if (!command_buffers.empty()) {
        vkFreeCommandBuffers(
            g_vulkan.device, command_pool,
            static_cast<uint32_t>(command_buffers.size()), command_buffers.data()
        );
        command_buffers.clear();
    }
    if (command_pool) vkDestroyCommandPool(g_vulkan.device, command_pool, nullptr);

    for (auto fb : framebuffers) if (fb) vkDestroyFramebuffer(g_vulkan.device, fb, nullptr);
    framebuffers.clear();

    if (render_pass) vkDestroyRenderPass(g_vulkan.device, render_pass, nullptr);

    for (auto iv : swapchain_image_views) if (iv) vkDestroyImageView(g_vulkan.device, iv, nullptr);
    swapchain_image_views.clear();

    return 0;
}

#include <iostream>

int main() {
    std::cout << "hello, world 👋 \n";
    print_libs();

    if (!platform_init()) return 1;
    std::cout << "platform initialized\n";

    const int rc = run_vulkan_smoke_test_clear_black();

    std::cout << "cleanup\n";
    platform_shutdown();
    return rc;
}
