// smoke_test.cpp
#include <vector>
#include <cstdio>
#include <cstring>
#include <cassert>
#include "render.hpp"

static int run_vulkan_smoke_test_clear_black()
{
    RenderTargets    rt;
    CommandResources cmds;
    FrameSync        sync;

    // Render targets: swapchain clear->present
    rt.init(
        g_vulkan.device,
        g_vulkan.swapchain_format,
        g_vulkan.swapchain_extent,
        g_vulkan.swapchain_image_views // finalLayout defaults to PRESENT_SRC_KHR
    );

    // Command pool + one primary CB per framebuffer
    cmds.init(g_vulkan.device, g_vulkan.graphics_family,
              static_cast<uint32_t>(rt.framebuffers.size()));

    // Record clear-to-black into each command buffer
    for (size_t i = 0; i < cmds.buffers.size(); ++i) {
        VkCommandBufferBeginInfo begin{};
        begin.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        begin.flags = VK_COMMAND_BUFFER_USAGE_SIMULTANEOUS_USE_BIT;
        VK_CHECK(vkBeginCommandBuffer(cmds.buffers[i], &begin));

        VkClearValue clear{};
        clear.color = {{0.f, 0.f, 0.f, 1.f}};

        VkRenderPassBeginInfo rpbi{};
        rpbi.sType                    = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi.renderPass               = rt.render_pass;
        rpbi.framebuffer              = rt.framebuffers[i];
        rpbi.renderArea.offset        = {0, 0};
        rpbi.renderArea.extent        = g_vulkan.swapchain_extent;
        rpbi.clearValueCount          = 1;
        rpbi.pClearValues             = &clear;

        vkCmdBeginRenderPass(cmds.buffers[i], &rpbi, VK_SUBPASS_CONTENTS_INLINE);
        vkCmdEndRenderPass(cmds.buffers[i]);

        VK_CHECK(vkEndCommandBuffer(cmds.buffers[i]));
    }

    // Semaphores + fence
    sync.init(g_vulkan.device);

    // Render loop
    while (!platform_should_quit()) {
        VK_CHECK(vkWaitForFences(g_vulkan.device, 1, &sync.in_flight_fence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(g_vulkan.device, 1, &sync.in_flight_fence));

        uint32_t image_index = 0;
        VkResult acq = vkAcquireNextImageKHR(
            g_vulkan.device, g_vulkan.swapchain, UINT64_MAX,
            sync.image_available, VK_NULL_HANDLE, &image_index);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) break;
        VK_CHECK(acq);

        VkPipelineStageFlags wait_stage = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT;
        VkSubmitInfo submit{};
        submit.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO;
        submit.waitSemaphoreCount   = 1;
        submit.pWaitSemaphores      = &sync.image_available;
        submit.pWaitDstStageMask    = &wait_stage;
        submit.commandBufferCount   = 1;
        submit.pCommandBuffers      = &cmds.buffers[image_index];
        submit.signalSemaphoreCount = 1;
        submit.pSignalSemaphores    = &sync.render_finished;

        VK_CHECK(vkQueueSubmit(g_vulkan.graphics_queue, 1, &submit, sync.in_flight_fence));

        VkPresentInfoKHR present{};
        present.sType              = VK_STRUCTURE_TYPE_PRESENT_INFO_KHR;
        present.waitSemaphoreCount = 1;
        present.pWaitSemaphores    = &sync.render_finished;
        present.swapchainCount     = 1;
        present.pSwapchains        = &g_vulkan.swapchain;
        present.pImageIndices      = &image_index;

        VkResult pres = vkQueuePresentKHR(g_vulkan.present_queue, &present);
        if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) break;
        VK_CHECK(pres);
    }

    // Cleanup in explicit order
    VK_CHECK(vkDeviceWaitIdle(g_vulkan.device));
    sync.shutdown(g_vulkan.device);
    cmds.shutdown(g_vulkan.device);
    rt.shutdown(g_vulkan.device);

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
