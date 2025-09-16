// tests/text_atlas_hello.cpp
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>

#include "platform.hpp"
#include "render.hpp"
#include "render_pipeline.hpp"
#include "shader_compile.hpp"
#include "text_format_caps.hpp"
#include "text_atlas.hpp"
#include "text_render.hpp"

// --- Fullscreen textured triangle shaders (no vertex buffers) ---
static constexpr const char* kVS = R"GLSL(
#version 450
layout(location=0) out vec2 vUV;
void main() {
    // Fullscreen triangle (2x2 UVs so we see the full atlas)
    const vec2 pos[3] = vec2[3](vec2(-1.0,-1.0), vec2( 3.0,-1.0), vec2(-1.0, 3.0));
    const vec2 uv [3] = vec2[3](vec2( 0.0, 0.0), vec2( 2.0, 0.0), vec2( 0.0, 2.0));
    gl_Position = vec4(pos[gl_VertexIndex], 0.0, 1.0);
    vUV = uv[gl_VertexIndex];
}
)GLSL";

static constexpr const char* kFS = R"GLSL(
#version 450
layout(set=0,binding=0) uniform sampler2D uAtlas;
layout(location=0) in  vec2 vUV;
layout(location=0) out vec4 oColor;
void main() {
    // Our atlas is R8 (or RGBA8 fallback). Use .r and show as grayscale.
    float r = texture(uAtlas, vUV).r;
    oColor = vec4(r, r, r, 1.0);
}
)GLSL";

// if we for some reason dont have the font assets in our directory
// we dont want this test to fail we want it to show something is off by printing diffrent usage
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
            std::fprintf(stdout, "[text_atlas_hello] Using font: %s\n", path);
            return true;
        }
    }
    return false;
}

