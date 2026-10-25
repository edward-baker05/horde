#include "sim/GpuCollidePhysics.hpp"

#include <SDL3/SDL_log.h>

#include <imgui.h>

#include <algorithm>

#include "gfx/GpuContext.hpp"
#include "gfx/ShaderLoader.hpp"
#include "scene/Scene.hpp"
#include "sim/UnitBuffers.hpp"

namespace horde::sim {

namespace {

// Matches cbuffer IntegrateParams in unit_integrate.comp.hlsl
struct alignas(16) GPUIntegrateUniforms {
    glm::vec2 goal;
    float dt;
    Uint32 unitCount;
    float maxSpeed;
    float steering;
    glm::vec2 padding;
};

// Matches cbuffer FinalizeParams in unit_finalize.comp.hlsl
struct alignas(16) GPUFinalizeUniforms {
    glm::vec2 worldMin;
    glm::vec2 worldMax;
    float invDt;
    Uint32 unitCount;
    float maxSpeed;
    float radius;
    glm::uvec2 wallBinDim;
    float wallBinSize;
    Uint32 wallCount;
};
static_assert(sizeof(GPUFinalizeUniforms) == 48);

Uint32 groups(Uint32 threads) {
    return std::max<Uint32>(1, (threads + 255) / 256);
}

} // namespace

GpuCollidePhysics::~GpuCollidePhysics() {
    shutdown();
}

bool GpuCollidePhysics::init(scene::Services& services, const SimConfig& config) {
    m_device = services.gpu->device();
    m_config = config;
    if (m_forcedBroadphase) {
        m_config.collision.broadphase = *m_forcedBroadphase;
    }
    m_unitCount = static_cast<std::uint32_t>(config.initialUnits.size());
    m_config.initialUnits.clear(); // only needed for the upload below

    // unit_integrate.comp: 2 rw buffers (units, previous positions), 1 uniform buffer
    m_integratePipeline = services.shaders->loadCompute("unit_integrate.comp", 0, 0, 0, 0, 2, 1, 256, 1, 1);
    // unit_finalize.comp: 4 ro buffers (previous positions, 3 wall buffers), 1 rw buffer (units), 1 uniform buffer
    m_finalizePipeline = services.shaders->loadCompute("unit_finalize.comp", 0, 0, 4, 0, 1, 1, 256, 1, 1);
    if (!m_integratePipeline || !m_finalizePipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuCollidePhysics: failed to load pipelines");
        return false;
    }

    // One cell per unit diameter, so the collision shader's 3x3 search is complete.
    const CollisionSettings& collision = m_config.collision;
    if (!m_renderer.init(services) ||
        !m_grid.init(services, m_unitCount, config.worldSize, config.unitSize, collision.broadphase,
                     collision.bucketCapacity) ||
        !m_collisions.init(services, collision, config.unitSize, config.worldSize) ||
        !m_walls.init(m_device, config.walls.get())) {
        return false;
    }

    const SDL_GPUBufferUsageFlags usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
                                          SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                                          SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    m_units[0] = createUnitBuffer(m_device, m_unitCount, usage);
    m_units[1] = createUnitBuffer(m_device, m_unitCount, usage);

    SDL_GPUBufferCreateInfo prevInfo{};
    prevInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    prevInfo.size = static_cast<Uint32>(sizeof(glm::vec2) * std::max<std::uint32_t>(m_unitCount, 1));
    m_prevPositions = SDL_CreateGPUBuffer(m_device, &prevInfo);

    if (!m_units[0] || !m_units[1] || !m_prevPositions) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuCollidePhysics: failed to create buffers: %s",
                     SDL_GetError());
        return false;
    }

    m_current = 0;
    return uploadUnitsNow(m_device, config.initialUnits.data(), m_unitCount, m_units, 1);
}

void GpuCollidePhysics::shutdown() {
    m_walls.shutdown();
    m_collisions.shutdown();
    m_grid.shutdown();
    m_renderer.shutdown();

    if (m_device == nullptr) {
        return;
    }

    SDL_GPUComputePipeline** pipelines[] = {&m_integratePipeline, &m_finalizePipeline};
    for (SDL_GPUComputePipeline** pipeline : pipelines) {
        if (*pipeline) {
            SDL_ReleaseGPUComputePipeline(m_device, *pipeline);
            *pipeline = nullptr;
        }
    }

    SDL_GPUBuffer** buffers[] = {&m_units[0], &m_units[1], &m_prevPositions};
    for (SDL_GPUBuffer** buffer : buffers) {
        if (*buffer) {
            SDL_ReleaseGPUBuffer(m_device, *buffer);
            *buffer = nullptr;
        }
    }

    m_device = nullptr;
}

