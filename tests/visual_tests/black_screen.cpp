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
    VkClearColorValue black{{0.f, 0.f, 0.f, 1.f}};
    cmds.record_clear_all(rt, g_vulkan.swapchain_extent, black);

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

        cmds.submit_one(g_vulkan.graphics_queue, image_index, sync);
        VkResult pres = sync.present_one(g_vulkan.present_queue, g_vulkan.swapchain, image_index);
        
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
