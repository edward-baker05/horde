#include "ui/RmlRenderer.hpp"

#include <SDL3/SDL.h>
#include <SDL3_image/SDL_image.h>
#include <glm/gtc/matrix_transform.hpp>
#include <glm/gtc/type_ptr.hpp>

#include <cstring>

#include "gfx/ShaderLoader.hpp"

namespace horde::ui {

namespace {

// Must match RmlUniforms in shaders/src/rml.vert.hlsl.
struct Uniforms {
    glm::mat4 transform;
    glm::vec2 translation;
    glm::vec2 padding;
};

} // namespace

RmlRenderer::~RmlRenderer() {
    shutdown();
}

bool RmlRenderer::init(SDL_GPUDevice* device, const gfx::ShaderLoader& shaders, SDL_GPUTextureFormat targetFormat) {
    m_device = device;

    SDL_GPUShader* vertex = shaders.loadGraphics(
        "rml.vert", SDL_GPU_SHADERSTAGE_VERTEX,
        gfx::ShaderResources{.samplers = 0, .storageTextures = 0, .storageBuffers = 0, .uniformBuffers = 1});
    if (vertex == nullptr) {
        return false;
    }

    SDL_GPUShader* fragment = shaders.loadGraphics(
        "rml.frag", SDL_GPU_SHADERSTAGE_FRAGMENT,
        gfx::ShaderResources{.samplers = 1, .storageTextures = 0, .storageBuffers = 0, .uniformBuffers = 0});
    if (fragment == nullptr) {
        SDL_ReleaseGPUShader(device, vertex);
        return false;
    }

    // RmlUi 6 colours and textures are premultiplied, so the source is not
    // multiplied by alpha again.
    SDL_GPUColorTargetBlendState blend{};
    blend.enable_blend = true;
    blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
    blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
    blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
    blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

    SDL_GPUColorTargetDescription colorTarget{};
    colorTarget.format = targetFormat;
    colorTarget.blend_state = blend;

    // Rml::Vertex: float2 position, ubyte4-normalised colour, float2 uv.
    SDL_GPUVertexBufferDescription bufferDescription{};
    bufferDescription.slot = 0;
    bufferDescription.pitch = sizeof(Rml::Vertex);
    bufferDescription.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

    SDL_GPUVertexAttribute attributes[3]{};
    attributes[0].location = 0;
    attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[0].offset = offsetof(Rml::Vertex, position);
    attributes[1].location = 1;
    attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
    attributes[1].offset = offsetof(Rml::Vertex, colour);
    attributes[2].location = 2;
    attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
    attributes[2].offset = offsetof(Rml::Vertex, tex_coord);

    SDL_GPUGraphicsPipelineCreateInfo info{};
    info.vertex_shader = vertex;
    info.fragment_shader = fragment;
    info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    info.vertex_input_state.vertex_buffer_descriptions = &bufferDescription;
    info.vertex_input_state.num_vertex_buffers = 1;
    info.vertex_input_state.vertex_attributes = attributes;
    info.vertex_input_state.num_vertex_attributes = 3;
    // RmlUi emits both windings (transforms can mirror), so no culling.
    info.rasterizer_state.cull_mode = SDL_GPU_CULLMODE_NONE;
    info.target_info.num_color_targets = 1;
    info.target_info.color_target_descriptions = &colorTarget;

    m_pipeline = SDL_CreateGPUGraphicsPipeline(device, &info);
    SDL_ReleaseGPUShader(device, vertex);
    SDL_ReleaseGPUShader(device, fragment);
    if (m_pipeline == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "RmlUi pipeline creation failed: %s", SDL_GetError());
        return false;
    }

    // Linear so text and images stay smooth under dp scaling.
    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mag_filter = SDL_GPU_FILTER_LINEAR;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_sampler = SDL_CreateGPUSampler(device, &samplerInfo);
    if (m_sampler == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "RmlUi sampler creation failed: %s", SDL_GetError());
        return false;
    }

    // Untextured geometry multiplies by this, so one pipeline covers both.
    const Rml::byte white[4] = {255, 255, 255, 255};
    m_white = reinterpret_cast<Texture*>(createTexture(white, 1, 1));
    return m_white != nullptr;
}

