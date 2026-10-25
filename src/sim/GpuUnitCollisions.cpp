#include "sim/GpuUnitCollisions.hpp"

#include <SDL3/SDL_log.h>

#include <glm/glm.hpp>

#include "gfx/GpuContext.hpp"
#include "gfx/ShaderLoader.hpp"
#include "scene/Scene.hpp"
#include "sim/GpuSpatialGrid.hpp"

namespace horde::sim {

namespace {

// Matches cbuffer CollideParams in unit_collide.comp.hlsl
struct alignas(16) GPUCollideUniforms {
    glm::vec2 worldMin;
    glm::vec2 worldMax;
    glm::uvec2 gridDim;
    float invCellSize;
    Uint32 unitCount;
    float diameter;
    float relaxation;
    float maxCorrection;
    float padding;
};

} // namespace

GpuUnitCollisions::~GpuUnitCollisions() {
    shutdown();
}

bool GpuUnitCollisions::init(scene::Services& services, const CollisionSettings& settings, float diameter,
                             glm::vec2 worldSize) {
    m_device = services.gpu->device();
    m_settings = settings;
    m_diameter = diameter;
    // Positions are a unit's top-left corner, so the far edges sit one size in.
    m_worldMax = glm::max(worldSize - glm::vec2(diameter), glm::vec2(0.0f));

    // unit_collide.comp: 3 ro buffers (units, keys, ranges), 1 rw buffer, 1 uniform buffer
    m_pipeline = services.shaders->loadCompute("unit_collide.comp", 0, 0, 3, 0, 1, 1, 256, 1, 1);
    if (m_pipeline == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuUnitCollisions: failed to load unit_collide.comp");
        return false;
    }
    return true;
}

void GpuUnitCollisions::shutdown() {
    if (m_device && m_pipeline) {
        SDL_ReleaseGPUComputePipeline(m_device, m_pipeline);
        m_pipeline = nullptr;
    }
    m_device = nullptr;
}

std::uint32_t GpuUnitCollisions::resolve(SDL_GPUCommandBuffer* commands, SDL_GPUBuffer* const units[2],
                                         std::uint32_t current, std::uint32_t unitCount,
                                         const GpuSpatialGrid& grid) const {
    if (m_pipeline == nullptr || unitCount == 0) {
        return current;
    }

    GPUCollideUniforms uniforms{};
    uniforms.worldMin = glm::vec2(0.0f);
    uniforms.worldMax = m_worldMax;
    uniforms.gridDim = grid.gridDim();
    uniforms.invCellSize = 1.0f / grid.cellSize();
    uniforms.unitCount = unitCount;
    uniforms.diameter = m_diameter;
    uniforms.relaxation = m_settings.relaxation;
    uniforms.maxCorrection = m_settings.maxCorrection * m_diameter;

    for (std::uint32_t i = 0; i < m_settings.iterations; ++i) {
        const std::uint32_t next = 1 - current;

        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));

        SDL_GPUStorageBufferReadWriteBinding binding{};
        binding.buffer = units[next];

        if (SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &binding, 1)) {
            SDL_BindGPUComputePipeline(pass, m_pipeline);
            SDL_GPUBuffer* readOnly[3] = {units[current], grid.sortedKeys(), grid.cellRanges()};
            SDL_BindGPUComputeStorageBuffers(pass, 0, readOnly, 3);
            SDL_DispatchGPUCompute(pass, (unitCount + 255) / 256, 1, 1);
            SDL_EndGPUComputePass(pass);
        }

        current = next;
    }

    return current;
}

} // namespace horde::sim
