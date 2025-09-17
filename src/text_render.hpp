#ifndef TEXT_RENDER_HPP
#define TEXT_RENDER_HPP

#include <string_view>
#include "render.hpp"
#include "render_pipeline.hpp"
#include "text_atlas.hpp"

int measure_text_x_px(const FontAtlasCPU& cpu, std::string_view s);
inline int measure_y_px(const FontAtlasCPU& cpu){
    return cpu.ascent - cpu.descent + cpu.line_gap;
}

VkResult build_text_sampler(VkSampler* out,VkFilter filter,VkDevice device);


struct alignas(16) RightTriangle { float x0,y0, dx,dy; };
struct alignas(16) TriPair       { RightTriangle screen, uv; };

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
layout(set=1, binding=0) uniform sampler2D atlas;

void main() {
    float a = texture(atlas, vUV).r;
    outColor = vec4(pc.color.rgb, pc.color.a * a);
}

)GLSL";

// Per-instance vertex payload (one instance == one triangle)
struct alignas(32) TriInstance {
    float sx0, sy0;  // screen base
    float sdx, sdy;  // screen side
    float u0,  v0;   // uv base
    float du,  dv;   // uv side
};
static_assert(sizeof(TriInstance) == 32, "TriInstance must be 32B");

// Build per-triangle instances for a whole line (two TriPair per glyph)
void text_line_tripairs(std::vector<TriPair>& out,
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

    // 2) resets the buffer
    inline void frame_start() {m_frameUsed = 0;}

    // 2) allocates memory for the currrent frame and resets the buffer
    VkResult frame_start(VkDevice device,
	                       VkPhysicalDevice phys,
	                       uint32_t instance_capacity);

    // 2b) allocates memory for the currrent frame
    VkResult maybe_realloc_instances(VkDevice device,
                                           VkPhysicalDevice phys,
                                           uint32_t instance_capacity);

    // 3) Record a draw given TriPairs (we pack to TriInstance internally).
    VkResult record_draw(VkDevice device,
                         VkPhysicalDevice phys,
                         VkCommandBuffer cb,
                         std::span<const TriPair> pairs,
                         const float rgba[4]);

    // 3b) Convenience: build TriPairs for a line then draw.
    VkResult record_draw_line(VkDevice device,
                              VkPhysicalDevice phys,
                              VkCommandBuffer cb,
                              std::string_view s,
                              float origin_x, float origin_y,
                              float box_scale_x, float box_scale_y,
                              const FontAtlasCPU& cpu,
                              const float rgba[4]);

    // 4) Cleanup
    void destroy(VkDevice device);

    // expose the dynamic array (VB) if you want to fill it yourself
    VkBuffer     vertex_buffer() const { return m_vb; }
    VkDeviceSize vertex_buffer_capacity() const { return m_vbCap; }



private:
    // pipeline bits
    VkDescriptorSetLayout m_set0_empty  = VK_NULL_HANDLE;
	VkDescriptorSetLayout m_set1_atlas  = VK_NULL_HANDLE;
    
    VkPipelineLayout      m_layout    = VK_NULL_HANDLE;
    VkPipeline            m_pipeline  = VK_NULL_HANDLE;

    // descriptors
    VkDescriptorPool m_pool = VK_NULL_HANDLE;
    VkDescriptorSet  m_ds   = VK_NULL_HANDLE;

    // dynamic vertex buffer (persistently mapped)
    VkBuffer        m_vb    = VK_NULL_HANDLE;
    VkDeviceMemory  m_vbMem = VK_NULL_HANDLE;
    VkDeviceSize    m_frameUsed = 0;
    VkDeviceSize    m_vbCap = 0;
    void*           m_vbPtr = nullptr;

    // non-owned atlas handles
    VkImageView     m_atlasView   = VK_NULL_HANDLE;
    VkSampler       m_atlasSampler= VK_NULL_HANDLE;

private:

    VkResult build_pipeline_(VkDevice device,
                             VkRenderPass rp,
                             VkShaderModule vs, VkShaderModule fs,
                             const VkViewport& vp, const VkRect2D& sc);

    VkResult ensure_vb_capacity_(VkDevice device,
                                 VkPhysicalDevice phys,
                                 VkDeviceSize bytes);

    
};


#endif // TEXT_RENDER_HPP

