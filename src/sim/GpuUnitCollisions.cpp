#include "sim/GpuUnitCollisions.hpp"

#include <SDL3/SDL_log.h>

#include <glm/glm.hpp>

#include <algorithm>

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
    Uint32 unitsSorted; // 1 when units are in grid order (GpuUnitReorder)
    glm::uvec2 wallBinDim;
    float wallBinSize;
    Uint32 wallCount;
    Uint32 solver; // 0 relaxed, 1 average, 2 chebyshev: matches SOLVER_* in collide_common.hlsli
    float averageOmega;
    float chebyshevOmega; // this pass's weight; 1 = a plain relaxed pass
    float smoothing;      // unit_smooth.comp
};

// The Chebyshev weight for each pass (Wang 2015, eq. 12): 1 for the first
// `delay` passes, then 2 / (2 - rho^2), then 4 / (4 - rho^2 * previous),
// rising toward 2 / (1 + sqrt(1 - rho^2)).
float chebyshevOmega(std::uint32_t pass, std::uint32_t delay, float rho, float previous) {
    if (pass < delay) {
        return 1.0f;
    }
    const float rhoSq = rho * rho;
    if (pass == delay) {
        return 2.0f / (2.0f - rhoSq);
    }
    return 4.0f / (4.0f - rhoSq * previous);
}
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

    // 6 ro buffers (units, grid A, grid B, 3 wall buffers), 1 rw buffer, 1 uniform buffer
    m_pipeline = services.shaders->loadCompute("unit_collide.comp", 0, 0, 6, 0, 1, 1, 256, 1, 1);
    if (m_pipeline == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuUnitCollisions: failed to load unit_collide.comp");
        return false;
    }
    if (m_settings.velocitySmoothing > 0.0f) {
        m_smoothPipeline = services.shaders->loadCompute("unit_smooth.comp", 0, 0, 6, 0, 1, 1, 256, 1, 1);
        if (m_smoothPipeline == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuUnitCollisions: failed to load unit_smooth.comp");
            return false;
        }
    }
    return true;
}

void GpuUnitCollisions::shutdown() {
    if (m_device && m_pipeline) {
        SDL_ReleaseGPUComputePipeline(m_device, m_pipeline);
        m_pipeline = nullptr;
    }
    if (m_device && m_smoothPipeline) {
        SDL_ReleaseGPUComputePipeline(m_device, m_smoothPipeline);
        m_smoothPipeline = nullptr;
    }
    m_device = nullptr;
}

namespace {

// What every collide-layout pass is given, before any per-pass weights.
GPUCollideUniforms collideUniforms(const CollisionSettings& settings, float diameter, glm::vec2 worldMax,
                                   std::uint32_t unitCount, const GpuSpatialGrid& grid, const GpuWalls& walls) {
    GPUCollideUniforms uniforms{};
    uniforms.worldMin = glm::vec2(0.0f);
    uniforms.worldMax = worldMax;
    uniforms.gridDim = grid.gridDim();
    uniforms.invCellSize = 1.0f / grid.cellSize();
    uniforms.unitCount = unitCount;
    uniforms.diameter = diameter;
    uniforms.relaxation = settings.relaxation;
    uniforms.maxCorrection = settings.maxCorrection * diameter;
    uniforms.unitsSorted = settings.reorderUnits ? 1u : 0u;
    uniforms.wallBinDim = walls.binDim();
    uniforms.wallBinSize = walls.binSize();
    uniforms.wallCount = walls.primitiveCount();
    uniforms.solver = static_cast<Uint32>(settings.solver);
    uniforms.averageOmega = settings.averageOmega;
    uniforms.chebyshevOmega = 1.0f;
    uniforms.smoothing = settings.velocitySmoothing;
    return uniforms;
}

} // namespace

std::uint32_t GpuUnitCollisions::smooth(SDL_GPUCommandBuffer* commands, SDL_GPUBuffer* const units[2],
                                        std::uint32_t current, std::uint32_t unitCount, const GpuSpatialGrid& grid,
                                        const GpuWalls& walls) const {
    if (m_smoothPipeline == nullptr || unitCount == 0) {
        return current;
    }
    const GPUCollideUniforms uniforms = collideUniforms(m_settings, m_diameter, m_worldMax, unitCount, grid, walls);
    SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
    const std::uint32_t next = 1 - current;
    SDL_GPUStorageBufferReadWriteBinding binding{};
    binding.buffer = units[next];
    if (SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &binding, 1)) {
        SDL_BindGPUComputePipeline(pass, m_smoothPipeline);
        SDL_GPUBuffer* readOnly[6] = {units[current],      grid.gridA(),        grid.gridB(),
                                      walls.buffers()[0], walls.buffers()[1], walls.buffers()[2]};
        SDL_BindGPUComputeStorageBuffers(pass, 0, readOnly, 6);
        SDL_DispatchGPUCompute(pass, (unitCount + 255) / 256, 1, 1);
        SDL_EndGPUComputePass(pass);
    }
    return next;
}

std::uint32_t GpuUnitCollisions::resolve(SDL_GPUCommandBuffer* commands, SDL_GPUBuffer* const units[2],
                                         std::uint32_t current, std::uint32_t unitCount, const GpuSpatialGrid& grid,
                                         const GpuWalls& walls) const {
    if (m_pipeline == nullptr || unitCount == 0) {
        return current;
    }

    GPUCollideUniforms uniforms = collideUniforms(m_settings, m_diameter, m_worldMax, unitCount, grid, walls);

    // The first pass can't extrapolate: the buffer it writes doesn't hold
    // this step's units yet. The second mustn't: it would extrapolate from
    // the predicted positions, before any pass has pushed units out of walls,
    // and bounce them off (see CollisionSettings::chebyshevDelay).
    const std::uint32_t delay = std::max<std::uint32_t>(m_settings.chebyshevDelay, kMinChebyshevDelay);

    for (std::uint32_t i = 0; i < m_settings.iterations; ++i) {
        const std::uint32_t next = 1 - current;

        if (m_settings.solver == CollisionSolver::Chebyshev) {
            uniforms.chebyshevOmega = chebyshevOmega(i, delay, m_settings.chebyshevRho, uniforms.chebyshevOmega);
        }
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
