#include "sim/GpuUnitCollisions.hpp"

#include <SDL3/SDL_log.h>

#include <glm/glm.hpp>

#include "gfx/GpuContext.hpp"
#include "gfx/ShaderLoader.hpp"
#include "scene/Scene.hpp"
#include "sim/GpuSpatialGrid.hpp"
#include "sim/GpuWalls.hpp"

namespace horde::sim {

namespace {

// Matches cbuffer CollideParams in collide_common.hlsli
struct alignas(16) GPUCollideUniforms {
    glm::vec2 worldMin;
    glm::vec2 worldMax;
    glm::uvec2 gridDim;
    float invCellSize;
    Uint32 unitCount;
    float diameter;
    float relaxation;
    float maxCorrection;
    Uint32 bucketCapacity;
    glm::uvec2 wallBinDim;
    float wallBinSize;
    Uint32 wallCount;
    Uint32 solver; // 0 relaxed, 1 average: matches SOLVER_* in collide_common.hlsli
    float averageOmega;
    Uint32 solverPadding[2];
};
static_assert(sizeof(GPUCollideUniforms) == 80);

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

    // Both variants: 6 ro buffers (units, grid A, grid B, 3 wall buffers), 1 rw buffer, 1 uniform buffer
    const char* shader = settings.broadphase == Broadphase::Buckets ? "unit_collide_buckets.comp" : "unit_collide.comp";
    m_pipeline = services.shaders->loadCompute(shader, 0, 0, 6, 0, 1, 1, 256, 1, 1);
    if (m_pipeline == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuUnitCollisions: failed to load %s", shader);
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
                                         std::uint32_t current, std::uint32_t unitCount, const GpuSpatialGrid& grid,
                                         const GpuWalls& walls) const {
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
    uniforms.bucketCapacity = grid.bucketCapacity();
    uniforms.wallBinDim = walls.binDim();
    uniforms.wallBinSize = walls.binSize();
    uniforms.wallCount = walls.primitiveCount();
    uniforms.solver = m_settings.solver == CollisionSolver::Average ? 1u : 0u;
    uniforms.averageOmega = m_settings.averageOmega;

    for (std::uint32_t i = 0; i < m_settings.iterations; ++i) {
        const std::uint32_t next = 1 - current;

        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));

        SDL_GPUStorageBufferReadWriteBinding binding{};
        binding.buffer = units[next];

        if (SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &binding, 1)) {
            SDL_BindGPUComputePipeline(pass, m_pipeline);
            SDL_GPUBuffer* readOnly[6] = {units[current],      grid.gridA(),        grid.gridB(),
                                          walls.buffers()[0], walls.buffers()[1], walls.buffers()[2]};
            SDL_BindGPUComputeStorageBuffers(pass, 0, readOnly, 6);
            SDL_DispatchGPUCompute(pass, (unitCount + 255) / 256, 1, 1);
            SDL_EndGPUComputePass(pass);
        }

        current = next;
    }

    return current;
}

} // namespace horde::sim
