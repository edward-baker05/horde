#include "sim/GpuCollidePhysics.hpp"

#include <SDL3/SDL_log.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>

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
    Uint32 fieldCount;
    Uint32 fieldPadding[3];
    // Two per region: (centre, half size), then (direction, strength, unused).
    glm::vec4 fields[kMaxFieldRegions * 2];
    Uint32 explosionCount;
    float flingDrag;
    float flingMaxSpeed;
    Uint32 explosionPadding;
    // Two per explosion: (centre, radius, this step's push at the centre),
    // then (falloff, fling 0 or 1, unused x2).
    glm::vec4 explosions[kMaxExplosions * 2];
};
static_assert(sizeof(GPUIntegrateUniforms) ==
              32 + 16 * kMaxFlowMarkers + 16 + 32 * kMaxFieldRegions + 16 + 32 * kMaxExplosions);

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

// Packs up to kMaxFieldRegions field regions, two vec4s each.
Uint32 packFields(const std::vector<FieldArea>& fields, glm::vec4* out) {
    const auto count = static_cast<Uint32>(std::min(fields.size(), kMaxFieldRegions));
    for (Uint32 i = 0; i < count; ++i) {
        out[i * 2] = glm::vec4(fields[i].center, fields[i].halfSize);
        out[i * 2 + 1] = glm::vec4(fields[i].direction, fields[i].strength, 0.0f);
    }
    return count;
}

// The share of an explosion's total push to apply in a step of `dt`, with
// `left` of it (1 to 0) still to come.
//
// Its strength over time s (0 to 1 of the duration) follows the decay curve
// (1 - s)^(k - 1), k = 1 constant, 2 linear, 3 quadratic; so the share given
// out by time s is 1 - (1 - s)^k, and `left` = (1 - s)^k says how far through
// it is. A shove also never moves a unit more than `maxStep` world units at
// the centre, where the push is largest, so a step's move stays within what
// unit_finalize's wall sweep checks. A push held back that way just carries
// on for longer.
float explosionShare(const Explosion& explosion, float left, float dt, float maxStep) {
    if (explosion.duration <= 0.0f) {
        return left;
    }
    const float k = static_cast<float>(explosion.decay) + 1.0f;
    const float now = 1.0f - std::pow(left, 1.0f / k);
    const float next = std::min(1.0f, now + dt / explosion.duration);
    float share = std::min(left, left - std::pow(1.0f - next, k));
    if (!explosion.fling && std::abs(explosion.strength) * share > maxStep) {
        share = maxStep / std::abs(explosion.strength);
    }
    return share;
}

