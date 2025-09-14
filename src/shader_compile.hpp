#ifndef MYGAME_SHADER_COMPILE_HPP
#define MYGAME_SHADER_COMPILE_HPP

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

// glslang
#include <glslang/Public/ShaderLang.h>
#include <glslang/Include/ResourceLimits.h>   // TBuiltInResource, DefaultTBuiltInResource (extern)
#include <SPIRV/GlslangToSpv.h>               // GlslangToSpv, glslang::SpvOptions
#include <vulkan/vulkan.h>

namespace shader {

struct CompileResult {
    bool ok = false;
    std::vector<uint32_t> spirv;
    glslang::SpvOptions spv_opts;
    std::string log;
};

// All knobs, no double-wrapping. Defaults depend on NDEBUG.
struct Options {
    int glslVersion = 450;

    glslang::EShTargetClientVersion   vulkanTarget = glslang::EShTargetVulkan_1_0;
    glslang::EShTargetLanguageVersion spirvTarget  = glslang::EShTargetSpv_1_0;

    bool        forwardCompatible = false;
    EShMessages messages = static_cast<EShMessages>(EShMsgDefault | EShMsgSpvRules | EShMsgVulkanRules);
    const char* entry    = "main";

    glslang::SpvOptions spv{};   // passed straight to GlslangToSpv

    Options() {
        spv.validate = true;
    #ifdef NDEBUG
        spv.disableOptimizer = false; // use optimizer if ENABLE_OPT=ON in your glslang build
        spv.stripDebugInfo   = true;
        spv.generateDebugInfo= false;
    #else
        spv.disableOptimizer = true;  // faster iteration in Debug
        spv.stripDebugInfo   = false;
        spv.generateDebugInfo= true;
    #endif
    }
};

// Compile GLSL source → SPIR-V (kept in memory).
CompileResult compile_glsl_to_spirv(EShLanguage stage,
                                    std::string_view source,
                                    const Options& opt = Options(),
                                    std::string_view debugName = "shader.glsl");


// If the includer has already included Vulkan (normal headers), expose the helper:
#if defined(VK_VERSION_1_0) || defined(VULKAN_H_) || defined(VK_CORE_H_)
VkShaderModule make_shader_module(VkDevice device, const std::vector<uint32_t>& words);
#endif

} // namespace shader

static inline int vk_client_number(glslang::EShTargetClientVersion v) {
    switch (v) {
        case glslang::EShTargetVulkan_1_1: return VK_API_VERSION_1_1;
        case glslang::EShTargetVulkan_1_2: return VK_API_VERSION_1_2;
        case glslang::EShTargetVulkan_1_3: return VK_API_VERSION_1_3;
        default:                            return VK_API_VERSION_1_0;
    }
}

#endif // MYGAME_SHADER_COMPILE_HPP
