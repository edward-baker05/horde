#include "sim/GpuUnitReorder.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <utility>

#include "gfx/GpuContext.hpp"
#include "gfx/ShaderLoader.hpp"
#include "scene/Scene.hpp"
#include "sim/GpuSpatialGrid.hpp"

namespace horde::sim {

namespace {

// Matches cbuffer ReorderParams in unit_reorder.comp.hlsl
struct alignas(16) GPUReorderUniforms {
    Uint32 unitCount;
    Uint32 stride; // words
    Uint32 renumberKeys;
    Uint32 padding;
};

} // namespace

GpuUnitReorder::~GpuUnitReorder() {
    shutdown();
}

bool GpuUnitReorder::init(scene::Services& services) {
    m_device = services.gpu->device();
    // 1 ro buffer (source), 2 rw buffers (sorted keys, target), 1 uniform buffer
    m_pipeline = services.shaders->loadCompute("unit_reorder.comp", 0, 0, 1, 0, 2, 1, 256, 1, 1);
    if (m_pipeline == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuUnitReorder: failed to load unit_reorder.comp");
        return false;
    }
    return true;
}

void GpuUnitReorder::shutdown() {
    if (m_device && m_pipeline) {
        SDL_ReleaseGPUComputePipeline(m_device, m_pipeline);
        m_pipeline = nullptr;
    }
    m_device = nullptr;
}

void GpuUnitReorder::reorder(SDL_GPUCommandBuffer* commands, GpuSpatialGrid& grid, std::uint32_t unitCount,
                             std::span<const Stream> streams) const {
    if (m_pipeline == nullptr || unitCount == 0) {
        return;
    }

    for (std::size_t i = 0; i < streams.size(); ++i) {
        const Stream& stream = streams[i];
        // The keys are only renumbered once every stream has used them.
        const GPUReorderUniforms uniforms{unitCount, stream.stride / 4, i + 1 == streams.size() ? 1u : 0u, 0};
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));

        SDL_GPUStorageBufferReadWriteBinding bindings[2]{};
        bindings[0].buffer = grid.gridA();
        bindings[1].buffer = *stream.spare;
        if (SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, bindings, 2)) {
            SDL_BindGPUComputePipeline(pass, m_pipeline);
            SDL_BindGPUComputeStorageBuffers(pass, 0, stream.live, 1);
            SDL_DispatchGPUCompute(pass, std::max<Uint32>(1, (unitCount + 255) / 256), 1, 1);
            SDL_EndGPUComputePass(pass);
        }
        std::swap(*stream.live, *stream.spare);
    }
}

} // namespace horde::sim