void GpuCollidePhysics::update(float dt) {
    m_dt = dt;
}

void GpuCollidePhysics::compute(SDL_GPUCommandBuffer* commands) {
    if (!m_finalizePipeline || m_unitCount == 0 || m_dt <= 0.0f) {
        return;
    }

    const glm::vec2 worldMax = glm::max(m_config.worldSize - glm::vec2(m_config.unitSize), glm::vec2(0.0f));

    // 1. Steer and predict.
    {
        GPUIntegrateUniforms uniforms{};
        uniforms.goal = m_config.movement.goal;
        uniforms.dt = m_dt;
        uniforms.unitCount = m_unitCount;
        uniforms.maxSpeed = m_config.movement.maxSpeed;
        uniforms.steering = m_config.movement.steering;
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));

        SDL_GPUStorageBufferReadWriteBinding bindings[2]{};
        bindings[0].buffer = m_units[m_current];
        bindings[1].buffer = m_prevPositions;

        if (SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, bindings, 2)) {
            SDL_BindGPUComputePipeline(pass, m_integratePipeline);
            SDL_DispatchGPUCompute(pass, groups(m_unitCount), 1, 1);
            SDL_EndGPUComputePass(pass);
        }
    }

    // 2. Broadphase over the predicted positions.
    m_grid.build(commands, m_units[m_current], m_unitCount);

    // 3. Separate overlapping units.
    m_current = m_collisions.resolve(commands, m_units, m_current, m_unitCount, m_grid, m_walls);

    // 4. Bounds and velocity.
    {
        GPUFinalizeUniforms uniforms{};
        uniforms.worldMin = glm::vec2(0.0f);
        uniforms.worldMax = worldMax;
        uniforms.invDt = 1.0f / m_dt;
        uniforms.unitCount = m_unitCount;
        uniforms.maxSpeed = m_config.movement.maxSpeed;
        uniforms.radius = m_config.unitSize * 0.5f;
        uniforms.wallBinDim = m_walls.binDim();
        uniforms.wallBinSize = m_walls.binSize();
        uniforms.wallCount = m_walls.primitiveCount();
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));

        SDL_GPUStorageBufferReadWriteBinding binding{};
        binding.buffer = m_units[m_current];

        if (SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &binding, 1)) {
            SDL_BindGPUComputePipeline(pass, m_finalizePipeline);
            SDL_GPUBuffer* readOnly[4] = {m_prevPositions, m_walls.buffers()[0], m_walls.buffers()[1],
                                          m_walls.buffers()[2]};
            SDL_BindGPUComputeStorageBuffers(pass, 0, readOnly, 4);
            SDL_DispatchGPUCompute(pass, groups(m_unitCount), 1, 1);
            SDL_EndGPUComputePass(pass);
        }
    }
}

void GpuCollidePhysics::renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                                   const glm::mat4& viewProjection) {
    m_renderer.draw(commands, pass, m_units[m_current], m_unitCount, viewProjection, m_config.unitSize);
}

void GpuCollidePhysics::debugUi() {
    const CollisionSettings& c = m_config.collision;
    ImGui::Text("Broadphase: %s, %u x %u cells, %u passes", broadphaseName(c.broadphase), m_grid.gridDim().x,
                m_grid.gridDim().y, m_grid.passCount());
    if (c.solver == CollisionSolver::Average) {
        ImGui::Text("Collision: %u Jacobi passes, average solver", c.iterations);
    } else {
        ImGui::Text("Collision: %u Jacobi passes, relaxed solver (%.2f)", c.iterations, c.relaxation);
    }
    ImGui::Text("Walls: %u primitives", m_walls.primitiveCount());
    ImGui::Text("Goal: (%.0f, %.0f) at %.0f u/s", m_config.movement.goal.x, m_config.movement.goal.y,
                m_config.movement.maxSpeed);
}

bool GpuCollidePhysics::readUnits(std::vector<BaseUnitData>& out) {
    return m_device && downloadUnits(m_device, m_units[m_current], m_unitCount, out);
}

} // namespace horde::sim
