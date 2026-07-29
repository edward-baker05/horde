#pragma once

#include <SDL3/SDL_gpu.h>

#include <string>

namespace horde::gfx {

// Resource counts a shader declares. These must match what the HLSL actually
// binds — SDL_GPU cannot introspect the binary, and a mismatch is a validation
// error or, worse, silent corruption.
struct ShaderResources {
    Uint32 samplers = 0;
    Uint32 storageTextures = 0;
    Uint32 storageBuffers = 0;
    Uint32 uniformBuffers = 0;
};

// Loads compiled shaders from the format-specific directory next to the
// executable, chosen from what the running SDL_GPU backend supports:
//
//   shaders/spirv/<name>.spv   (Vulkan)
//   shaders/msl/<name>.msl     (Metal)
//   shaders/dxil/<name>.dxil   (D3D12)
//
// `name` is the stem without an extension, e.g. "sprite.vert".
class ShaderLoader {
public:
    explicit ShaderLoader(SDL_GPUDevice* device);

    // Returns nullptr and logs on failure. The caller owns the result and must
    // release it with SDL_ReleaseGPUShader.
    SDL_GPUShader* loadGraphics(const std::string& name, SDL_GPUShaderStage stage,
                                const ShaderResources& resources) const;

    // Returns nullptr and logs on failure. The caller owns the result and must
    // release it with SDL_ReleaseGPUComputePipeline.
    //
    // Compute splits storage resources into read-only and read-write sets, so
    // it takes its counts directly rather than through ShaderResources.
    SDL_GPUComputePipeline* loadCompute(const std::string& name, Uint32 samplers, Uint32 readOnlyStorageTextures,
                                        Uint32 readOnlyStorageBuffers, Uint32 readWriteStorageTextures,
                                        Uint32 readWriteStorageBuffers, Uint32 uniformBuffers, Uint32 threadCountX,
                                        Uint32 threadCountY, Uint32 threadCountZ) const;

    // Which format directory this loader resolved to, for logging.
    const char* formatName() const {
        return m_formatName;
    }

private:
    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUShaderFormat m_format = SDL_GPU_SHADERFORMAT_INVALID;
    const char* m_formatName = "none";
    const char* m_subdirectory = "";
    const char* m_extension = "";
    const char* m_entrypoint = "main";
};

} // namespace horde::gfx
