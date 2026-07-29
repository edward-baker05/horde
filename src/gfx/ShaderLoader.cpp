#include "gfx/ShaderLoader.hpp"

#include <SDL3/SDL_iostream.h>
#include <SDL3/SDL_log.h>

#include <iostream>

#include "core/Paths.hpp"

namespace horde::gfx {

namespace {

// Reads a whole file through SDL's IO layer so packaged/Android builds keep
// working. Returns nullptr on failure; free with SDL_free.
void* readFile(const std::filesystem::path& path, size_t& sizeOut) {
    void* data = SDL_LoadFile(path.string().c_str(), &sizeOut);

    if (data == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "Could not read '%s': %s", path.string().c_str(), SDL_GetError());
    }

    return data;
}

} // namespace

ShaderLoader::ShaderLoader(SDL_GPUDevice* device) : m_device(device) {
    const SDL_GPUShaderFormat supported = SDL_GetGPUShaderFormats(device);

    if (supported & SDL_GPU_SHADERFORMAT_SPIRV) {
        m_format = SDL_GPU_SHADERFORMAT_SPIRV;
        m_formatName = "SPIR-V";
        m_subdirectory = "spirv";
        m_extension = ".spv";
        m_entrypoint = "main";
    } else if (supported & SDL_GPU_SHADERFORMAT_MSL) {
        m_format = SDL_GPU_SHADERFORMAT_MSL;
        m_formatName = "MSL";
        m_subdirectory = "msl";
        m_extension = ".msl";
        // SPIRV-Cross renames the entry point when transpiling to Metal,
        // because `main` is reserved there.
        m_entrypoint = "main0";
    } else if (supported & SDL_GPU_SHADERFORMAT_DXIL) {
        m_format = SDL_GPU_SHADERFORMAT_DXIL;
        m_formatName = "DXIL";
        m_subdirectory = "dxil";
        m_extension = ".dxil";
        m_entrypoint = "main";
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "GPU backend supports no shader format we ship");
    }

    SDL_Log("Shader format: %s", m_formatName);
}

SDL_GPUShader* ShaderLoader::loadGraphics(const std::string& name, SDL_GPUShaderStage stage,
                                          const ShaderResources& resources) const {
    const std::filesystem::path path = paths::shader(std::string(m_subdirectory) + "/" + name + m_extension);

    size_t size = 0;
    void* code = readFile(path, size);

    if (code == nullptr) {
        return nullptr;
    }

    SDL_GPUShaderCreateInfo info{};
    info.code = static_cast<const Uint8*>(code);
    info.code_size = size;
    info.entrypoint = m_entrypoint;
    info.format = m_format;
    info.stage = stage;
    info.num_samplers = resources.samplers;
    info.num_storage_textures = resources.storageTextures;
    info.num_storage_buffers = resources.storageBuffers;
    info.num_uniform_buffers = resources.uniformBuffers;

    SDL_GPUShader* shader = SDL_CreateGPUShader(m_device, &info);
    SDL_free(code);

    if (shader == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_CreateGPUShader failed for '%s': %s", name.c_str(), SDL_GetError());
    }

    return shader;
}

SDL_GPUComputePipeline* ShaderLoader::loadCompute(const std::string& name, Uint32 samplers,
                                                  Uint32 readOnlyStorageTextures, Uint32 readOnlyStorageBuffers,
                                                  Uint32 readWriteStorageTextures, Uint32 readWriteStorageBuffers,
                                                  Uint32 uniformBuffers, Uint32 threadCountX, Uint32 threadCountY,
                                                  Uint32 threadCountZ) const {
    const std::filesystem::path path = paths::shader(std::string(m_subdirectory) + "/" + name + m_extension);

    size_t size = 0;
    void* code = readFile(path, size);

    if (code == nullptr) {
        return nullptr;
    }

    SDL_GPUComputePipelineCreateInfo info{};
    info.code = static_cast<const Uint8*>(code);
    info.code_size = size;
    info.entrypoint = m_entrypoint;
    info.format = m_format;
    info.num_samplers = samplers;
    info.num_readonly_storage_textures = readOnlyStorageTextures;
    info.num_readonly_storage_buffers = readOnlyStorageBuffers;
    info.num_readwrite_storage_textures = readWriteStorageTextures;
    info.num_readwrite_storage_buffers = readWriteStorageBuffers;
    info.num_uniform_buffers = uniformBuffers;
    info.threadcount_x = threadCountX;
    info.threadcount_y = threadCountY;
    info.threadcount_z = threadCountZ;

    SDL_GPUComputePipeline* pipeline = SDL_CreateGPUComputePipeline(m_device, &info);
    SDL_free(code);

    if (pipeline == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_GPU, "SDL_CreateGPUComputePipeline failed for '%s': %s", name.c_str(),
                     SDL_GetError());
    }

    return pipeline;
}

} // namespace horde::gfx
