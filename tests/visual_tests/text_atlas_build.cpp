// tests/text_atlas_hello.cpp
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <cstring>

#include "platform.hpp"          // g_vulkan, platform_init/platform_shutdown, extern FT_Library free_type
#include "render.hpp"            // your Vk helper builders + VK_CHECK
#include "render_pipeline.hpp"   // render::fragment_vertex_stage_info(...)
#include "shader_compile.hpp"    // shader::compile_glsl_to_spirv / shader::make_shader_module
#include "text_format_caps.hpp"  // pick_text_caps(...)
#include "text_atlas.hpp"        // build_cpu_font_atlas(...), build_font_atlas_gpu(...), destroy_gpu_font_atlas(...)

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
    TextFormatCaps caps{};
    if (!pick_text_caps(g_vulkan.physical_device, caps)) {
        std::fprintf(stderr, "[text_atlas_hello] No suitable format\n");
        platform_shutdown();
        return 1;
    }

    VkExtent2D screen = g_vulkan.swapchain_extent.width ? g_vulkan.swapchain_extent
                                                        : VkExtent2D{1280, 720};
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
                                  caps.format, caps.filter,
                                  cpu, gpu));

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
        VkDescriptorSetLayoutBinding b{};
        b.binding = 0;
        b.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        b.descriptorCount = 1;
        b.stageFlags = VK_SHADER_STAGE_FRAGMENT_BIT;
        b.pImmutableSamplers = nullptr;

        VkDescriptorSetLayoutCreateInfo dslci{};
        dslci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
        dslci.bindingCount = 1;
        dslci.pBindings = &b;
        VK_CHECK(vkCreateDescriptorSetLayout(g_vulkan.device, &dslci, nullptr, &dsl));

        VkDescriptorPoolSize ps{};
        ps.type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        ps.descriptorCount = 1;

        VkDescriptorPoolCreateInfo dpci{};
        dpci.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
        dpci.flags = 0;
        dpci.maxSets = 1;
        dpci.poolSizeCount = 1;
        dpci.pPoolSizes = &ps;
        VK_CHECK(vkCreateDescriptorPool(g_vulkan.device, &dpci, nullptr, &dp));

        VkDescriptorSetAllocateInfo dsai{};
        dsai.sType = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
        dsai.descriptorPool = dp;
        dsai.descriptorSetCount = 1;
        dsai.pSetLayouts = &dsl;
        VK_CHECK(vkAllocateDescriptorSets(g_vulkan.device, &dsai, &ds));

        VkDescriptorImageInfo di{};
        di.sampler = gpu.sampler;
        di.imageView = gpu.view;
        di.imageLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;

        VkWriteDescriptorSet w{};
        w.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
        w.dstSet = ds;
        w.dstBinding = 0;
        w.dstArrayElement = 0;
        w.descriptorCount = 1;
        w.descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER;
        w.pImageInfo = &di;

        vkUpdateDescriptorSets(g_vulkan.device, 1, &w, 0, nullptr);
    }

    // 5) Pipeline layout + shaders + pipeline
    VkPipelineLayout pl = VK_NULL_HANDLE;
    VkPipeline        gp = VK_NULL_HANDLE;
    VkShaderModule    vs = VK_NULL_HANDLE;
    VkShaderModule    fs = VK_NULL_HANDLE;

    {
        // Layout
        VkPipelineLayoutCreateInfo plci{};
        plci.sType = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
        plci.setLayoutCount = 1;
        plci.pSetLayouts = &dsl;
        plci.pushConstantRangeCount = 0;
        VK_CHECK(vkCreatePipelineLayout(g_vulkan.device, &plci, nullptr, &pl));

        // Compile shaders
        shader::Options opt; // your engine defaults (targets)
        auto vres = shader::compile_glsl_to_spirv(EShLangVertex,   kVS, opt, "atlas_fullscreen.vert");
        auto fres = shader::compile_glsl_to_spirv(EShLangFragment, kFS, opt, "atlas_fullscreen.frag");
        if (!vres.ok) { std::fprintf(stderr, "VS compile failed:\n%s\n", vres.log.c_str()); std::abort(); }
        if (!fres.ok) { std::fprintf(stderr, "FS compile failed:\n%s\n", fres.log.c_str()); std::abort(); }
        vs = shader::make_shader_module(g_vulkan.device, vres.spirv);
        fs = shader::make_shader_module(g_vulkan.device, fres.spirv);

        auto stages = render::fragment_vertex_stage_info(fs, vs);

        // Fixed-function blocks
        VkViewport vp{};
        vp.x = 0.0f; vp.y = 0.0f;
        vp.width  = float(g_vulkan.swapchain_extent.width);
        vp.height = float(g_vulkan.swapchain_extent.height);
        vp.minDepth = 0.0f; vp.maxDepth = 1.0f;

        VkRect2D sc{};
        sc.offset = {0, 0};
        sc.extent = g_vulkan.swapchain_extent;

        auto vi   = render::vertex_input_info();
        auto ia   = render::input_assembly_info();
        auto vpst = render::viewport_state_info_static({&vp, 1}, {&sc, 1}); // <-- static
        auto rs   = render::rasterization_state_info(VK_CULL_MODE_NONE);
        auto ms   = render::multisample_state_info();

        VkPipelineColorBlendAttachmentState cbAttach[] = { render::no_blend };
        auto cb = render::color_blend_state(cbAttach);

        VkGraphicsPipelineCreateInfo gpci =
            render::graphics_pipeline_info(
                stages,
                &vi, &ia, &vpst, &rs, &ms, &cb,
                pl, rt.render_pass,
                /*subpass*/0
            );

        VK_CHECK(vkCreateGraphicsPipelines(g_vulkan.device, VK_NULL_HANDLE, 1, &gpci, nullptr, &gp));
    }

    // 6) Render loop: draw the atlas on screen
    const double t0 = SDL_GetTicks() / 1000.0;
    (void)t0;
    while (!platform_should_quit()) {
        // fence wait/reset
        VK_CHECK(vkWaitForFences(g_vulkan.device, 1, &sync.in_flight_fence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(g_vulkan.device, 1, &sync.in_flight_fence));

        // acquire
        uint32_t imageIndex = 0;
        VkResult acq = vkAcquireNextImageKHR(g_vulkan.device, g_vulkan.swapchain, UINT64_MAX,
                                             sync.image_available, VK_NULL_HANDLE, &imageIndex);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) break;
        VK_CHECK(acq);

        // record
        VkCommandBuffer cb = cmd.buffers[imageIndex];
        VK_CHECK(vkResetCommandBuffer(cb, 0));

        VkCommandBufferBeginInfo bi{};
        bi.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO;
        VK_CHECK(vkBeginCommandBuffer(cb, &bi));

        VkClearValue clear{};
        clear.color = {{0.05f, 0.05f, 0.08f, 1.0f}};

        VkRenderPassBeginInfo rpbi{};
        rpbi.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO;
        rpbi.renderPass = rt.render_pass;
        rpbi.framebuffer = rt.framebuffers[imageIndex];
        rpbi.renderArea.offset = {0,0};
        rpbi.renderArea.extent = g_vulkan.swapchain_extent;
        rpbi.clearValueCount = 1;
        rpbi.pClearValues = &clear;
        vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        // viewport/scissor
        VkViewport vp{};
        vp.x = 0.0f; vp.y = 0.0f;
        vp.width  = float(g_vulkan.swapchain_extent.width);
        vp.height = float(g_vulkan.swapchain_extent.height);
        vp.minDepth = 0.0f; vp.maxDepth = 1.0f;

        VkRect2D sc{};
        sc.offset = {0,0};
        sc.extent = g_vulkan.swapchain_extent;

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gp);
        vkCmdBindDescriptorSets(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, pl,
                                /*firstSet=*/0, /*descriptorSetCount=*/1, &ds,
                                /*dynamicOffsetCount=*/0, /*pDynamicOffsets=*/nullptr);

        vkCmdDraw(cb, /*vertexCount=*/3, /*instanceCount=*/1, /*firstVertex=*/0, /*firstInstance=*/0);

        vkCmdEndRenderPass(cb);
        VK_CHECK(vkEndCommandBuffer(cb));

        // submit + present
        VK_CHECK(sync.submit_one(g_vulkan.graphics_queue, imageIndex, cmd));
        VkResult pres = sync.present_one(g_vulkan.present_queue, g_vulkan.swapchain, imageIndex);
        if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) break;
        VK_CHECK(pres);

        // for a smoke test, you could exit after 60 frames:
        // if (SDL_GetTicks() > 1000 + 60*16) break;
    }

    // 7) Cleanup
    VK_CHECK(vkDeviceWaitIdle(g_vulkan.device));
    if (gp) vkDestroyPipeline(g_vulkan.device, gp, nullptr);
    if (pl) vkDestroyPipelineLayout(g_vulkan.device, pl, nullptr);
    if (vs) vkDestroyShaderModule(g_vulkan.device, vs, nullptr);
    if (fs) vkDestroyShaderModule(g_vulkan.device, fs, nullptr);
    if (dp) vkDestroyDescriptorPool(g_vulkan.device, dp, nullptr);
    if (dsl) vkDestroyDescriptorSetLayout(g_vulkan.device, dsl, nullptr);

    destroy_gpu_font_atlas(g_vulkan.device, gpu);
    sync.shutdown(g_vulkan.device);
    cmd.shutdown(g_vulkan.device);
    rt.shutdown(g_vulkan.device);
    platform_shutdown();

    std::fprintf(stdout, "[text_atlas_hello] OK\n");
    return 0;
}