void RmlRenderer::shutdown() {
    if (m_device == nullptr) {
        return;
    }

    flushReleases();
    if (m_white != nullptr) {
        ReleaseTexture(reinterpret_cast<Rml::TextureHandle>(m_white));
        m_white = nullptr;
        flushReleases();
    }
    if (m_sampler != nullptr) {
        SDL_ReleaseGPUSampler(m_device, m_sampler);
        m_sampler = nullptr;
    }
    if (m_pipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(m_device, m_pipeline);
        m_pipeline = nullptr;
    }
    for (auto& list : m_lists) {
        list.clear();
    }
    m_device = nullptr;
}

void RmlRenderer::flushReleases() {
    for (SDL_GPUBuffer* buffer : m_pendingBuffers) {
        SDL_ReleaseGPUBuffer(m_device, buffer);
    }
    for (SDL_GPUTexture* texture : m_pendingTextures) {
        SDL_ReleaseGPUTexture(m_device, texture);
    }
    m_pendingBuffers.clear();
    m_pendingTextures.clear();
}

void RmlRenderer::beginFrame() {
    flushReleases();
}

void RmlRenderer::beginRecord(DeviceKind kind, SDL_GPUCommandBuffer* commands, glm::ivec2 targetSize) {
    m_recording = static_cast<int>(kind);
    m_commands = commands;
    m_targetSize = targetSize;
    m_listSizes[m_recording] = targetSize;
    m_lists[m_recording].clear();
    m_scissorEnabled = false;
    m_transform = glm::mat4(1.0f);
}

void RmlRenderer::endRecord() {
    // Copy passes are opened and closed inside withCopyPass, so none is open
    // here; only the command buffer pointer needs dropping.
    m_commands = nullptr;
}

template <typename Fn>
void RmlRenderer::withCopyPass(Fn&& fn) {
    SDL_GPUCommandBuffer* commands = m_commands;
    const bool temporary = commands == nullptr;
    if (temporary) {
        commands = SDL_AcquireGPUCommandBuffer(m_device);
        if (commands == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_GPU, "RmlUi upload: SDL_AcquireGPUCommandBuffer failed: %s", SDL_GetError());
            return;
        }
    }

    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    fn(copy);
    SDL_EndGPUCopyPass(copy);

    if (temporary) {
        SDL_SubmitGPUCommandBuffer(commands);
    }
}

SDL_GPUBuffer* RmlRenderer::uploadBuffer(SDL_GPUBufferUsageFlags usage, const void* data, Uint32 size) {
    SDL_GPUBufferCreateInfo bufferInfo{};
    bufferInfo.usage = usage;
    bufferInfo.size = size;
    SDL_GPUBuffer* buffer = SDL_CreateGPUBuffer(m_device, &bufferInfo);

    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(m_device, &transferInfo);

    if (buffer == nullptr || transfer == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "RmlUi buffer upload failed: %s", SDL_GetError());
        if (buffer != nullptr) {
            SDL_ReleaseGPUBuffer(m_device, buffer);
        }
        if (transfer != nullptr) {
            SDL_ReleaseGPUTransferBuffer(m_device, transfer);
        }
        return nullptr;
    }

    void* mapped = SDL_MapGPUTransferBuffer(m_device, transfer, false);
    std::memcpy(mapped, data, size);
    SDL_UnmapGPUTransferBuffer(m_device, transfer);

    withCopyPass([&](SDL_GPUCopyPass* copy) {
        SDL_GPUTransferBufferLocation source{transfer, 0};
        SDL_GPUBufferRegion destination{buffer, 0, size};
        SDL_UploadToGPUBuffer(copy, &source, &destination, false);
    });

    SDL_ReleaseGPUTransferBuffer(m_device, transfer);
    return buffer;
}

Rml::CompiledGeometryHandle RmlRenderer::CompileGeometry(Rml::Span<const Rml::Vertex> vertices,
                                                         Rml::Span<const int> indices) {
    if (vertices.size() == 0 || indices.size() == 0) {
        return 0;
    }

    auto* geometry = new Geometry();
    geometry->indexCount = static_cast<Uint32>(indices.size());
    geometry->vertices = uploadBuffer(SDL_GPU_BUFFERUSAGE_VERTEX, vertices.data(),
                                      static_cast<Uint32>(vertices.size() * sizeof(Rml::Vertex)));
    geometry->indices =
        uploadBuffer(SDL_GPU_BUFFERUSAGE_INDEX, indices.data(), static_cast<Uint32>(indices.size() * sizeof(int)));

    if (geometry->vertices == nullptr || geometry->indices == nullptr) {
        ReleaseGeometry(reinterpret_cast<Rml::CompiledGeometryHandle>(geometry));
        return 0;
    }
    return reinterpret_cast<Rml::CompiledGeometryHandle>(geometry);
}

