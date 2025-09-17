// tests/text_render_hello.cpp
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string_view>
#include <cstring>
#include <cmath>

#include "platform.hpp"
#include "render.hpp"
#include "render_pipeline.hpp"
#include "shader_compile.hpp"
#include "text_format_caps.hpp"
#include "text_atlas.hpp"
#include "text_render.hpp"   // your VB-only TextRenderer API

static const char* kFallbackFonts[] = {
    "assets/Arialn.ttf",
#ifdef __linux__
    "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf",
    "/usr/share/fonts/truetype/liberation/LiberationSans-Regular.ttf",
#endif
#ifdef _WIN32
    "C:\\Windows\\Fonts\\arial.ttf",
#endif
#ifdef __APPLE__
    "/System/Library/Fonts/Supplemental/Arial.ttf",
    "/System/Library/Fonts/Supplemental/Helvetica.ttc",
#endif
};

static bool try_build_cpu_atlas_from_any_font(uint32_t px, FontAtlasCPU& out) {
    for (const char* path : kFallbackFonts) {
        if (build_cpu_font_atlas(free_type, path, px, out)) {
            std::fprintf(stdout, "[text_render_hello] Using font: %s\n", path);
            return true;
        }
    }
    return false;
}

static VkShaderModule make_shader(VkDevice dev, EShLanguage stage,
                                  std::string_view src, const char* dbg) {
    shader::Options opt; // engine defaults (VK 1.0 / SPV 1.0 or whatever you default to)
    auto res = shader::compile_glsl_to_spirv(stage, src, opt, dbg);
    if (!res.ok) {
        std::fprintf(stderr, "[text_render_hello] %s compile failed:\n%s\n", dbg, res.log.c_str());
        std::abort();
    }
    return shader::make_shader_module(dev, res.spirv);
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    if (!platform_init()) {
        std::fprintf(stderr, "[text_render_hello] platform_init failed\n");
        return 1;
    }

    // ----- Build atlas (CPU+GPU) -----
    VkFormat format; VkFilter filter;
    if (!pick_text_format_and_filter(g_vulkan.physical_device, format, filter)) {
        std::fprintf(stderr, "[text_render_hello] No suitable text format\n");
        platform_shutdown();
        return 1;
    }

    const VkExtent2D screen = g_vulkan.swapchain_extent;
    // Pick a font size ~ 1/12 of screen height — tweak as you like.
    const uint32_t px = choose_font_px_for_screen(screen, 1.0/12.0);

    FontAtlasCPU cpu{};
    if (!try_build_cpu_atlas_from_any_font(px, cpu)) {
        std::fprintf(stderr, "[text_render_hello] failed to build CPU atlas\n");
        platform_shutdown();
        return 1;
    }

    FontAtlasGPU gpu{};
    VK_CHECK(build_font_atlas_gpu(g_vulkan.device, g_vulkan.physical_device,
                                  g_vulkan.graphics_queue, g_vulkan.graphics_family,
                                  format, cpu, gpu));

    VkSampler sampler = VK_NULL_HANDLE;
    VK_CHECK(build_text_sampler(&sampler, filter, g_vulkan.device));

    // ----- Render targets, cmds, sync -----
    RenderTargets   rt;
    CommandResources cmd;
    FrameSync        sync;

    rt.init(g_vulkan.device, g_vulkan.swapchain_format, g_vulkan.swapchain_extent, g_vulkan.swapchain_image_views);
    cmd.init(g_vulkan.device, g_vulkan.graphics_family, static_cast<uint32_t>(rt.framebuffers.size()));
    sync.init(g_vulkan.device);

    // ----- Shaders -----
    VkShaderModule vs = make_shader(g_vulkan.device, EShLangVertex,   text_render_vs, "text_render_vs");
    VkShaderModule fs = make_shader(g_vulkan.device, EShLangFragment, text_render_fs, "text_render_fs");

    // ----- Text renderer -----
    TextRenderer text; // your class (VB-only under the hood)

    // Create with static viewport/scissor from your globals
    VK_CHECK(text.create(g_vulkan.device,
                         rt.render_pass,
                         vs, fs,
                         g_vulkan.viewport,
                         g_vulkan.scissor,
                         gpu.view,
                         sampler));

    // Pre-reserve for worst-case glyph count (2 triangles per glyph)
    constexpr std::string_view kMsg = "Hello, world!";
    VK_CHECK(text.reserve_instances(g_vulkan.device, g_vulkan.physical_device, /*instances*/ uint32_t(kMsg.size()*2)));

    // Pixel→NDC scale so RightTriangle positions are in clip space
    const float sx_ndc = 2.0f / float(screen.width);
    const float sy_ndc = -2.0f / float(screen.height); // minus: top-left pixel (0,0) → NDC (-1,+1)

    // Center horizontally, reasonable baseline vertically.
    const int text_w_px = measure_text_x_px(cpu, kMsg);
    const int line_h_px = measure_y_px(cpu);
    const float origin_x_px = 0.5f * (float(screen.width) - float(text_w_px));
    const float origin_y_px = 0.5f * float(screen.height) + 0.35f * float(line_h_px); // eyeballed baseline

    const float origin_x_ndc = -1.0f + sx_ndc * origin_x_px;
    const float origin_y_ndc = +1.0f + sy_ndc * origin_y_px;

    const float sx = sx_ndc;
    const float sy = sy_ndc;

    const float color[4] = {1, 1, 1, 1}; // white

    // ----- Render loop -----
    while (!platform_should_quit()) {
        VK_CHECK(vkWaitForFences(g_vulkan.device, 1, &sync.in_flight_fence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(g_vulkan.device, 1, &sync.in_flight_fence));

        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(g_vulkan.device, g_vulkan.swapchain, UINT64_MAX,
                                             sync.image_available, VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) break;
        VK_CHECK(acq);

        VkCommandBuffer cb = cmd.buffers[imageIndex];
        VK_CHECK(vkResetCommandBuffer(cb, 0));
        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(cb, &bi));

        VkClearValue clear{}; clear.color = {{0.06f, 0.06f, 0.09f, 1.0f}};
        auto rpbi = render::render_pass_begin_info(
            rt.render_pass, rt.framebuffers[imageIndex], g_vulkan.swapchain_extent, std::span{&clear,1});
        vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        // Draw the line
        VK_CHECK(text.record_draw_line(g_vulkan.device,
                                       g_vulkan.physical_device,
                                       cb,
                                       kMsg,
                                       origin_x_ndc, origin_y_ndc,  // pen origin in NDC
                                       sx, sy,                       // pixel->NDC scale
                                       cpu,
                                       color));

        vkCmdEndRenderPass(cb);
        VK_CHECK(vkEndCommandBuffer(cb));

        VK_CHECK(sync.submit_one(g_vulkan.graphics_queue, imageIndex, cmd));
        VkResult pres = sync.present_one(g_vulkan.present_queue, g_vulkan.swapchain, imageIndex);
        if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) break;
        VK_CHECK(pres);
    }

    // ----- Cleanup -----
    VK_CHECK(vkDeviceWaitIdle(g_vulkan.device));
    text.destroy(g_vulkan.device);
    if (vs) vkDestroyShaderModule(g_vulkan.device, vs, nullptr);
    if (fs) vkDestroyShaderModule(g_vulkan.device, fs, nullptr);
    if (sampler) vkDestroySampler(g_vulkan.device, sampler, nullptr);
    destroy_gpu_font_atlas(g_vulkan.device, gpu);
    sync.shutdown(g_vulkan.device);
    cmd.shutdown(g_vulkan.device);
    rt.shutdown(g_vulkan.device);
    platform_shutdown();

    std::fprintf(stdout, "[text_render_hello] OK\n");
    return 0;
}
