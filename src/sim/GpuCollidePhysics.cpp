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
    float dt;
    Uint32 unitCount;
    float maxSpeed;
    float steering;
    glm::vec2 fallbackGoal;
    float radius;
    Uint32 exitCount;
    glm::vec4 exits[kMaxFlowMarkers]; // xy = centre, zw = half size
};
static_assert(sizeof(GPUIntegrateUniforms) == 32 + 16 * kMaxFlowMarkers);

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
    Uint32 exitCount;
    Uint32 spawnCount;
    Uint32 frame;
    Uint32 respawnOnExit;
    float exitDamage;
    Uint32 healthPadding[3];
    glm::vec4 exits[kMaxFlowMarkers];
    glm::vec4 spawns[kMaxFlowMarkers];
};
static_assert(sizeof(GPUFinalizeUniforms) == 80 + 32 * kMaxFlowMarkers);

// Packs up to kMaxFlowMarkers areas as (centre, half size).
Uint32 packAreas(const std::vector<MarkerArea>& areas, glm::vec4* out) {
    const auto count = static_cast<Uint32>(std::min(areas.size(), kMaxFlowMarkers));
    for (Uint32 i = 0; i < count; ++i) {
        out[i] = glm::vec4(areas[i].center, areas[i].halfSize);
    }
    return count;
}

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
    m_capacity = m_unitCount + config.flow.toSpawn;
    m_config.initialUnits.clear(); // only needed for the upload below
    m_spawnRng.seed(config.flow.seed);
    m_spawnDue = 0.0;
    m_frame = 0;
    if (config.flow.spawns.size() > kMaxFlowMarkers || config.flow.exits.size() > kMaxFlowMarkers) {
        SDL_Log("GpuCollidePhysics: only the first %zu spawns and exits are used", kMaxFlowMarkers);
    }

    // unit_integrate.comp: 2 rw buffers (units, previous positions), 1 uniform buffer
    m_integratePipeline = services.shaders->loadCompute("unit_integrate.comp", 0, 0, 0, 0, 2, 1, 256, 1, 1);
    // unit_finalize.comp: 4 ro buffers (previous positions, 3 wall buffers), 2 rw buffers (units, health), 1 uniform buffer
    m_finalizePipeline = services.shaders->loadCompute("unit_finalize.comp", 0, 0, 4, 0, 2, 1, 256, 1, 1);
    if (!m_integratePipeline || !m_finalizePipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuCollidePhysics: failed to load pipelines");
        return false;
    }

    // One cell per unit diameter, so the collision shader's 3x3 search is complete.
    const CollisionSettings& collision = m_config.collision;
    if (!m_renderer.init(services) ||
        !m_grid.init(services, m_capacity, config.worldSize, config.unitSize, collision.broadphase,
                     collision.bucketCapacity) ||
        !m_collisions.init(services, collision, config.unitSize, config.worldSize) ||
        !m_walls.init(m_device, config.walls.get())) {
        return false;
    }

    const SDL_GPUBufferUsageFlags usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
                                          SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                                          SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    m_units[0] = createUnitBuffer(m_device, m_capacity, usage);
    m_units[1] = createUnitBuffer(m_device, m_capacity, usage);

    // Both are read by unit.vert when drawing, as well as by compute.
    SDL_GPUBufferCreateInfo prevInfo{};
    prevInfo.usage = usage;
    prevInfo.size = static_cast<Uint32>(sizeof(glm::vec2) * std::max<std::uint32_t>(m_capacity, 1));
    m_prevPositions = SDL_CreateGPUBuffer(m_device, &prevInfo);

    SDL_GPUBufferCreateInfo healthInfo{};
    healthInfo.usage = usage;
    healthInfo.size = static_cast<Uint32>(sizeof(float) * std::max<std::uint32_t>(m_capacity, 1));
    m_health = SDL_CreateGPUBuffer(m_device, &healthInfo);

    if (!m_units[0] || !m_units[1] || !m_prevPositions || !m_health) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuCollidePhysics: failed to create buffers: %s",
                     SDL_GetError());
        return false;
    }

    if (config.flow.toSpawn > 0 && !config.flow.spawns.empty()) {
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = static_cast<Uint32>(sizeof(BaseUnitData) * config.flow.toSpawn);
        m_spawnTransfer = SDL_CreateGPUTransferBuffer(m_device, &transferInfo);
        transferInfo.size = static_cast<Uint32>(sizeof(float) * config.flow.toSpawn);
        m_spawnHealthTransfer = SDL_CreateGPUTransferBuffer(m_device, &transferInfo);
        if (m_spawnTransfer == nullptr || m_spawnHealthTransfer == nullptr) {
            SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuCollidePhysics: failed to create spawn buffer: %s",
                         SDL_GetError());
            return false;
        }
    }

    m_current = 0;
    const std::vector<float> health = startingHealth(m_unitCount);
    return uploadUnitsNow(m_device, config.initialUnits.data(), m_unitCount, m_units, 1) &&
           uploadBytesNow(m_device, health.data(), static_cast<std::uint32_t>(sizeof(float) * health.size()), m_health);
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

    for (SDL_GPUTransferBuffer** transfer : {&m_spawnTransfer, &m_spawnHealthTransfer}) {
        if (*transfer) {
            SDL_ReleaseGPUTransferBuffer(m_device, *transfer);
            *transfer = nullptr;
        }
    }

    SDL_GPUBuffer** buffers[] = {&m_units[0], &m_units[1], &m_prevPositions, &m_health};
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

