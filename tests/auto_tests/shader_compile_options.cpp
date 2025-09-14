// tests/shader_options_test.cpp
#include <cstdio>
#include <vector>
#include <string>
#include "shader_compile.hpp"

// Minimal shader that compiles for any target
static constexpr const char* kVS = R"GLSL(
#version 450
void main() {
    const vec2 P[3] = vec2[3](vec2(0,0.7), vec2(-0.7,-0.7), vec2(0.7,-0.7));
    gl_Position = vec4(P[gl_VertexIndex % 3], 0.0, 1.0);
}
)GLSL";

static void run_case(const char* label,
                     glslang::EShTargetClientVersion vkTarget,
                     glslang::EShTargetLanguageVersion spvTarget)
{
    shader::Options opt; // defaults to Vulkan 1.0 / SPIR-V 1.0
    opt.vulkanTarget = vkTarget;
    opt.spirvTarget  = spvTarget;

    auto res = shader::compile_glsl_to_spirv(EShLangVertex, kVS, opt, label);
    if (!res.ok) {
        std::fprintf(stderr, "[%s] FAIL\n%s\n", label, res.log.c_str());
    } else {
        std::fprintf(stdout, "[%s] OK  (words=%zu)\n", label, res.spirv.size());
    }
}

int main() {
    // Always compile with your DEFAULTS first (Vulkan 1.0 / SPIR-V 1.0)
    {
        shader::Options def; // your defaults
        auto res = shader::compile_glsl_to_spirv(EShLangVertex, kVS, def, "vulkan1.0-spv1.0 (defaults)");
        if (!res.ok) {
            std::fprintf(stderr, "[defaults] FAIL\n%s\n", res.log.c_str());
            return 1; // if defaults fail, something is truly wrong
        } else {
            std::fprintf(stdout, "[defaults] OK  (words=%zu)\n", res.spirv.size());
        }
    }

    // Additional option sets (compile-only; no Vulkan device used)
    run_case("vulkan1.1-spv1.3",
             glslang::EShTargetVulkan_1_1,
             glslang::EShTargetSpv_1_3);

    run_case("vulkan1.2-spv1.5",
             glslang::EShTargetVulkan_1_2,
             glslang::EShTargetSpv_1_5);

    run_case("vulkan1.3-spv1.6",
             glslang::EShTargetVulkan_1_3,
             glslang::EShTargetSpv_1_6);

    // Don’t fail the whole test if advanced targets aren’t supported in your glslang build:
    // Success of the defaults is the pass condition.
    return 0;
}