void RmlRenderer::RenderGeometry(Rml::CompiledGeometryHandle handle, Rml::Vector2f translation,
                                 Rml::TextureHandle texture) {
    const auto* geometry = reinterpret_cast<const Geometry*>(handle);
    const auto* tex = texture != 0 ? reinterpret_cast<const Texture*>(texture) : m_white;

    SDL_Rect scissor{0, 0, m_targetSize.x, m_targetSize.y};
    if (m_scissorEnabled) {
        scissor = m_scissor;
    }

    m_lists[m_recording].push_back({geometry->vertices, geometry->indices, geometry->indexCount, tex->texture,
                       glm::vec2(translation.x, translation.y), m_transform, scissor});
}

void RmlRenderer::ReleaseGeometry(Rml::CompiledGeometryHandle handle) {
    auto* geometry = reinterpret_cast<Geometry*>(handle);
    if (geometry->vertices != nullptr) {
        m_pendingBuffers.push_back(geometry->vertices);
    }
    if (geometry->indices != nullptr) {
        m_pendingBuffers.push_back(geometry->indices);
    }
    delete geometry;
}

Rml::TextureHandle RmlRenderer::createTexture(const Rml::byte* rgba, int width, int height) {
    SDL_GPUTextureCreateInfo textureInfo{};
    textureInfo.type = SDL_GPU_TEXTURETYPE_2D;
    textureInfo.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    textureInfo.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
    textureInfo.width = static_cast<Uint32>(width);
    textureInfo.height = static_cast<Uint32>(height);
    textureInfo.layer_count_or_depth = 1;
    textureInfo.num_levels = 1;
    SDL_GPUTexture* gpuTexture = SDL_CreateGPUTexture(m_device, &textureInfo);

    const Uint32 size = static_cast<Uint32>(width * height * 4);
    SDL_GPUTransferBufferCreateInfo transferInfo{};
    transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    transferInfo.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(m_device, &transferInfo);

    if (gpuTexture == nullptr || transfer == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "RmlUi texture upload failed: %s", SDL_GetError());
        if (gpuTexture != nullptr) {
            SDL_ReleaseGPUTexture(m_device, gpuTexture);
        }
        if (transfer != nullptr) {
            SDL_ReleaseGPUTransferBuffer(m_device, transfer);
        }
        return 0;
    }

    void* mapped = SDL_MapGPUTransferBuffer(m_device, transfer, false);
    std::memcpy(mapped, rgba, size);
    SDL_UnmapGPUTransferBuffer(m_device, transfer);

    withCopyPass([&](SDL_GPUCopyPass* copy) {
        SDL_GPUTextureTransferInfo source{};
        source.transfer_buffer = transfer;
        SDL_GPUTextureRegion destination{};
        destination.texture = gpuTexture;
        destination.w = static_cast<Uint32>(width);
        destination.h = static_cast<Uint32>(height);
        destination.d = 1;
        SDL_UploadToGPUTexture(copy, &source, &destination, false);
    });

    SDL_ReleaseGPUTransferBuffer(m_device, transfer);
    return reinterpret_cast<Rml::TextureHandle>(new Texture{gpuTexture});
}

