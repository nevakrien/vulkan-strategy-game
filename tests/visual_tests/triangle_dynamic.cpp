// tests/visual_tests/visual_triangle_cli.cpp
#include <cstdio>
#include <cstring>
#include <vector>
#include <cmath>
#include <optional>
#include "render.hpp"
#include "shader_compile.hpp"
#include "render_pipeline.hpp"

// --- GLSL --------------------------------------------------------------------
static constexpr const char* kVS = R"GLSL(
#version 450
layout(push_constant) uniform PC { float t; } pc;
void main() {
    vec2 p = (gl_VertexIndex==0)? vec2( 0.0,  0.7)
            : (gl_VertexIndex==1)? vec2(-0.7, -0.7)
                                  : vec2( 0.7, -0.7);
    float c = cos(pc.t), s = sin(pc.t);
    p = mat2(c,-s,s,c) * p;
    gl_Position = vec4(p,0.0,1.0);
}
)GLSL";

static constexpr const char* kFS = R"GLSL(
#version 450
layout(push_constant) uniform PC { float t; } pc;
layout(location=0) out vec4 outColor;
void main() {
    float r = 0.5 + 0.5 * sin(pc.t + 0.0);
    float g = 0.5 + 0.5 * sin(pc.t + 2.09439510239);
    float b = 0.5 + 0.5 * sin(pc.t + 4.18879020479);
    outColor = vec4(r,g,b,1.0);
}
)GLSL";

// --- Arg parsing -------------------------------------------------------------
static std::optional<glslang::EShTargetClientVersion> parse_vk(const char* s) {
    if (!s) return std::nullopt;
    if (!std::strcmp(s,"1.0")) return glslang::EShTargetVulkan_1_0;
    if (!std::strcmp(s,"1.1")) return glslang::EShTargetVulkan_1_1;
    if (!std::strcmp(s,"1.2")) return glslang::EShTargetVulkan_1_2;
    if (!std::strcmp(s,"1.3")) return glslang::EShTargetVulkan_1_3;
    std::fprintf(stderr, "Unknown --vk %s (use 1.0/1.1/1.2/1.3)\n", s);
    return std::nullopt;
}

static std::optional<glslang::EShTargetLanguageVersion> parse_spv(const char* s) {
    if (!s) return std::nullopt;
    if (!std::strcmp(s,"1.0")) return glslang::EShTargetSpv_1_0;
    if (!std::strcmp(s,"1.3")) return glslang::EShTargetSpv_1_3;
    if (!std::strcmp(s,"1.5")) return glslang::EShTargetSpv_1_5;
    if (!std::strcmp(s,"1.6")) return glslang::EShTargetSpv_1_6;
    std::fprintf(stderr, "Unknown --spv %s (use 1.0/1.3/1.5/1.6)\n", s);
    return std::nullopt;
}

// Clamp SPIR-V to a Vulkan semantic version
static glslang::EShTargetLanguageVersion clamp_spv(glslang::EShTargetClientVersion vk,
                                                    glslang::EShTargetLanguageVersion spv) {
    // Vulkan 1.0 → SPIR-V 1.0; 1.1 → 1.3; 1.2 → 1.5; 1.3 → 1.6
    glslang::EShTargetLanguageVersion max_for_vk = glslang::EShTargetSpv_1_0;
    switch (vk) {
        case glslang::EShTargetVulkan_1_1: max_for_vk = glslang::EShTargetSpv_1_3; break;
        case glslang::EShTargetVulkan_1_2: max_for_vk = glslang::EShTargetSpv_1_5; break;
        case glslang::EShTargetVulkan_1_3: max_for_vk = glslang::EShTargetSpv_1_6; break;
        default: break;
    }
    if (spv > max_for_vk) {
        std::fprintf(stderr, "[triangle-cli] Note: SPIR-V target downgraded to match Vulkan semantics.\n");
        return max_for_vk;
    }
    return spv;
}

// --- Pipeline helper --------------------------------------------------------
static VkShaderModule make_shader(VkDevice dev, EShLanguage stage, std::string_view src,
                                  const char* dbg, const shader::Options& opt) {
    auto res = shader::compile_glsl_to_spirv(stage, src, opt, dbg);
    if (!res.ok) { std::fprintf(stderr, "Shader compile failed for %s:\n%s\n", dbg, res.log.c_str()); std::abort(); }
    return shader::make_shader_module(dev, res.spirv);
}

