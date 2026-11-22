#include "gfx/Blur.hpp"

#include <SDL3/SDL_log.h>

#include <glm/common.hpp>

#include "gfx/ShaderLoader.hpp"

namespace horde::gfx {

namespace {

// Matches `BlurUniforms` in shaders/src/blur.frag.hlsl.
struct BlurUniforms {
    glm::vec2 direction;
    glm::vec2 texelSize;
};

} // namespace

Blur::~Blur() {
    shutdown();
}

bool Blur::init(SDL_GPUDevice* device, const ShaderLoader& shaders, SDL_GPUTextureFormat format) {
    m_device = device;
    m_format = format;

    SDL_GPUShader* vertex = shaders.loadGraphics("fullscreen.vert", SDL_GPU_SHADERSTAGE_VERTEX, ShaderResources{});
    if (vertex == nullptr) {
        return false;
    }
    SDL_GPUShader* fragment = shaders.loadGraphics(
        "blur.frag", SDL_GPU_SHADERSTAGE_FRAGMENT,
        ShaderResources{.samplers = 1, .storageTextures = 0, .storageBuffers = 0, .uniformBuffers = 1});
    if (fragment == nullptr) {
        SDL_ReleaseGPUShader(device, vertex);
        return false;
    }

    // Opaque overwrite: no blending, no vertex input (the triangle is built from the vertex id).
    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = format;

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.target_info.num_color_targets = 1;
    info.target_info.color_target_descriptions = &colorTarget;
    m_pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);

    SDL_ReleaseGPUShader(device, vertex);
    SDL_ReleaseGPUShader(device, fragment);
    if (m_pipeline == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Blur: SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_sampler = SDL_CreateGPUSampler(device, &samplerInfo);
    if (m_sampler == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Blur: SDL_CreateGPUSampler failed: %s", SDL_GetError());
        return false;
    }
    return true;
}

void Blur::shutdown() {
    m_horizontal.release();
    m_vertical.release();
    if (m_device == nullptr) {
        return;
    }
    if (m_sampler != nullptr) {
        SDL_ReleaseGPUSampler(m_device, m_sampler);
        m_sampler = nullptr;
    }
    if (m_pipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipeline);
        m_pipeline = nullptr;
    }
    m_device = nullptr;
}

bool Blur::resize(glm::ivec2 size) {
    const glm::ivec2 half = glm::max(size / 2, glm::ivec2(1));
    return m_horizontal.resize(m_device, m_format, half) && m_vertical.resize(m_device, m_format, half);
}

bool Blur::pass(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* source, RenderTarget& target, glm::vec2 direction) {
    SDL_GPUColorTargetInfo colorTarget{};
    colorTarget.texture = target.texture();
    colorTarget.load_op = SDL_GPU_LOADOP_DONT_CARE;
    colorTarget.store_op = SDL_GPU_STOREOP_STORE;

    SDL_GPURenderPass* renderPass = SDL_BeginGPURenderPass(commands, &colorTarget, 1, nullptr);
    if (renderPass == nullptr) {
        return false;
    }
    SDL_BindGPUGraphicsPipeline(renderPass, m_pipeline);

    SDL_GPUTextureSamplerBinding binding{source, m_sampler};
    SDL_BindGPUFragmentSamplers(renderPass, 0, &binding, 1);

    // One step is a texel of the half-resolution target in both passes; the
    // first pass reads a full-size source, which the linear filter averages.
    const BlurUniforms uniforms{direction, 1.0f / glm::vec2(target.size())};
    SDL_PushGPUFragmentUniformData(commands, 0, &uniforms, sizeof(uniforms));

    SDL_DrawGPUPrimitives(renderPass, 3, 1, 0, 0);
    SDL_EndGPURenderPass(renderPass);
    return true;
}

SDL_GPUTexture* Blur::apply(SDL_GPUCommandBuffer* commands, SDL_GPUTexture* source, glm::ivec2 size) {
    if (source == nullptr || !resize(size)) {
        return nullptr;
    }
    if (!pass(commands, source, m_horizontal, {1.0f, 0.0f}) ||
        !pass(commands, m_horizontal.texture(), m_vertical, {0.0f, 1.0f})) {
        return nullptr;
    }
    return m_vertical.texture();
}

} // namespace horde::gfx
