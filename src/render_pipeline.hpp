#ifndef RENDER_PIPELINE_HPP
#define RENDER_PIPELINE_HPP

#include <cstdint>
#include <string>
#include <string_view>

#include <array> 
#include <span>
#include <optional>

#include <vulkan/vulkan.h>
#include "common.hpp"

namespace render {
inline constexpr VkPipelineLayoutCreateInfo layout_info(
    std::span<const VkDescriptorSetLayout> descriptors = {},
    std::span<const VkPushConstantRange>  constants   = {}
){
    VkPipelineLayoutCreateInfo info{};
    info.sType                  = VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO;
    info.pNext                  = nullptr;
    info.flags                  = 0;
    info.setLayoutCount         = static_cast<uint32_t>(descriptors.size());
    info.pSetLayouts            = descriptors.data();
    info.pushConstantRangeCount = static_cast<uint32_t>(constants.size());
    info.pPushConstantRanges    = constants.data();
    return info; // points into the provided vectors; keep them alive!
}

inline constexpr VkPushConstantRange float_constant(VkShaderStageFlags stageFlags){
    VkPushConstantRange r{};
    r.stageFlags = stageFlags;
    r.offset     = 0;
    r.size       = sizeof(float);
    return r;
}

inline std::array<VkPipelineShaderStageCreateInfo, 2>
fragment_vertex_stage_info(VkShaderModule fs,VkShaderModule vs)
{
    VkPipelineShaderStageCreateInfo vsStage{
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext  = nullptr,
        .flags  = 0,
        .stage  = VK_SHADER_STAGE_VERTEX_BIT,
        .module = vs,
        .pName  = "main",
        .pSpecializationInfo = nullptr,
    };

    VkPipelineShaderStageCreateInfo fsStage{
        .sType  = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO,
        .pNext  = nullptr,
        .flags  = 0,
        .stage  = VK_SHADER_STAGE_FRAGMENT_BIT,
        .module = fs,
        .pName  = "main",
        .pSpecializationInfo = nullptr,
    };

    return { vsStage, fsStage };
}

inline constexpr VkPipelineVertexInputStateCreateInfo vertex_input_info(
    std::span<const VkVertexInputBindingDescription>    bindings = {},
    std::span<const VkVertexInputAttributeDescription>  attrs    = {})
{
    VkPipelineVertexInputStateCreateInfo info{};
    info.sType = VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO;
    info.pNext = nullptr;
    info.flags = 0;
    info.vertexBindingDescriptionCount   = static_cast<uint32_t>(bindings.size());
    info.pVertexBindingDescriptions      = bindings.data();
    info.vertexAttributeDescriptionCount = static_cast<uint32_t>(attrs.size());
    info.pVertexAttributeDescriptions    = attrs.data();
    return info;
}

inline constexpr VkPipelineInputAssemblyStateCreateInfo
input_assembly_info(
    VkPrimitiveTopology topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST,
    VkBool32 primitive_restart   = VK_FALSE
) noexcept
{
    VkPipelineInputAssemblyStateCreateInfo info{};
    info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO;
    info.pNext                   = nullptr;
    info.flags                   = 0; // must be 0
    info.topology                = topology;
    info.primitiveRestartEnable  = primitive_restart;

#ifndef NDEBUG
    if (primitive_restart) {
        // Vulkan spec: restart must be OFF for list/patch topologies.
        switch (topology) {
            case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP:
            case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP:
            case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_FAN:
            case VK_PRIMITIVE_TOPOLOGY_LINE_STRIP_WITH_ADJACENCY:
            case VK_PRIMITIVE_TOPOLOGY_TRIANGLE_STRIP_WITH_ADJACENCY:
                break; // OK
            default:
                DEBUG_ASSERT(false && "primitiveRestartEnable must be VK_FALSE for list/patch topologies");
        }
    }
#endif
    return info;
}

inline constexpr VkPipelineViewportStateCreateInfo
viewport_state_info_static(
    std::span<const VkViewport> viewports={},
    std::span<const VkRect2D>   scissors={}
){
#ifndef NDEBUG
    DEBUG_ASSERT(viewports.size() == scissors.size() &&
           "viewportCount must equal scissorCount");
#endif

    VkPipelineViewportStateCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    info.pNext         = nullptr;
    info.flags         = 0;
    info.viewportCount = static_cast<uint32_t>(viewports.size());
    info.pViewports    = viewports.data();
    info.scissorCount  = static_cast<uint32_t>(scissors.size());
    info.pScissors     = scissors.data();
    return info; // borrows spans
}

inline constexpr VkPipelineViewportStateCreateInfo
viewport_state_info_dynamic(uint32_t count) {
    VkPipelineViewportStateCreateInfo info{};
    info.sType         = VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO;
    info.pNext         = nullptr;
    info.flags         = 0;
    info.viewportCount = count;
    info.pViewports    = nullptr; // ignored when VK_DYNAMIC_STATE_VIEWPORT is enabled
    info.scissorCount  = count;
    info.pScissors     = nullptr; // ignored when VK_DYNAMIC_STATE_SCISSOR is enabled
    return info;
}



inline constexpr VkPipelineRasterizationStateCreateInfo
rasterization_state_info(
    VkCullModeFlags cullMode                      = VK_CULL_MODE_BACK_BIT,           // -> cullMode
    VkFrontFace     frontFace                     = VK_FRONT_FACE_COUNTER_CLOCKWISE, // -> frontFace
    bool            rasterizerDiscardEnable       = false,                           // -> rasterizerDiscardEnable
    std::optional<std::array<float,3>> depthBias  = std::nullopt                     // -> {depthBiasConstantFactor, depthBiasSlopeFactor, depthBiasClamp}
)
{
    const bool biasOn = depthBias.has_value();

    VkPipelineRasterizationStateCreateInfo info{};
    info.sType                   = VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO;
    info.pNext                   = nullptr;
    info.flags                   = 0;

    // Portable, always-safe defaults
    info.polygonMode             = VK_POLYGON_MODE_FILL; // non-solid requires fillModeNonSolid
    info.lineWidth               = 1.0f;                 // >1.0 requires wideLines or dynamic state
    info.depthClampEnable        = VK_FALSE;             // requires depthClamp feature to enable

    // Exposed portable knobs
    info.cullMode                = cullMode;
    info.frontFace               = frontFace;
    info.rasterizerDiscardEnable = rasterizerDiscardEnable ? VK_TRUE : VK_FALSE;

    // Depth bias (core; enable iff provided)
    info.depthBiasEnable         = biasOn ? VK_TRUE : VK_FALSE;
    info.depthBiasConstantFactor = biasOn ? (*depthBias)[0] : 0.0f;
    info.depthBiasSlopeFactor    = biasOn ? (*depthBias)[1] : 0.0f;
    info.depthBiasClamp          = biasOn ? (*depthBias)[2] : 0.0f;

    return info;
}

inline constexpr VkPipelineMultisampleStateCreateInfo
multisample_state_info(
    VkSampleCountFlagBits rasterizationSamples = VK_SAMPLE_COUNT_1_BIT, // -> rasterizationSamples
    std::span<const VkSampleMask> pSampleMask = {},                     // -> pSampleMask (size must match samples)
    bool                  alphaToCoverageEnable = false                // -> alphaToCoverageEnable

    // std::optional<float>  minSampleShading = std::nullopt               // -> sampleShadingEnable/minSampleShading (feature-gated)
)
{
#ifndef NDEBUG
    // if (minSampleShading.has_value()) {
    //     DEBUG_ASSERT(*minSampleShading >= 0.0f && *minSampleShading <= 1.0f && "minSampleShading must be in [0,1]");
    // }
    // If a mask is provided, its word-count must be ceil(samples/32).
    const uint32_t samples = static_cast<uint32_t>(rasterizationSamples);
    const uint32_t words_needed = (samples + 31u) / 32u;
    if (!pSampleMask.empty()) {
        DEBUG_ASSERT(pSampleMask.size() == words_needed && "pSampleMask length must be ceil(rasterizationSamples/32)");
    }
#endif

    VkPipelineMultisampleStateCreateInfo info{};
    info.sType                 = VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO;
    info.pNext                 = nullptr;
    info.flags                 = 0;

    info.rasterizationSamples  = rasterizationSamples;                  // keep in sync with render pass/sample targets
    info.alphaToCoverageEnable = alphaToCoverageEnable ? VK_TRUE : VK_FALSE;

    // // Sample shading: enabled iff minSampleShading is provided.
    // // NOTE: Requires device feature: VkPhysicalDeviceFeatures::sampleRateShading.
    // info.sampleShadingEnable   = minSampleShading.has_value() ? VK_TRUE : VK_FALSE;
    // info.minSampleShading      = minSampleShading.value_or(0.0f);


    // Sample mask: nullptr means "all 1s". If provided, size must be ceil(samples/32).
    info.pSampleMask           = pSampleMask.empty() ? nullptr : pSampleMask.data();

    // alphaToOneEnable is feature-gated (VkPhysicalDeviceFeatures::alphaToOne). Keep portable default = VK_FALSE.
    info.alphaToOneEnable      = VK_FALSE;

    return info;
}
constexpr VkColorComponentFlags all_color_mask =
    VK_COLOR_COMPONENT_R_BIT |
    VK_COLOR_COMPONENT_G_BIT |
    VK_COLOR_COMPONENT_B_BIT |
    VK_COLOR_COMPONENT_A_BIT;

constexpr VkPipelineColorBlendAttachmentState no_blend = {
    VK_FALSE,
    VK_BLEND_FACTOR_ONE,
    VK_BLEND_FACTOR_ZERO,
    VK_BLEND_OP_ADD,
    VK_BLEND_FACTOR_ONE,
    VK_BLEND_FACTOR_ZERO,
    VK_BLEND_OP_ADD,
    all_color_mask
};

inline constexpr VkPipelineColorBlendAttachmentState
color_blend_attachment(
    VkBlendFactor srcColor,
    VkBlendFactor dstColor,
    VkBlendOp colorOp,
    VkBlendFactor srcAlpha,
    VkBlendFactor dstAlpha,
    VkBlendOp alphaOp,
    VkColorComponentFlags mask = all_color_mask)
{
    return VkPipelineColorBlendAttachmentState{
        VK_TRUE,
        srcColor,
        dstColor,
        colorOp,
        srcAlpha,
        dstAlpha,
        alphaOp,
        mask
    };
}

constexpr VkPipelineColorBlendAttachmentState alpha_blend =
    color_blend_attachment(
        VK_BLEND_FACTOR_SRC_ALPHA,
        VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA,
        VK_BLEND_OP_ADD,
        VK_BLEND_FACTOR_ONE,
        VK_BLEND_FACTOR_ZERO,
        VK_BLEND_OP_ADD
    );



inline constexpr VkPipelineColorBlendStateCreateInfo
color_blend_state(
    std::span<const VkPipelineColorBlendAttachmentState> attachments,
    std::array<float,4> blend_constants = {0.f, 0.f, 0.f, 0.f})
{
    VkPipelineColorBlendStateCreateInfo info{};
    info.sType           = VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO;
    info.pNext           = nullptr;
    info.flags           = 0;

    info.logicOpEnable   = VK_FALSE ;
    info.logicOp         = VK_LOGIC_OP_CLEAR;

    info.attachmentCount = static_cast<uint32_t>(attachments.size());
    info.pAttachments    = attachments.data();

    info.blendConstants[0] = blend_constants[0];
    info.blendConstants[1] = blend_constants[1];
    info.blendConstants[2] = blend_constants[2];
    info.blendConstants[3] = blend_constants[3];

    return info;
}

inline constexpr VkPipelineDynamicStateCreateInfo
dynamic_state_info(std::span<const VkDynamicState> dynamic_states)
{
    VkPipelineDynamicStateCreateInfo dyn{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
    dyn.dynamicStateCount = dynamic_states.size();
    dyn.pDynamicStates = dynamic_states.data();

    return dyn;
}


inline VkGraphicsPipelineCreateInfo graphics_pipeline_info(
    // REQUIRED (non-null)
    std::span<const VkPipelineShaderStageCreateInfo>  stages,
    const VkPipelineVertexInputStateCreateInfo*       vertex_input,
    const VkPipelineInputAssemblyStateCreateInfo*     input_assembly,
    const VkPipelineViewportStateCreateInfo*          viewport_state,
    const VkPipelineRasterizationStateCreateInfo*     raster_state,
    const VkPipelineMultisampleStateCreateInfo*       multisample_state,
    const VkPipelineColorBlendStateCreateInfo*        color_blend_state,
    VkPipelineLayout                                  layout,
    VkRenderPass                                      render_pass,

    // COMMON OPTIONALS (often null)
    uint32_t                                          subpass = 0,
    const VkPipelineDynamicStateCreateInfo*           dynamic_state   = nullptr,
    const VkPipelineDepthStencilStateCreateInfo*      depth_stencil   = nullptr,

    // RARER OPTIONALS
    const VkPipelineTessellationStateCreateInfo*      tessellation    = nullptr,
    VkPipelineCreateFlags                             flags           = 0,
    VkPipeline                                        base_handle     = VK_NULL_HANDLE,
    int32_t                                           base_index      = -1
) {
#ifndef NDEBUG
    // Required pointers must be non-null for static path
    DEBUG_ASSERT(!stages.empty() && "graphics pipeline needs at least one shader stage");
    DEBUG_ASSERT(vertex_input      && "pVertexInputState must not be null (static path)");
    DEBUG_ASSERT(input_assembly    && "pInputAssemblyState must not be null (static path)");
    DEBUG_ASSERT(viewport_state    && "pViewportState must not be null (static path)");
    DEBUG_ASSERT(raster_state      && "pRasterizationState must not be null (static path)");
    DEBUG_ASSERT(multisample_state && "pMultisampleState must not be null (static path)");
    DEBUG_ASSERT(color_blend_state && "pColorBlendState must not be null (static path)");
    DEBUG_ASSERT(layout            != VK_NULL_HANDLE && "pipeline layout is required");
    DEBUG_ASSERT(render_pass       != VK_NULL_HANDLE && "render pass is required");

    // If tessellation shaders are present, tessellation state must be provided
    bool has_tess = false;
    for (auto& s : stages) {
        if (s.stage == VK_SHADER_STAGE_TESSELLATION_CONTROL_BIT ||
            s.stage == VK_SHADER_STAGE_TESSELLATION_EVALUATION_BIT) { has_tess = true; break; }
    }
    if (has_tess) {
        DEBUG_ASSERT(tessellation && "tessellation shaders require pTessellationState");
    }

    if (flags & VK_PIPELINE_CREATE_DERIVATIVE_BIT) {
        DEBUG_ASSERT((base_handle != VK_NULL_HANDLE) || (base_index >= 0) &&
               "derivative pipeline requires base handle or non-negative base index");
    }
#endif

    VkGraphicsPipelineCreateInfo info{};
    info.sType               = VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO;
    info.pNext               = nullptr;
    info.flags               = flags;

    info.stageCount          = static_cast<uint32_t>(stages.size());
    info.pStages             = stages.data();

    info.pVertexInputState   = vertex_input;
    info.pInputAssemblyState = input_assembly;
    info.pTessellationState  = tessellation;          // nullptr if unused
    info.pViewportState      = viewport_state;
    info.pRasterizationState = raster_state;
    info.pMultisampleState   = multisample_state;
    info.pDepthStencilState  = depth_stencil;         // nullptr if no depth/stencil
    info.pColorBlendState    = color_blend_state;
    info.pDynamicState       = dynamic_state;         // nullptr for fully static

    info.layout              = layout;
    info.renderPass          = render_pass;
    info.subpass             = subpass;

    info.basePipelineHandle  = base_handle;
    info.basePipelineIndex   = base_index;

    return info;
}

inline VkResult create_graphics_pipeline(
    VkPipeline& outPipe,
    VkDevice device,
    std::span<const VkPipelineShaderStageCreateInfo> stages,
    const VkPipelineViewportStateCreateInfo* viewport_state,
    VkPipelineLayout layout,
    VkRenderPass render_pass,

    // VALUE OPTIONALS (defaults via your constexpr helpers)
    VkPipelineRasterizationStateCreateInfo raster_state     = render::rasterization_state_info(),
    VkPipelineColorBlendStateCreateInfo    color_blend_state= render::color_blend_state({&render::alpha_blend, 1}),
    VkPipelineVertexInputStateCreateInfo   vertex_input     = render::vertex_input_info(),
    VkPipelineInputAssemblyStateCreateInfo input_assembly   = render::input_assembly_info(),
    VkPipelineMultisampleStateCreateInfo   multisample_state= render::multisample_state_info(),

    // COMMON OPTIONALS (often null)
    uint32_t subpass = 0,
    const VkPipelineDynamicStateCreateInfo*      dynamic_state = nullptr,
    const VkPipelineDepthStencilStateCreateInfo* depth_stencil = nullptr,

    // RARER OPTIONALS
    const VkPipelineTessellationStateCreateInfo* tessellation  = nullptr,
    VkPipelineCreateFlags flags = 0,
    VkPipeline base_handle = VK_NULL_HANDLE,
    int32_t   base_index  = -1
) {
    if (!viewport_state) return VK_ERROR_INITIALIZATION_FAILED; // required

    // Build the top-level create-info (addresses of by-value args are valid for the call)
    auto gp = render::graphics_pipeline_info(
        stages,
        &vertex_input,
        &input_assembly,
        viewport_state,
        &raster_state,
        &multisample_state,
        &color_blend_state,
        layout,
        render_pass,
        subpass,
        dynamic_state,
        depth_stencil,
        tessellation,
        flags,
        base_handle,
        base_index
    );

    return vkCreateGraphicsPipelines(device, VK_NULL_HANDLE, 1, &gp, nullptr, &outPipe);
}

inline VkResult create_graphics_pipeline_basic(
    VkPipeline& outPipe,
    VkDevice device,
    std::span<const VkPipelineShaderStageCreateInfo> stages,
    const VkPipelineViewportStateCreateInfo* viewport_state,
    VkPipelineLayout layout,
    VkRenderPass render_pass,
    VkCullModeFlags cull = VK_CULL_MODE_BACK_BIT,
    const VkPipelineColorBlendAttachmentState& blend = render::alpha_blend,
    uint32_t subpass = 0,
    const VkPipelineDynamicStateCreateInfo* dynamic_state = nullptr
) {
    // value blocks
    auto raster_state      = render::rasterization_state_info(cull);
    auto vertex_input      = render::vertex_input_info();
    auto input_assembly    = render::input_assembly_info();
    auto multisample_state = render::multisample_state_info();

    // make a color-blend state from a single attachment
    VkPipelineColorBlendAttachmentState att[1] = { blend };
    auto color_blend_state = render::color_blend_state(att);

    return create_graphics_pipeline(
        outPipe, device, stages, viewport_state, layout, render_pass,
        raster_state, color_blend_state, vertex_input, input_assembly, multisample_state,
        subpass, dynamic_state
    );
}

inline VkRenderPassBeginInfo render_pass_begin_info(
    VkRenderPass render_pass,
    VkFramebuffer framebuffer,
    VkExtent2D extent,
    std::span<const VkClearValue> clears,
    VkOffset2D offset = {0, 0}
) {
    VkRenderPassBeginInfo info{VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO};
    info.renderPass = render_pass;
    info.framebuffer = framebuffer;
    info.renderArea.offset = offset;
    info.renderArea.extent = extent;
    info.clearValueCount   = static_cast<uint32_t>(clears.size());
    info.pClearValues      = clears.data();
    return info;
}

// -----------------------------------------------------------------------------
// Descriptor helpers (flat, prefixed with desc_)
// -----------------------------------------------------------------------------

inline constexpr VkDescriptorSetLayoutBinding
desc_binding(uint32_t binding_idx,
             VkDescriptorType type,
             uint32_t descriptor_count,
             VkShaderStageFlags stages,
             std::span<const VkSampler> immutable_samplers = {})
{
    VkDescriptorSetLayoutBinding b{};
    b.binding            = binding_idx;
    b.descriptorType     = type;
    b.descriptorCount    = descriptor_count;
    b.stageFlags         = stages;
    b.pImmutableSamplers = immutable_samplers.empty() ? nullptr
                                                      : immutable_samplers.data();
    return b;
}

inline constexpr VkDescriptorSetLayoutCreateInfo
desc_layout_info(std::span<const VkDescriptorSetLayoutBinding> bindings,
                 VkDescriptorSetLayoutCreateFlags flags = 0)
{
    VkDescriptorSetLayoutCreateInfo ci{};
    ci.sType        = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO;
    ci.pNext        = nullptr;
    ci.flags        = flags;
    ci.bindingCount = static_cast<uint32_t>(bindings.size());
    ci.pBindings    = bindings.data();
    return ci;
}

inline constexpr VkDescriptorPoolSize
desc_pool_size(VkDescriptorType type, uint32_t count)
{
    VkDescriptorPoolSize s{};
    s.type            = type;
    s.descriptorCount = count;
    return s;
}

inline constexpr VkDescriptorPoolCreateInfo
desc_pool_info(std::span<const VkDescriptorPoolSize> sizes,
               uint32_t max_sets,
               VkDescriptorPoolCreateFlags flags = 0)
{
    VkDescriptorPoolCreateInfo ci{};
    ci.sType         = VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO;
    ci.pNext         = nullptr;
    ci.flags         = flags;
    ci.maxSets       = max_sets;
    ci.poolSizeCount = static_cast<uint32_t>(sizes.size());
    ci.pPoolSizes    = sizes.data();
    return ci;
}

inline constexpr VkDescriptorSetAllocateInfo
desc_alloc_info(VkDescriptorPool pool,
                std::span<const VkDescriptorSetLayout> layouts)
{
    VkDescriptorSetAllocateInfo ai{};
    ai.sType              = VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO;
    ai.pNext              = nullptr;
    ai.descriptorPool     = pool;
    ai.descriptorSetCount = static_cast<uint32_t>(layouts.size());
    ai.pSetLayouts        = layouts.data();
    return ai;
}

inline constexpr VkDescriptorImageInfo
desc_image_info(VkSampler sampler,
                VkImageView view,
                VkImageLayout layout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL)
{
    VkDescriptorImageInfo ii{};
    ii.sampler     = sampler;
    ii.imageView   = view;
    ii.imageLayout = layout;
    return ii;
}

inline constexpr VkWriteDescriptorSet
desc_write_image(VkDescriptorSet set,
                 uint32_t binding,
                 const VkDescriptorImageInfo* info,
                 uint32_t array_element = 0,
                 VkDescriptorType type = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
{
    VkWriteDescriptorSet w{};
    w.sType           = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
    w.pNext           = nullptr;
    w.dstSet          = set;
    w.dstBinding      = binding;
    w.dstArrayElement = array_element;
    w.descriptorCount = 1;
    w.descriptorType  = type;
    w.pImageInfo      = info;
    return w;
}


} // namespace render

#endif //RENDER_PIPELINE_HPP