// --- Run test ----------------------------------------------------------------
static int run_visual_triangle_with_opts(const shader::Options& opt) {
    RenderTargets rt;
    CommandResources cmds;
    FrameSync sync;

    rt.init(g_vulkan.device, g_vulkan.swapchain_format, g_vulkan.swapchain_extent, g_vulkan.swapchain_image_views);
    cmds.init(g_vulkan.device, g_vulkan.graphics_family, static_cast<uint32_t>(rt.framebuffers.size()));
    sync.init(g_vulkan.device);

    // compile shaders
    VkShaderModule vs = make_shader(g_vulkan.device, EShLangVertex,   kVS, "triangle.vert", opt);
    VkShaderModule fs = make_shader(g_vulkan.device, EShLangFragment, kFS, "triangle.frag", opt);

    // pipeline layout (push-constant: float t)
    std::vector<VkPushConstantRange> ranges = {
        render::float_constant(VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT)
    };
    auto plci = render::layout_info({}, ranges);
    VkPipelineLayout pl = VK_NULL_HANDLE;
    VK_CHECK(vkCreatePipelineLayout(g_vulkan.device, &plci, nullptr, &pl));

    auto vpst = render::viewport_state_info_dynamic(1);

    // dynamic viewport/scissor
    VkDynamicState dynStates[] = { VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR };
    auto dyn = render::dynamic_state_info(dynStates);

    // shader stages
    auto stages = render::fragment_vertex_stage_info(fs, vs);

    // create pipeline via the new helper in `render`
    VkPipeline gp = VK_NULL_HANDLE;
    VK_CHECK(render::create_graphics_pipeline_basic(
        /*outPipe=*/gp,
        /*device=*/g_vulkan.device,
        /*stages=*/stages,
        /*viewport_state=*/&vpst,
        /*layout=*/pl,
        /*render_pass=*/rt.render_pass,
        /*cull=*/VK_CULL_MODE_NONE,     // keep old behavior; omit to use 3D defaults
        /*blend=*/render::no_blend,     // keep old behavior; omit to use alpha blend
        /*subpass=*/0,
        /*dynamic_state=*/&dyn
    ));

    const double t0 = SDL_GetTicks() / 1000.0;
    while (!platform_should_quit()) {
        VK_CHECK(vkWaitForFences(g_vulkan.device, 1, &sync.in_flight_fence, VK_TRUE, UINT64_MAX));
        VK_CHECK(vkResetFences(g_vulkan.device, 1, &sync.in_flight_fence));

        uint32_t image_index = 0;
        VkResult acq = vkAcquireNextImageKHR(g_vulkan.device, g_vulkan.swapchain, UINT64_MAX, sync.image_available, VK_NULL_HANDLE, &image_index);
        if (acq == VK_ERROR_OUT_OF_DATE_KHR) break;
        VK_CHECK(acq);

        float t = float(SDL_GetTicks()/1000.0 - t0);

        VkCommandBuffer cb = cmds.buffers[image_index];
        VK_CHECK(vkResetCommandBuffer(cb, 0));

        VkCommandBufferBeginInfo bi{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
        VK_CHECK(vkBeginCommandBuffer(cb, &bi));

        VkClearValue clear; clear.color = {{0.02f,0.02f,0.02f,1.0f}};
        VkClearValue clears[1]={}; clears[0].color = {{0.02f,0.02f,0.02f,1.0f}};
        auto rpbi = render::render_pass_begin_info(
            rt.render_pass,
            rt.framebuffers[image_index],
            g_vulkan.swapchain_extent,
            clears
        );
        vkCmdBeginRenderPass(cb, &rpbi, VK_SUBPASS_CONTENTS_INLINE);

        vkCmdBindPipeline(cb, VK_PIPELINE_BIND_POINT_GRAPHICS, gp);
        vkCmdSetViewport(cb, 0, 1, &g_vulkan.viewport);
        vkCmdSetScissor (cb, 0, 1, &g_vulkan.scissor);
        vkCmdPushConstants(cb, pl, VK_SHADER_STAGE_VERTEX_BIT|VK_SHADER_STAGE_FRAGMENT_BIT, 0, sizeof(float), &t);
        vkCmdDraw(cb, 3, 1, 0, 0);

        vkCmdEndRenderPass(cb);
        VK_CHECK(vkEndCommandBuffer(cb));

        VK_CHECK(sync.submit_one(g_vulkan.graphics_queue, image_index, cmds));
        VkResult pres = sync.present_one(g_vulkan.present_queue, g_vulkan.swapchain, image_index);
        if (pres == VK_ERROR_OUT_OF_DATE_KHR || pres == VK_SUBOPTIMAL_KHR) break;
        VK_CHECK(pres);
    }

    VK_CHECK(vkDeviceWaitIdle(g_vulkan.device));
    if (gp) vkDestroyPipeline(g_vulkan.device, gp, nullptr);
    if (pl) vkDestroyPipelineLayout(g_vulkan.device, pl, nullptr);
    if (vs) vkDestroyShaderModule(g_vulkan.device, vs, nullptr);
    if (fs) vkDestroyShaderModule(g_vulkan.device, fs, nullptr);

    sync.shutdown(g_vulkan.device);
    cmds.shutdown(g_vulkan.device);
    rt.shutdown(g_vulkan.device);
    return 0;
}

// --- main: parse optional --vk / --spv --------------------------------------
int main(int argc, char** argv) {

    shader::Options opt; // starts with your engine defaults (1.0/1.0)

    // Parse optional args
    for (int i = 1; i < argc; ++i) {
        if (!std::strcmp(argv[i], "--vk") && i+1 < argc) {
            if (auto v = parse_vk(argv[i+1])) { opt.vulkanTarget = *v; }
            ++i;
        } else if (!std::strcmp(argv[i], "--spv") && i+1 < argc) {
            if (auto s = parse_spv(argv[i+1])) { opt.spirvTarget = *s; }
            ++i;
        } else if (!std::strncmp(argv[i], "--vk=", 5)) {
            auto v = parse_vk(argv[i]+5); if (v) opt.vulkanTarget = *v;
        } else if (!std::strncmp(argv[i], "--spv=", 6)) {
            auto s = parse_spv(argv[i]+6); if (s) opt.spirvTarget = *s;
        } else {
            std::fprintf(stderr, "Usage: %s [--vk 1.0|1.1|1.2|1.3] [--spv 1.0|1.3|1.5|1.6]\n", argv[0]);
        }
    }

    if (!platform_init(vk_client_convert(opt.vulkanTarget))) return 1;
    // if (!platform_init()) return 1;



    // Safety: clamp SPIR-V to Vulkan semantics (only when user requested values)
    opt.spirvTarget = clamp_spv(opt.vulkanTarget, opt.spirvTarget);

    int rc = run_visual_triangle_with_opts(opt);
    platform_shutdown();
    return rc;
}