std::vector<float> GpuCollidePhysics::startingHealth(std::uint32_t count) const {
    return std::vector<float>(count, static_cast<float>(m_config.health.maxHp));
}

void GpuCollidePhysics::spawnPending(SDL_GPUCommandBuffer* commands) {
    if (m_spawnTransfer == nullptr || m_unitCount >= m_capacity) {
        return;
    }

    // A rate of 0 brings everyone in at once; otherwise whole units as they fall due.
    const FlowSettings& flow = m_config.flow;
    std::uint32_t count = m_capacity - m_unitCount;
    if (flow.spawnRate > 0.0f) {
        m_spawnDue += static_cast<double>(flow.spawnRate) * m_dt;
        count = std::min(count, static_cast<std::uint32_t>(m_spawnDue));
        m_spawnDue -= count;
    }
    if (count == 0) {
        return;
    }

    const std::vector<BaseUnitData> units = spawnInAreas(count, flow.spawns, m_config.worldSize, m_config.unitSize,
                                                         m_spawnRng, m_config.walls.get());
    const Uint32 size = static_cast<Uint32>(sizeof(BaseUnitData) * count);

    void* mapped = SDL_MapGPUTransferBuffer(m_device, m_spawnTransfer, true);
    SDL_memcpy(mapped, units.data(), size);
    SDL_UnmapGPUTransferBuffer(m_device, m_spawnTransfer);

    const std::vector<float> health = startingHealth(count);
    const Uint32 healthSize = static_cast<Uint32>(sizeof(float) * count);
    void* healthMapped = SDL_MapGPUTransferBuffer(m_device, m_spawnHealthTransfer, true);
    SDL_memcpy(healthMapped, health.data(), healthSize);
    SDL_UnmapGPUTransferBuffer(m_device, m_spawnHealthTransfer);

    if (SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands)) {
        SDL_GPUTransferBufferLocation src{};
        src.transfer_buffer = m_spawnTransfer;
        // Appends after the live units. Not cycled: the rest of the buffer is live data.
        SDL_GPUBufferRegion dst{};
        dst.buffer = m_units[m_current];
        dst.offset = static_cast<Uint32>(sizeof(BaseUnitData) * m_unitCount);
        dst.size = size;
        SDL_UploadToGPUBuffer(copy, &src, &dst, false);

        SDL_GPUTransferBufferLocation healthSrc{};
        healthSrc.transfer_buffer = m_spawnHealthTransfer;
        SDL_GPUBufferRegion healthDst{};
        healthDst.buffer = m_health;
        healthDst.offset = static_cast<Uint32>(sizeof(float) * m_unitCount);
        healthDst.size = healthSize;
        SDL_UploadToGPUBuffer(copy, &healthSrc, &healthDst, false);
        SDL_EndGPUCopyPass(copy);
    }

    m_unitCount += count;
}

