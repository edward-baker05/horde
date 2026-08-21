#include "gfx/SpriteBatch.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <immintrin.h>

#include "gfx/ShaderLoader.hpp"

namespace horde::gfx {

SpriteBatch::~SpriteBatch() {
    shutdown();
}

bool SpriteBatch::init(SDL_GPUDevice* device, const ShaderLoader& shaders, SDL_GPUTextureFormat colorTargetFormat,
                       Uint32 maxSprites) {
    m_device = device;
    m_maxSprites = maxSprites;

    // The vertex shader reads one storage buffer and one uniform buffer; the
    // fragment shader reads one sampled texture. These counts must match the
    // HLSL exactly.
    SDL_GPUShader* vertex = shaders.loadGraphics(
        "sprite.vert", SDL_GPU_SHADERSTAGE_VERTEX,
        ShaderResources{.samplers = 0, .storageTextures = 0, .storageBuffers = 1, .uniformBuffers = 1});

    if (vertex == nullptr) {
        return false;
    }

    SDL_GPUShader* fragment = shaders.loadGraphics(
        "sprite.frag", SDL_GPU_SHADERSTAGE_FRAGMENT,
        ShaderResources{.samplers = 1, .storageTextures = 0, .storageBuffers = 0, .uniformBuffers = 0});

    if (fragment == nullptr) {
        SDL_ReleaseGPUShader(device, vertex);
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
    colorTarget.format = colorTargetFormat;
    colorTarget.blend_state = blend;

    SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
    pipelineInfo.vertex_shader = vertex;
    pipelineInfo.fragment_shader = fragment;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.target_info.num_color_targets = 1;
    pipelineInfo.target_info.color_target_descriptions = &colorTarget;
    // No vertex_input_state: quads come from SV_VertexID.

    m_pipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);

    SDL_ReleaseGPUShader(device, vertex);
    SDL_ReleaseGPUShader(device, fragment);

    if (m_pipeline == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_CreateGPUGraphicsPipeline failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUBufferCreateInfo bufferInfo{};
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bufferInfo.size = static_cast<Uint32>(sizeof(Sprite)) * m_maxSprites;

    m_spriteBuffer = SDL_CreateGPUBuffer(device, &bufferInfo);

    if (m_spriteBuffer == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_CreateGPUBuffer failed: %s", SDL_GetError());
        return false;
    }

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = bufferInfo.size;

    m_transferBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);

    if (m_transferBuffer == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_CreateGPUTransferBuffer failed: %s", SDL_GetError());
        return false;
    }

    // Nearest filtering keeps pixel art crisp at any zoom, which matters for
    // both the world view and the tech tree.
    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_REPEAT;

    m_sampler = SDL_CreateGPUSampler(device, &samplerInfo);

    if (m_sampler == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_CreateGPUSampler failed: %s", SDL_GetError());
        return false;
    }

    m_sprites.reserve(m_maxSprites);
    return true;
}

void SpriteBatch::shutdown() {
    if (m_device == nullptr) {
        return;
    }

    if (m_sampler != nullptr) {
        SDL_ReleaseGPUSampler(m_device, m_sampler);
        m_sampler = nullptr;
    }

    if (m_transferBuffer != nullptr) {
        SDL_ReleaseGPUTransferBuffer(m_device, m_transferBuffer);
        m_transferBuffer = nullptr;
    }

    if (m_spriteBuffer != nullptr) {
        SDL_ReleaseGPUBuffer(m_device, m_spriteBuffer);
        m_spriteBuffer = nullptr;
    }

    if (m_pipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipeline);
        m_pipeline = nullptr;
    }

    m_device = nullptr;
}

bool SpriteBatch::ensureCapacity(Uint32 requiredCapacity) {
    if (requiredCapacity <= m_maxSprites) {
        return true;
    }
    if (m_device == nullptr) {
        return false;
    }

    Uint32 newCapacity = m_maxSprites > 0 ? m_maxSprites : 65536;
    while (newCapacity < requiredCapacity) {
        newCapacity *= 2;
    }

    if (m_spriteBuffer != nullptr) {
        SDL_ReleaseGPUBuffer(m_device, m_spriteBuffer);
        m_spriteBuffer = nullptr;
    }
    if (m_transferBuffer != nullptr) {
        SDL_ReleaseGPUTransferBuffer(m_device, m_transferBuffer);
        m_transferBuffer = nullptr;
    }

    m_maxSprites = newCapacity;

    SDL_GPUBufferCreateInfo bufferInfo{};
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bufferInfo.size = static_cast<Uint32>(sizeof(Sprite)) * m_maxSprites;
    m_spriteBuffer = SDL_CreateGPUBuffer(m_device, &bufferInfo);
    if (m_spriteBuffer == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SpriteBatch::ensureCapacity failed to create GPU buffer: %s", SDL_GetError());
        return false;
    }

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = bufferInfo.size;
    m_transferBuffer = SDL_CreateGPUTransferBuffer(m_device, &transferInfo);
    if (m_transferBuffer == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SpriteBatch::ensureCapacity failed to create transfer buffer: %s", SDL_GetError());
        return false;
    }

    m_sprites.reserve(m_maxSprites);
    return true;
}

