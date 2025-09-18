#ifndef TEXT_RENDER_HPP
#define TEXT_RENDER_HPP

#include <string_view>
#include "render.hpp"
#include "render_pipeline.hpp"
#include "text_atlas.hpp"
#include "memory.hpp"

int measure_text_x_px(const FontAtlasCPU& cpu, std::string_view s);
inline int measure_y_px(const FontAtlasCPU& cpu){
    return cpu.ascent - cpu.descent + cpu.line_gap;
}

VkResult build_text_sampler(VkSampler* out,VkFilter filter,VkDevice device);


struct alignas(16) RightTriangle { float x0,y0, dx,dy; };
struct alignas(32) TriPair       { RightTriangle screen, uv; };
static_assert(sizeof(TriPair) == 32, "TriPair must be 32B");

//we feed in a TriPair
constexpr const char* text_render_vs = R"GLSL(
#version 450
// per-instance attributes (binding 0)
layout(location=0) in vec2 in_screen_base;  // x0,y0
layout(location=1) in vec2 in_screen_side;  // dx,dy
layout(location=2) in vec2 in_uv_base;      // u0,v0
layout(location=3) in vec2 in_uv_side;      // du,dv

layout(location=0) out vec2 vUV;

vec2 tri_corner(vec2 base, vec2 side, uint i) {
    return (i == 0u) ? base
         : (i == 1u) ? base + vec2(side.x, 0.0)
                     : base + vec2(0.0,     side.y);
}

void main() {
    uint vi = uint(gl_VertexIndex % 3);  // 0..2 within the triangle
    vec2 pos = tri_corner(in_screen_base, in_screen_side, vi);
    vUV      = tri_corner(in_uv_base,     in_uv_side,     vi);
    gl_Position = vec4(pos, 0.0, 1.0);
}
)GLSL";



constexpr const char* text_render_fs = R"GLSL(
#version 450
layout(push_constant) uniform PC { vec4 color; } pc;

layout(location=0) in  vec2 vUV;
layout(location=0) out vec4 outColor;

// combined image+sampler (VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER)
layout(set=0, binding=0) uniform sampler2D atlas;

void main() {
    float a = texture(atlas, vUV).r;
    outColor = vec4(pc.color.rgb, pc.color.a * a);
}

)GLSL";


// Build per-triangle instances for a whole line (two TriPair per glyph)
void text_line_draw_info(std::vector<TriPair>& out,
                        std::string_view s,
                        float x, float y,        // pen origin
                        float sx, float sy,      // text scale
                        const FontAtlasCPU& cpu);


class TextRenderer {
public:
    // 1) Create pipeline/layouts/descriptors (atlas sampler is non-owned & reused).
    VkResult create(VkDevice device,
                    VkRenderPass renderPass,
                    VkShaderModule vs, VkShaderModule fs,
                    const VkViewport& viewport,
                    const VkRect2D&  scissor,
                    VkImageView atlasView,
                    VkSampler   atlasSampler);

    // 2) Record a draw given TriPairs (we pack to TriInstance internally).
    VkResult record_draw( VkCommandBuffer cb,
                      MappedArena& arena,
                      std::span<const TriPair> pairs,
                      const float rgba[4]);

    // 2b) Convenience: build TriPairs for a line then draw.
    VkResult record_draw_line( VkCommandBuffer cb,
                               MappedArena& arena,
                               std::string_view s,
                               float x, float y,
                               float sx, float sy,
                               const FontAtlasCPU& cpu,
                               const float rgba[4]);

    // 3) Cleanup
    void destroy(VkDevice device);


private:
    // pipeline bits
	VkDescriptorSetLayout m_set  = VK_NULL_HANDLE;

    
    VkPipelineLayout      m_layout    = VK_NULL_HANDLE;
    VkPipeline            m_pipeline  = VK_NULL_HANDLE;

    // descriptors
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet  m_ds   = VK_NULL_HANDLE;

    // non-owned atlas handles
    VkImageView     m_atlasView   = VK_NULL_HANDLE;
    VkSampler       m_atlasSampler= VK_NULL_HANDLE;

private:

    VkResult build_pipeline_(VkDevice device,
                             VkRenderPass rp,
                             VkShaderModule vs, VkShaderModule fs,
                             const VkViewport& vp, const VkRect2D& sc);    
};


#endif // TEXT_RENDER_HPP