void GpuCollidePhysics::compute(SDL_GPUCommandBuffer* commands) {
    if (!m_finalizePipeline || m_dt <= 0.0f) {
        return;
    }

    // 0. Bring in whoever is due.
    spawnPending(commands);
    if (m_unitCount == 0) {
        return;
    }
    ++m_frame;

    const glm::vec2 worldMax = glm::max(m_config.worldSize - glm::vec2(m_config.unitSize), glm::vec2(0.0f));
    const float radius = m_config.unitSize * 0.5f;

    // 1. Steer and predict.
    {
        GPUIntegrateUniforms uniforms{};
        uniforms.dt = m_dt;
        uniforms.unitCount = m_unitCount;
        uniforms.maxSpeed = m_config.movement.maxSpeed;
        uniforms.steering = m_config.movement.steering;
        uniforms.fallbackGoal = m_config.movement.goal + glm::vec2(radius);
        uniforms.radius = radius;
        uniforms.exitCount = packAreas(m_config.flow.exits, uniforms.exits);
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
        uniforms.radius = radius;
        uniforms.wallBinDim = m_walls.binDim();
        uniforms.wallBinSize = m_walls.binSize();
        uniforms.wallCount = m_walls.primitiveCount();
        uniforms.exitCount = packAreas(m_config.flow.exits, uniforms.exits);
        uniforms.spawnCount = packAreas(m_config.flow.spawns, uniforms.spawns);
        uniforms.frame = m_frame;
        uniforms.respawnOnExit = m_config.flow.respawnAtExit ? 1u : 0u;
        uniforms.exitDamage = m_config.health.exitDamage;
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));

        SDL_GPUStorageBufferReadWriteBinding bindings[2]{};
        bindings[0].buffer = m_units[m_current];
        bindings[1].buffer = m_health;

        if (SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, bindings, 2)) {
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
                                   const glm::mat4& viewProjection, float alpha) {
    // m_prevPositions holds where each unit started the latest step, which
    // unit_integrate writes before moving it: exactly the previous state.
    gfx::UnitRenderer::DrawOptions options;
    options.previous = m_frame > 0 ? m_prevPositions : nullptr;
    options.alpha = alpha;
    // Further than a unit could move in one step is a respawn, not motion.
    options.maxJump = std::max(4.0f * m_config.unitSize, 2.0f * m_config.movement.maxSpeed * m_dt);
    options.health = m_health;
    options.maxHealth = static_cast<float>(m_config.health.maxHp);
    m_renderer.draw(commands, pass, m_units[m_current], m_unitCount, viewProjection, m_config.unitSize, options);
}

void GpuCollidePhysics::debugUi() {
    const CollisionSettings& c = m_config.collision;
    ImGui::Text("Broadphase: %s, %u x %u cells, %u passes", broadphaseName(c.broadphase), m_grid.gridDim().x,
                m_grid.gridDim().y, m_grid.passCount());
    if (c.solver == CollisionSolver::Average) {
        ImGui::Text("Collision: %u Jacobi passes, average solver (omega %.2f)", c.iterations, c.averageOmega);
    } else {
        ImGui::Text("Collision: %u Jacobi passes, relaxed solver (%.2f)", c.iterations, c.relaxation);
    }
    ImGui::Text("Walls: %u primitives", m_walls.primitiveCount());
    if (m_unitCount < m_capacity) {
        ImGui::Text("Spawning: %u / %u at %.0f/s", m_unitCount, m_capacity, m_config.flow.spawnRate);
    }
    ImGui::Text("Heading for %zu exit(s) at %.0f u/s%s", m_config.flow.exits.size(), m_config.movement.maxSpeed,
                m_config.flow.respawnAtExit ? ", respawning" : "");
    ImGui::Text("Max HP %u, %.1f damage per exit", m_config.health.maxHp, m_config.health.exitDamage);
}

bool GpuCollidePhysics::readHealth(std::vector<float>& out) {
    out.resize(m_unitCount);
    if (m_device == nullptr) {
        return false;
    }
    if (m_unitCount == 0) {
        return true;
    }

    const Uint32 size = static_cast<Uint32>(sizeof(float) * m_unitCount);
    SDL_GPUTransferBufferCreateInfo info{};
    info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    info.size = size;
    SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(m_device, &info);
    if (transfer == nullptr) {
        return false;
    }

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(m_device);
    SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands);
    SDL_GPUBufferRegion src{};
    src.buffer = m_health;
    src.size = size;
    SDL_GPUTransferBufferLocation dst{};
    dst.transfer_buffer = transfer;
    SDL_DownloadFromGPUBuffer(copy, &src, &dst);
    SDL_EndGPUCopyPass(copy);

    SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
    const bool ok = fence != nullptr && SDL_WaitForGPUFences(m_device, true, &fence, 1);
    if (fence) {
        SDL_ReleaseGPUFence(m_device, fence);
    }
    if (ok) {
        SDL_memcpy(out.data(), SDL_MapGPUTransferBuffer(m_device, transfer, false), size);
        SDL_UnmapGPUTransferBuffer(m_device, transfer);
    }
    SDL_ReleaseGPUTransferBuffer(m_device, transfer);
    return ok;
}

bool GpuCollidePhysics::readUnits(std::vector<BaseUnitData>& out) {
    return m_device && downloadUnits(m_device, m_units[m_current], m_unitCount, out);
}

} // namespace horde::sim