void SpriteBatch::begin() {
    m_sprites.clear();
    m_runs.clear();
}

void SpriteBatch::draw(const Sprite& sprite, SDL_GPUTexture* texture) {
    if (texture == nullptr) {
        return;
    }

    ensureCapacity(static_cast<Uint32>(m_sprites.size() + 1));

    if (m_runs.empty() || m_runs.back().texture != texture) {
        m_runs.push_back({texture, static_cast<Uint32>(m_sprites.size()), 0});
    }

    m_runs.back().count += 1;
    m_sprites.push_back(sprite);
}

void SpriteBatch::drawUnits(const glm::vec2* positions, const int* healths, size_t count, glm::vec2 size,
                            glm::vec4 uv, SDL_GPUTexture* texture) {
    if (texture == nullptr || count == 0 || positions == nullptr) {
        return;
    }

    ensureCapacity(static_cast<Uint32>(m_sprites.size() + count));

    if (m_runs.empty() || m_runs.back().texture != texture) {
        m_runs.push_back({texture, static_cast<Uint32>(m_sprites.size()), 0});
    }

    const size_t oldSize = m_sprites.size();
    m_runs.back().count += static_cast<Uint32>(count);
    m_sprites.resize(oldSize + count);

    Sprite* dst = m_sprites.data() + oldSize;

    // Stream 64-byte Sprite structs directly via two 256-bit AVX2 stores per unit
    for (size_t i = 0; i < count; ++i) {
        float r = 0.0f;
        float g = 1.0f;
        float b = 0.2f;

        if (i == 0) {
            r = 1.0f;
            g = 0.0f;
            b = 0.0f;
        } else if (healths != nullptr) {
            const int hp = healths[i];
            if (hp == -1) {
                // Sleeping perimeter block unit: Vivid Sky Blue / Cyan
                r = 0.20f;
                g = 0.80f;
                b = 1.00f;
            } else if (hp == -2) {
                // Sleeping interior block unit: Rich Royal Blue
                r = 0.10f;
                g = 0.40f;
                b = 1.00f;
            } else {
                const float hpRatio = std::clamp(static_cast<float>(hp) * 0.1f, 0.0f, 1.0f);
                r = 1.0f - hpRatio;
                g = hpRatio;
                b = 0.15f;
            }
        }

        __m256 reg0 = _mm256_setr_ps(positions[i].x, positions[i].y, 0.0f, 0.0f, size.x, size.y, 0.0f, 0.0f);
        __m256 reg1 = _mm256_setr_ps(uv.x, uv.y, uv.z, uv.w, r, g, b, 1.0f);

        float* target = reinterpret_cast<float*>(dst + i);
        _mm256_storeu_ps(target, reg0);
        _mm256_storeu_ps(target + 8, reg1);
    }
}

void SpriteBatch::upload(SDL_GPUCommandBuffer* commands) {
    m_uploadedCount = static_cast<Uint32>(m_sprites.size());

    if (m_uploadedCount == 0) {
        return;
    }

    const Uint32 byteCount = m_uploadedCount * static_cast<Uint32>(sizeof(Sprite));

    // Cycling hands us a fresh region rather than stalling on the previous
    // frame's copy still being in flight.
    void* mapped = SDL_MapGPUTransferBuffer(m_device, m_transferBuffer, true);
    SDL_memcpy(mapped, m_sprites.data(), byteCount);
    SDL_UnmapGPUTransferBuffer(m_device, m_transferBuffer);

    SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commands);

    SDL_GPUTransferBufferLocation source{};
    source.transfer_buffer = m_transferBuffer;
    source.offset = 0;

    SDL_GPUBufferRegion destination{};
    destination.buffer = m_spriteBuffer;
    destination.offset = 0;
    destination.size = byteCount;

    SDL_UploadToGPUBuffer(copyPass, &source, &destination, true);
    SDL_EndGPUCopyPass(copyPass);
}

void SpriteBatch::flush(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const glm::mat4& viewProjection) {
    if (m_uploadedCount == 0) {
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, m_pipeline);
    SDL_BindGPUVertexStorageBuffers(pass, 0, &m_spriteBuffer, 1);
    SDL_PushGPUVertexUniformData(commands, 0, &viewProjection, sizeof(viewProjection));

    for (const Run& run : m_runs) {
        SDL_GPUTextureSamplerBinding binding{};
        binding.texture = run.texture;
        binding.sampler = m_sampler;
        SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);

        // Six vertices per sprite; the vertex shader turns them into two
        // triangles. first_vertex feeds through to SV_VertexID, so the shader
        // still indexes the storage buffer correctly.
        SDL_DrawGPUPrimitives(pass, run.count * 6, 1, run.first * 6, 0);
    }
}

} // namespace horde::gfx