// Furthest an explosion moves a unit in one step, in diameters: what
// unit_finalize's wall sweep covers (MAX_SWEEP_HOPS radii). Any further and
// a unit could skip through a thin wall.
constexpr float kMaxBlastStepDiameters = 32.0f;

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
    m_unitCount = static_cast<std::uint32_t>(config.initialUnits.size());
    m_capacity = m_unitCount + config.flow.toSpawn;
    m_config.initialUnits.clear(); // only needed for the upload below
    m_spawnRng.seed(config.flow.seed);
    m_spawnDue = 0.0;
    m_frame = 0;
    if (config.flow.spawns.size() > kMaxFlowMarkers || config.flow.exits.size() > kMaxFlowMarkers) {
        SDL_Log("GpuCollidePhysics: only the first %zu spawns and exits are used", kMaxFlowMarkers);
    }
    if (config.fieldRegions.size() > kMaxFieldRegions) {
        SDL_Log("GpuCollidePhysics: only the first %zu field regions are used", kMaxFieldRegions);
    }

    // unit_integrate.comp: 3 rw buffers (units, previous positions, fling), 1 uniform buffer
    m_integratePipeline = services.shaders->loadCompute("unit_integrate.comp", 0, 0, 0, 0, 3, 1, 256, 1, 1);
    // unit_finalize.comp: 4 ro buffers (previous positions, 3 wall buffers), 3 rw buffers (units, health, fling),
    // 1 uniform buffer
    m_finalizePipeline = services.shaders->loadCompute("unit_finalize.comp", 0, 0, 4, 0, 3, 1, 256, 1, 1);
    if (!m_integratePipeline || !m_finalizePipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuCollidePhysics: failed to load pipelines");
        return false;
    }

    // One cell per unit diameter, so the collision shader's 3x3 search is complete.
    const CollisionSettings& collision = m_config.collision;
    if (!m_renderer.init(services) ||
        !m_grid.init(services, m_capacity, config.worldSize, config.unitSize) ||
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

    SDL_GPUBufferCreateInfo flingInfo = prevInfo;
    flingInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    m_fling = SDL_CreateGPUBuffer(m_device, &flingInfo);

    if (!m_units[0] || !m_units[1] || !m_prevPositions || !m_health || !m_fling) {
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
    // Every unit, including those still to spawn, starts unflung.
    const std::vector<glm::vec2> fling(std::max<std::uint32_t>(m_capacity, 1), glm::vec2(0.0f));
    return uploadUnitsNow(m_device, config.initialUnits.data(), m_unitCount, m_units, 1) &&
           uploadBytesNow(m_device, health.data(), static_cast<std::uint32_t>(sizeof(float) * health.size()), m_health) &&
           uploadBytesNow(m_device, fling.data(), static_cast<std::uint32_t>(sizeof(glm::vec2) * fling.size()), m_fling);
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

    SDL_GPUBuffer** buffers[] = {&m_units[0], &m_units[1], &m_prevPositions, &m_health, &m_fling};
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

bool GpuCollidePhysics::addExplosion(const Explosion& explosion) {
    if (explosion.radius <= 0.0f || explosion.strength == 0.0f) {
        return false;
    }
    if (m_explosions.size() >= kMaxExplosions) {
        m_explosions.erase(m_explosions.begin());
    }
    m_explosions.push_back({explosion, 1.0f});
    return true;
}

std::vector<Explosion> GpuCollidePhysics::activeExplosions() const {
    std::vector<Explosion> out;
    out.reserve(m_explosions.size());
    for (const ActiveExplosion& active : m_explosions) {
        out.push_back(active.explosion);
    }
    return out;
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
        uniforms.fieldCount = packFields(m_config.fieldRegions, uniforms.fields);
        uniforms.explosionCount = static_cast<Uint32>(m_explosions.size());
        for (std::size_t i = 0; i < m_explosions.size(); ++i) {
            ActiveExplosion& active = m_explosions[i];
            const Explosion& e = active.explosion;
            const float share = explosionShare(e, active.left, m_dt, kMaxBlastStepDiameters * m_config.unitSize);
            active.left -= share;
            uniforms.explosions[i * 2] = glm::vec4(e.center, e.radius, e.strength * share);
            uniforms.explosions[i * 2 + 1] =
                glm::vec4(static_cast<float>(e.falloff), e.fling ? 1.0f : 0.0f, 0.0f, 0.0f);
        }
        uniforms.flingDrag = m_flingDrag;
        // The same limit as a shove's, per step.
        uniforms.flingMaxSpeed = kMaxBlastStepDiameters * m_config.unitSize / m_dt;
        std::erase_if(m_explosions, [](const ActiveExplosion& active) { return active.left <= 1e-5f; });
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));

        SDL_GPUStorageBufferReadWriteBinding bindings[3]{};
        bindings[0].buffer = m_units[m_current];
        bindings[1].buffer = m_prevPositions;
        bindings[2].buffer = m_fling;

        if (SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, bindings, 3)) {
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

        SDL_GPUStorageBufferReadWriteBinding bindings[3]{};
        bindings[0].buffer = m_units[m_current];
        bindings[1].buffer = m_health;
        bindings[2].buffer = m_fling;

        if (SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, bindings, 3)) {
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
    ImGui::Text("Broadphase: counting sort, %u x %u cells, %u passes", m_grid.gridDim().x, m_grid.gridDim().y,
                m_grid.passCount());
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
