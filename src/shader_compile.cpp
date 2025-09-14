#include "shader_compile.hpp"
#include "platform.hpp"   // your project’s place that includes <vulkan/vulkan.h> and VK_CHECK

#include <stdexcept>
#include <cstdio>

// Some builds don’t bring this extern in via the header on all platforms.
// Declare it here; it’s defined inside glslang and linked via glslang-default-resource-limits.
extern TBuiltInResource DefaultTBuiltInResource;

namespace shader {
CompileResult compile_glsl_to_spirv(EShLanguage stage,
                                    std::string_view source,
                                    const Options& opt,
                                    std::string_view debugName)
{
    CompileResult r{};

    const char* src_ptr = source.data();

    glslang::TShader shader(stage);
    shader.setStrings(&src_ptr, 1);
    shader.setEntryPoint(opt.entry);
    shader.setSourceEntryPoint(opt.entry);

    // Target Vulkan + SPIR-V (client semantics version for Vulkan is typically 100)
    shader.setEnvInput(glslang::EShSourceGlsl, stage, glslang::EShClientVulkan, 100);
    shader.setEnvClient(glslang::EShClientVulkan, opt.vulkanTarget);
    shader.setEnvTarget(glslang::EShTargetSpv,    opt.spirvTarget);

    if (!shader.parse(&DefaultTBuiltInResource,
                      opt.glslVersion,
                      opt.forwardCompatible,
                      opt.messages))
    {
        r.ok = false;
        r.log = std::string("GLSL parse failed [") + std::string(debugName) + "]:\n" +
                shader.getInfoLog() + shader.getInfoDebugLog();
        return r;
    }

    glslang::TProgram prog;
    prog.addShader(&shader);

    if (!prog.link(opt.messages)) {
        r.ok = false;
        r.log = std::string("GLSL link failed [") + std::string(debugName) + "]:\n" +
                prog.getInfoLog() + prog.getInfoDebugLog();
        return r;
    }

    glslang::TIntermediate* ir = prog.getIntermediate(stage);
    if (!ir) {
        r.ok = false;
        r.log = "No intermediate representation generated.";
        return r;
    }

    // GlslangToSpv expects a non-const SpvOptions*
    glslang::SpvOptions spv_opts = opt.spv;

    r.spirv.clear();
    glslang::GlslangToSpv(*ir, r.spirv, &spv_opts);  // optimizer/validator honored if built-in
    r.ok = true;
    return r;
}

#if defined(VK_VERSION_1_0) || defined(VULKAN_H_) || defined(VK_CORE_H_)
VkShaderModule make_shader_module(VkDevice device, const std::vector<uint32_t>& words) {
    VkShaderModuleCreateInfo ci{};
    ci.sType    = VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO;
    ci.codeSize = words.size() * sizeof(uint32_t);
    ci.pCode    = words.data();

    VkShaderModule mod{};
    VK_CHECK(vkCreateShaderModule(device, &ci, nullptr, &mod));
    return mod;
}
#endif

} // namespace shader
