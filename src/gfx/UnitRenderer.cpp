#include "gfx/UnitRenderer.hpp"

#include <SDL3/SDL_log.h>

#include "gfx/GpuContext.hpp"
#include "gfx/ShaderLoader.hpp"
#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
#include "scene/Scene.hpp"

namespace horde::gfx {

namespace {

// Matches HLSL cbuffer UnitUniforms in unit.vert.hlsl
struct alignas(16) UnitDrawUniforms {
    glm::mat4 viewProjection;
    glm::vec4 uvRect;   // xy = top-left, zw = bottom-right
    glm::vec4 color;    // tint color
    glm::vec2 unitSize; // width, height
    float alpha;
    float maxJump;
    float hasHealth;
    float padding[3];
};
static_assert(sizeof(UnitDrawUniforms) == 128);

} // namespace

UnitRenderer::~UnitRenderer() {
    shutdown();
}

bool UnitRenderer::init(scene::Services& services) {
    m_device = services.gpu->device();
    m_atlas = services.atlas->handle();

    // unit.vert: 3 storage buffers (units t0, previous positions t1, health t2, space0), 1 uniform buffer (b0, space1)
    SDL_GPUShader* vertexShader = services.shaders->loadGraphics(
        "unit.vert", SDL_GPU_SHADERSTAGE_VERTEX,
        ShaderResources{.samplers = 0, .storageTextures = 0, .storageBuffers = 3, .uniformBuffers = 1});

    // sprite.frag: 1 sampler (s0, space2), 1 sampled texture (t0, space2)
    SDL_GPUShader* fragmentShader = services.shaders->loadGraphics(
        "sprite.frag", SDL_GPU_SHADERSTAGE_FRAGMENT,
        ShaderResources{.samplers = 1, .storageTextures = 0, .storageBuffers = 0, .uniformBuffers = 0});

    if (!vertexShader || !fragmentShader) {
        if (vertexShader) SDL_ReleaseGPUShader(m_device, vertexShader);
        if (fragmentShader) SDL_ReleaseGPUShader(m_device, fragmentShader);
        return false;
    }

    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = true;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = services.gpu->swapchainFormat();
    colorTarget.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vertexShader;
    pipelineInfo.fragment_shader = fragmentShader;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.target_info.num_color_targets = 1;
    pipelineInfo.target_info.color_target_descriptions = &colorTarget;

    m_pipeline = SDL_CreateGPUGraphicsPipeline(m_device, &pipelineInfo);
    SDL_ReleaseGPUShader(m_device, vertexShader);
    SDL_ReleaseGPUShader(m_device, fragmentShader);

    if (!m_pipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create unit graphics pipeline: %s", SDL_GetError());
        return false;
    }

    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    m_sampler = SDL_CreateGPUSampler(m_device, &samplerInfo);
    if (!m_sampler) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create unit sampler: %s", SDL_GetError());
        return false;
    }

    return true;
}

void UnitRenderer::shutdown() {
    if (m_device == nullptr) {
        return;
    }
    if (m_sampler) {
        SDL_ReleaseGPUSampler(m_device, m_sampler);
        m_sampler = nullptr;
    }
    if (m_pipeline) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipeline);
        m_pipeline = nullptr;
    }
    m_device = nullptr;
}

void UnitRenderer::draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, SDL_GPUBuffer* units,
                        Uint32 count, const glm::mat4& viewProjection, float unitSize,
                        const DrawOptions& options) const {
    if (!m_pipeline || !units || count == 0) {
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, m_pipeline);
    // Slots the shader won't read still need something bound.
    SDL_GPUBuffer* buffers[3] = {units, options.previous ? options.previous : units,
                                 options.health ? options.health : units};
    SDL_BindGPUVertexStorageBuffers(pass, 0, buffers, 3);

    UnitDrawUniforms uniforms{};
    uniforms.viewProjection = viewProjection;
    uniforms.uvRect = atlasCell(1, 0, 4, 4);
    uniforms.color = glm::vec4(0.0f, 1.0f, 0.2f, 1.0f);
    uniforms.unitSize = glm::vec2(unitSize, unitSize);
    uniforms.alpha = options.alpha;
    uniforms.maxJump = options.previous ? options.maxJump : 0.0f;
    uniforms.hasHealth = options.health ? 1.0f : 0.0f;
    SDL_PushGPUVertexUniformData(commands, 0, &uniforms, sizeof(uniforms));

    SDL_GPUTextureSamplerBinding atlasBinding{};
    atlasBinding.texture = m_atlas;
    atlasBinding.sampler = m_sampler;
    SDL_BindGPUFragmentSamplers(pass, 0, &atlasBinding, 1);

    // 6 vertices per unit quad
    SDL_DrawGPUPrimitives(pass, count * 6, 1, 0, 0);
}

} // namespace horde::gfx