Rml::TextureHandle RmlRenderer::LoadTexture(Rml::Vector2i& textureDimensions, const Rml::String& source) {
    SDL_Surface* loaded = IMG_Load(source.c_str());
    if (loaded == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "RmlUi LoadTexture '%s' failed: %s", source.c_str(), SDL_GetError());
        return 0;
    }

    SDL_Surface* surface = SDL_ConvertSurface(loaded, SDL_PIXELFORMAT_RGBA32);
    SDL_DestroySurface(loaded);
    if (surface == nullptr) {
        return 0;
    }

    // RmlUi 6 wants premultiplied alpha; image files are straight.
    std::vector<Rml::byte> pixels(static_cast<size_t>(surface->w) * surface->h * 4);
    for (int y = 0; y < surface->h; ++y) {
        const auto* row = static_cast<const Rml::byte*>(surface->pixels) + static_cast<size_t>(y) * surface->pitch;
        Rml::byte* out = pixels.data() + static_cast<size_t>(y) * surface->w * 4;
        for (int x = 0; x < surface->w; ++x) {
            const unsigned alpha = row[x * 4 + 3];
            out[x * 4 + 0] = static_cast<Rml::byte>((row[x * 4 + 0] * alpha + 127) / 255);
            out[x * 4 + 1] = static_cast<Rml::byte>((row[x * 4 + 1] * alpha + 127) / 255);
            out[x * 4 + 2] = static_cast<Rml::byte>((row[x * 4 + 2] * alpha + 127) / 255);
            out[x * 4 + 3] = static_cast<Rml::byte>(alpha);
        }
    }

    textureDimensions = {surface->w, surface->h};
    const Rml::TextureHandle handle = createTexture(pixels.data(), surface->w, surface->h);
    SDL_DestroySurface(surface);
    return handle;
}

Rml::TextureHandle RmlRenderer::GenerateTexture(Rml::Span<const Rml::byte> source, Rml::Vector2i sourceDimensions) {
    // Already premultiplied RGBA8, per the RmlUi 6 contract.
    return createTexture(source.data(), sourceDimensions.x, sourceDimensions.y);
}

void RmlRenderer::ReleaseTexture(Rml::TextureHandle handle) {
    auto* texture = reinterpret_cast<Texture*>(handle);
    m_pendingTextures.push_back(texture->texture);
    delete texture;
}

void RmlRenderer::EnableScissorRegion(bool enable) {
    m_scissorEnabled = enable;
}

void RmlRenderer::SetScissorRegion(Rml::Rectanglei region) {
    // Clamped: SDL_GPU requires the scissor to stay inside the target.
    const int left = glm::clamp(region.Left(), 0, m_targetSize.x);
    const int top = glm::clamp(region.Top(), 0, m_targetSize.y);
    const int right = glm::clamp(region.Right(), left, m_targetSize.x);
    const int bottom = glm::clamp(region.Bottom(), top, m_targetSize.y);
    m_scissor = {left, top, right - left, bottom - top};
}

void RmlRenderer::SetTransform(const Rml::Matrix4f* transform) {
    // Both are column-major, so the floats copy straight across.
    if (transform == nullptr) {
        m_transform = glm::mat4(1.0f);
    } else {
        m_transform = glm::make_mat4(transform->data());
    }
}

void RmlRenderer::draw(DeviceKind kind, SDL_GPURenderPass* pass, SDL_GPUCommandBuffer* commands) {
    std::vector<DrawCommand>& list = m_lists[static_cast<int>(kind)];
    const glm::ivec2 size = m_listSizes[static_cast<int>(kind)];
    if (list.empty() || m_pipeline == nullptr) {
        return;
    }

    SDL_BindGPUGraphicsPipeline(pass, m_pipeline);

    // Same convention as Camera2D: y-down pixels, zero-to-one depth.
    const glm::mat4 projection =
        glm::orthoRH_ZO(0.0f, static_cast<float>(size.x), static_cast<float>(size.y), 0.0f, -1.0f, 1.0f);

    for (const DrawCommand& command : list) {
        if (command.scissor.w <= 0 || command.scissor.h <= 0) {
            continue;
        }
        SDL_SetGPUScissor(pass, &command.scissor);

        const Uniforms uniforms{projection * command.transform, command.translation, glm::vec2(0.0f)};
        SDL_PushGPUVertexUniformData(commands, 0, &uniforms, sizeof(uniforms));

        SDL_GPUBufferBinding vertexBinding{command.vertices, 0};
        SDL_BindGPUVertexBuffers(pass, 0, &vertexBinding, 1);
        SDL_GPUBufferBinding indexBinding{command.indices, 0};
        SDL_BindGPUIndexBuffer(pass, &indexBinding, SDL_GPU_INDEXELEMENTSIZE_32BIT);

        SDL_GPUTextureSamplerBinding textureBinding{command.texture, m_sampler};
        SDL_BindGPUFragmentSamplers(pass, 0, &textureBinding, 1);

        SDL_DrawGPUIndexedPrimitives(pass, command.indexCount, 1, 0, 0, 0);
    }

    list.clear();
}

} // namespace horde::ui