int main(int argc, char** argv) {
    (void)argc; (void)argv;

    // 1) Init platform (normal windowed init is fine)
    if (!platform_init(/* your typical args, if any */)) {
        std::fprintf(stderr, "[text_atlas_hello] platform_init failed\n");
        return 1;
    }

    // 2) Build the atlas (CPU -> GPU)
    VkFormat format;
    VkFilter filter;
    if (!pick_text_format_and_filter(g_vulkan.physical_device, format,filter)) {
        std::fprintf(stderr, "[text_atlas_hello] No suitable format\n");
        platform_shutdown();
        return 1;
    }

    VkExtent2D screen = g_vulkan.swapchain_extent;
    uint32_t px = choose_font_px_for_screen(screen,1/10.0);

    FontAtlasCPU cpu{};
    if (!try_build_cpu_atlas_from_any_font(px, cpu)) {
        std::fprintf(stderr, "[text_atlas_hello] FreeType failed\n");
        platform_shutdown();
        return 1;
    }

    FontAtlasGPU gpu{};
    VK_CHECK(build_font_atlas_gpu(g_vulkan.device, g_vulkan.physical_device,
                                  g_vulkan.graphics_queue, g_vulkan.graphics_family,
                                  format,
                                  cpu, gpu));

    VkSampler sampler = VK_NULL_HANDLE;
    VK_CHECK(build_text_sampler(&sampler,filter,g_vulkan.device));

    // 3) Render targets + command buffers + sync
    RenderTargets   rt;
    CommandResources cmd;
    FrameSync        sync;
    rt.init(g_vulkan.device, g_vulkan.swapchain_format,
            g_vulkan.swapchain_extent, g_vulkan.swapchain_image_views);
    cmd.init(g_vulkan.device, g_vulkan.graphics_family,
             static_cast<uint32_t>(rt.framebuffers.size()));
    sync.init(g_vulkan.device);

    // 4) Descriptor set (combined image sampler)
    VkDescriptorSetLayout dsl = VK_NULL_HANDLE;
    VkDescriptorPool       dp  = VK_NULL_HANDLE;
    VkDescriptorSet        ds  = VK_NULL_HANDLE;

    {
        auto binding  = render::desc_binding(0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1,
                                     VK_SHADER_STAGE_FRAGMENT_BIT);
        auto dslci    = render::desc_layout_info({&binding, 1});
        VK_CHECK(vkCreateDescriptorSetLayout(g_vulkan.device, &dslci, nullptr, &dsl));

        auto ps       = render::desc_pool_size(VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1);
        auto dpci     = render::desc_pool_info({&ps, 1}, /*max_sets=*/1);
        VK_CHECK(vkCreateDescriptorPool(g_vulkan.device, &dpci, nullptr, &dp));

        auto dsai     = render::desc_alloc_info(dp, {&dsl, 1});
        VK_CHECK(vkAllocateDescriptorSets(g_vulkan.device, &dsai, &ds));

        auto di       = render::desc_image_info(sampler, gpu.view);
        auto w        = render::desc_write_image(ds, 0, &di);
        vkUpdateDescriptorSets(g_vulkan.device, 1, &w, 0, nullptr);
    }


    // 5) Pipeline layout + shaders + pipeline
    VkPipelineLayout pl = VK_NULL_HANDLE;
    VkPipeline        gp = VK_NULL_HANDLE;
    VkShaderModule    vs = VK_NULL_HANDLE;
    VkShaderModule    fs = VK_NULL_HANDLE;

    {
        // Layout
        VkPipelineLayoutCreateInfo plci = render::layout_info(std::span{&dsl,1});
        VK_CHECK(vkCreatePipelineLayout(g_vulkan.device, &plci, nullptr, &pl));

        // Compile shaders
        shader::Options opt;
        auto vres = shader::compile_glsl_to_spirv(EShLangVertex,   kVS, opt, "atlas_fullscreen.vert");
        auto fres = shader::compile_glsl_to_spirv(EShLangFragment, kFS, opt, "atlas_fullscreen.frag");
        if (!vres.ok) { std::fprintf(stderr, "VS compile failed:\n%s\n", vres.log.c_str()); std::abort(); }
        if (!fres.ok) { std::fprintf(stderr, "FS compile failed:\n%s\n", fres.log.c_str()); std::abort(); }
        vs = shader::make_shader_module(g_vulkan.device, vres.spirv);
        fs = shader::make_shader_module(g_vulkan.device, fres.spirv);

        auto stages = render::fragment_vertex_stage_info(fs, vs);

        auto vpst = render::viewport_state_info_static(
            { &g_vulkan.viewport, 1 },
            { &g_vulkan.scissor, 1 }
        );

        //pipline
        const VkPipelineColorBlendAttachmentState blend = render::no_blend; // or alpha_blend
        VK_CHECK(render::create_graphics_pipeline_basic(
            /*outPipe*/ gp,
            /*device*/  g_vulkan.device,
            /*stages*/  stages,
            /*viewport*/&vpst,
            /*layout*/  pl,
            /*rp*/      rt.render_pass,
            /*cull*/    VK_CULL_MODE_NONE,
            /*blend*/   blend
        ));
    }


    // 6) Render loop: draw the atlas on screen
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

        VkClearValue clear{};
        clear.color = {{0.05f, 0.05f, 0.08f, 1.0f}};

        auto rpbi = render::render_pass_begin_info(
            rt.render_pass,
            rt.framebuffers[imageIndex],
            g_vulkan.swapchain_extent,
            std::span{&clear, 1},
            VkOffset2D{0,0}
        );
        vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gp);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl,
                                0, 1, &ds,
                                0, nullptr);

        vkCmdDraw(cb, 3, 1, 0, 0);

        vkCmdEndRenderPass(cb);
        VK_CHECK(vkEndCommandBuffer(cb));

        VK_CHECK(sync.submit_one(g_vulkan.graphics_queue, imageIndex, cmd));
        VkResult pres = sync.present_one(g_vulkan.present_queue, g_vulkan.swapchain, imageIndex);
        if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) break;
        VK_CHECK(pres);
    }


    // 7) Cleanup
    VK_CHECK(vkDeviceWaitIdle(g_vulkan.device));
    if (gp) vkDestroyPipeline(g_vulkan.device, gp, nullptr);
    if (pl) vkDestroyPipelineLayout(g_vulkan.device, pl, nullptr);
    if (vs) vkDestroyShaderModule(g_vulkan.device, vs, nullptr);
    if (fs) vkDestroyShaderModule(g_vulkan.device, fs, nullptr);
    if (dp) vkDestroyDescriptorPool(g_vulkan.device, dp, nullptr);
    if (dsl) vkDestroyDescriptorSetLayout(g_vulkan.device, dsl, nullptr);
    if (sampler) vkDestroySampler(g_vulkan.device,sampler,nullptr);

    destroy_gpu_font_atlas(g_vulkan.device, gpu);
    sync.shutdown(g_vulkan.device);
    cmd.shutdown(g_vulkan.device);
    rt.shutdown(g_vulkan.device);
    platform_shutdown();

    std::fprintf(stdout, "[text_atlas_hello] OK\n");
    return 0;
}
