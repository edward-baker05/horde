#include "sim/GpuCollidePhysics.hpp"

#include <SDL3/SDL_log.h>

#include <glm/trigonometric.hpp>
#include <imgui.h>

#include <algorithm>
#include <cmath>

#include "core/PolishMode.hpp"
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
    // then (falloff, fling 0 or 1, this step's damage, status table index).
    glm::vec4 explosions[kMaxExplosions * 2];
    float time; // seconds of simulation
    float wobbleAngle;
    float wobbleRate;
    Uint32 congestionEnabled; // nonzero once the grid has been built
    glm::uvec2 gridDim;
    float invCellSize;
    float congestionStart;
    float congestionStop;
    Uint32 congestionPadding[3];
    Uint32 turretCount;
    Uint32 turretPadding[3];
    // Two per turret effect, as TurretEffects::step writes them.
    glm::vec4 turrets[logic::kMaxTurrets * 2];
};

static_assert(sizeof(GPUIntegrateUniforms) == 32 + 16 * kMaxFlowMarkers + 16 + 32 * kMaxFieldRegions + 16 +
                                                  32 * kMaxExplosions + 48 + 16 + 32 * logic::kMaxTurrets);
// SDL_GPU's uniform buffers hold 32 KiB per slot.
static_assert(sizeof(GPUIntegrateUniforms) <= 32768);

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
    Uint32 exitMode; // ExitBehaviour
    float exitDamage;
    Uint32 healthPadding[3];
    glm::vec4 exits[kMaxFlowMarkers];
    glm::vec4 spawns[kMaxFlowMarkers];
    // Per turret slot: TurretEffects::packContacts.
    glm::vec4 blockerContacts[logic::kMaxTurrets];
};

static_assert(sizeof(GPUFinalizeUniforms) == 80 + 32 * kMaxFlowMarkers + 16 * logic::kMaxTurrets);

// Matches cbuffer AimParams in turret_aim.comp.hlsl
struct alignas(16) GPUAimUniforms {
    glm::uvec2 gridDim;
    float cellSize;
    Uint32 requestCount;
    glm::vec4 requests[logic::kMaxTurrets];    // centre, range, unused
    glm::uvec4 requestIds[logic::kMaxTurrets]; // slot, shot ID, unused x2
};

static_assert(sizeof(GPUAimUniforms) == 16 + 32 * logic::kMaxTurrets);

// Matches cbuffer FireParams in turret_fire.comp.hlsl
struct alignas(16) GPUFireUniforms {
    glm::uvec2 gridDim;
    float cellSize;
    Uint32 shotCount;
    float unitRadius;
    float bulletRadius;
    float spread;
    Uint32 seed;
    glm::vec4 shotAim[logic::kMaxTurrets];  // centre, range, speed
    glm::vec4 shotHit[logic::kMaxTurrets];  // damage, life, unused x2
    glm::uvec4 shotIds[logic::kMaxTurrets]; // pool slot, pierce, status, unused
};

static_assert(sizeof(GPUFireUniforms) == 32 + 48 * logic::kMaxTurrets);

// Matches cbuffer BulletStepParams in bullet_step.comp.hlsl
struct alignas(16) GPUBulletStepUniforms {
    float dt;
    Uint32 bulletCount;
    float unitRadius;
    float cellSize;
    glm::uvec2 gridDim;
    glm::vec2 worldSize;
    glm::uvec2 wallBinDim;
    float wallBinSize;
    Uint32 wallCount;
};

static_assert(sizeof(GPUBulletStepUniforms) == 48);

// Matches BulletParams in bullets.hlsli
struct GPUBulletParams {
    float damage;
    float life;
    Uint32 pierce;
    Uint32 status;
    Uint32 lastHit;
    float radius;
    Uint32 clear;
    Uint32 padding;
};

static_assert(sizeof(GPUBulletParams) == 32);

// Bullets are drawn and hit as discs this wide, in world units.
constexpr float kBulletSize = 2.0f;
// Radians either side of where a gun aims that its bullets may stray.
constexpr float kBulletSpread = 0.03f;

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
    m_baseWalls = config.walls;
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

    // unit_integrate.comp: 3 ro buffers (IDs, cell ranges, status table), 6 rw buffers (units, previous positions,
    // fling, health, feedback, status), 1 uniform buffer
    m_integratePipeline = services.shaders->loadCompute("unit_integrate.comp", 0, 0, 3, 0, 6, 1, 256, 1, 1);
    // unit_finalize.comp: 4 ro buffers (previous positions, 3 wall buffers), 5 rw buffers (units, health, fling,
    // feedback, pending damage), 1 uniform buffer
    m_finalizePipeline = services.shaders->loadCompute("unit_finalize.comp", 0, 0, 4, 0, 5, 1, 256, 1, 1);
    // turret_aim.comp: 1 ro buffer (cell ranges), 1 rw buffer (feedback), 1 uniform buffer
    m_aimPipeline = services.shaders->loadCompute("turret_aim.comp", 0, 0, 1, 0, 1, 1, 256, 1, 1);
    // turret_fire.comp: 3 ro buffers (cell ranges, sorted keys, units), 3 rw buffers (bullets, bullet previous
    // positions, bullet params), 1 uniform buffer
    m_firePipeline = services.shaders->loadCompute("turret_fire.comp", 0, 0, 3, 0, 3, 1, 64, 1, 1);
    // bullet_step.comp: 8 ro buffers (3 wall buffers, cell ranges, sorted keys, units, IDs, status table), 5 rw
    // buffers (bullets, bullet previous positions, bullet params, pending damage, status), 1 uniform buffer
    m_bulletPipeline = services.shaders->loadCompute("bullet_step.comp", 0, 0, 8, 0, 5, 1, 256, 1, 1);
    if (!m_integratePipeline || !m_finalizePipeline || !m_aimPipeline || !m_firePipeline || !m_bulletPipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuCollidePhysics: failed to load pipelines");
        return false;
    }

    // One cell per unit diameter, so the collision shader's 3x3 search is complete.
    const CollisionSettings& collision = m_config.collision;
    if (!m_renderer.init(services) || !m_grid.init(services, m_capacity, config.worldSize, config.unitSize) ||
        !m_collisions.init(services, collision, config.unitSize, config.worldSize) ||
        (collision.reorderUnits && !m_reorder.init(services)) || !m_walls.init(m_device, config.walls.get())) {
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

    // Read by unit.vert too, to find unit 0.
    SDL_GPUBufferCreateInfo idsInfo = healthInfo;
    m_ids = SDL_CreateGPUBuffer(m_device, &idsInfo);

    // Read by unit.vert too, for the tints.
    SDL_GPUBufferCreateInfo statusInfo{};
    statusInfo.usage = usage;
    statusInfo.size = static_cast<Uint32>(sizeof(StatusEffect) * std::max<std::uint32_t>(m_capacity, 1));
    m_status = SDL_CreateGPUBuffer(m_device, &statusInfo);
    SDL_GPUBufferCreateInfo pendingInfo{};
    pendingInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    pendingInfo.size = static_cast<Uint32>(sizeof(std::uint32_t) * std::max<std::uint32_t>(m_capacity, 1));
    m_pendingDamage = SDL_CreateGPUBuffer(m_device, &pendingInfo);

    // Bullets are drawn too.
    m_bullets = createUnitBuffer(m_device, kMaxBullets, usage);
    SDL_GPUBufferCreateInfo bulletPrevInfo{};
    bulletPrevInfo.usage = usage;
    bulletPrevInfo.size = static_cast<Uint32>(sizeof(glm::vec2) * kMaxBullets);
    m_bulletPrev = SDL_CreateGPUBuffer(m_device, &bulletPrevInfo);
    SDL_GPUBufferCreateInfo bulletParamsInfo{};
    bulletParamsInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    bulletParamsInfo.size = static_cast<Uint32>(sizeof(GPUBulletParams) * kMaxBullets);
    m_bulletParams = SDL_CreateGPUBuffer(m_device, &bulletParamsInfo);
    m_nextBullet = 0;
    m_bulletExpiry.assign(kMaxBullets, 0.0f);

    SDL_GPUBufferCreateInfo statusTableInfo{};
    statusTableInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    statusTableInfo.size = static_cast<Uint32>(sizeof(StatusEffect) * kStatusTableSize);
    m_statusTable = SDL_CreateGPUBuffer(m_device, &statusTableInfo);

    bool spares = true;
    if (collision.reorderUnits) {
        m_prevSpare = SDL_CreateGPUBuffer(m_device, &prevInfo);
        m_healthSpare = SDL_CreateGPUBuffer(m_device, &healthInfo);
        m_flingSpare = SDL_CreateGPUBuffer(m_device, &flingInfo);
        m_idsSpare = SDL_CreateGPUBuffer(m_device, &idsInfo);
        m_slots = SDL_CreateGPUBuffer(m_device, &idsInfo);
        m_statusSpare = SDL_CreateGPUBuffer(m_device, &statusInfo);
        spares = m_prevSpare && m_healthSpare && m_flingSpare && m_idsSpare && m_slots && m_statusSpare;
    }

    SDL_GPUBufferCreateInfo feedbackInfo{};
    feedbackInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE;
    feedbackInfo.size = sizeof(std::uint32_t) * kFeedbackWords;
    m_feedback = SDL_CreateGPUBuffer(m_device, &feedbackInfo);
    SDL_GPUTransferBufferCreateInfo feedbackTransferInfo{};
    feedbackTransferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    feedbackTransferInfo.size = feedbackInfo.size;
    m_feedbackTransfer = SDL_CreateGPUTransferBuffer(m_device, &feedbackTransferInfo);
    m_tally = {};

    if (!m_units[0] || !m_units[1] || !m_prevPositions || !m_health || !m_fling || !m_ids || !spares || !m_feedback ||
        !m_feedbackTransfer || !m_status || !m_statusTable || !m_pendingDamage || !m_bullets || !m_bulletPrev ||
        !m_bulletParams) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuCollidePhysics: failed to create buffers: %s", SDL_GetError());
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
    // Every unit, including those still to spawn, starts unflung and with its
    // slot as its ID. The spares too: a reorder only ever writes the slots of
    // units already in play, so the slots past them keep these until a spawn
    // reaches them, in whichever buffer is live by then. Slots start as the
    // same identity: right until the first reorder.
    const std::vector<glm::vec2> fling(std::max<std::uint32_t>(m_capacity, 1), glm::vec2(0.0f));
    std::vector<std::uint32_t> ids(std::max<std::uint32_t>(m_capacity, 1));
    for (std::uint32_t i = 0; i < ids.size(); ++i) {
        ids[i] = i;
    }
    const auto flingSize = static_cast<std::uint32_t>(sizeof(glm::vec2) * fling.size());
    const auto idsSize = static_cast<std::uint32_t>(sizeof(std::uint32_t) * ids.size());
    for (SDL_GPUBuffer* buffer : {m_fling, m_flingSpare}) {
        if (buffer && !uploadBytesNow(m_device, fling.data(), flingSize, buffer)) {
            return false;
        }
    }
    for (SDL_GPUBuffer* buffer : {m_ids, m_idsSpare, m_slots}) {
        if (buffer && !uploadBytesNow(m_device, ids.data(), idsSize, buffer)) {
            return false;
        }
    }
    const std::vector<std::uint32_t> noFeedback(kFeedbackWords, 0u);
    if (!uploadBytesNow(m_device, noFeedback.data(), sizeof(std::uint32_t) * kFeedbackWords, m_feedback)) {
        return false;
    }
    // No unit is under any effect, and no source applies one until turrets are set.
    const std::vector<StatusEffect> noStatus(std::max<std::uint32_t>(m_capacity, 1));
    const auto statusSize = static_cast<std::uint32_t>(sizeof(StatusEffect) * noStatus.size());
    for (SDL_GPUBuffer* buffer : {m_status, m_statusSpare}) {
        if (buffer && !uploadBytesNow(m_device, noStatus.data(), statusSize, buffer)) {
            return false;
        }
    }
    const std::vector<std::uint32_t> noDamage(std::max<std::uint32_t>(m_capacity, 1), 0u);
    if (!uploadBytesNow(m_device, noDamage.data(), static_cast<std::uint32_t>(sizeof(std::uint32_t) * noDamage.size()),
                        m_pendingDamage)) {
        return false;
    }
    // Every bullet starts spent.
    const std::vector<BaseUnitData> noBullets(kMaxBullets, BaseUnitData{glm::vec2(kDeadPosition), glm::vec2(0.0f)});
    const std::vector<glm::vec2> noBulletPrev(kMaxBullets, glm::vec2(kDeadPosition));
    const std::vector<GPUBulletParams> noBulletParams(kMaxBullets, GPUBulletParams{});
    if (!uploadBytesNow(m_device, noBullets.data(), static_cast<std::uint32_t>(sizeof(BaseUnitData) * kMaxBullets),
                        m_bullets) ||
        !uploadBytesNow(m_device, noBulletPrev.data(), static_cast<std::uint32_t>(sizeof(glm::vec2) * kMaxBullets),
                        m_bulletPrev) ||
        !uploadBytesNow(m_device, noBulletParams.data(),
                        static_cast<std::uint32_t>(sizeof(GPUBulletParams) * kMaxBullets), m_bulletParams)) {
        return false;
    }
    const std::vector<StatusEffect> noTable(kStatusTableSize);
    if (!uploadBytesNow(m_device, noTable.data(), static_cast<std::uint32_t>(sizeof(StatusEffect) * kStatusTableSize),
                        m_statusTable)) {
        return false;
    }
    return uploadUnitsNow(m_device, config.initialUnits.data(), m_unitCount, m_units, 1) &&
           uploadBytesNow(m_device, health.data(), static_cast<std::uint32_t>(sizeof(float) * health.size()), m_health);
}

void GpuCollidePhysics::shutdown() {
    m_walls.shutdown();
    m_reorder.shutdown();
    m_collisions.shutdown();
    m_grid.shutdown();
    m_renderer.shutdown();

    if (m_device == nullptr) {
        return;
    }

    SDL_GPUComputePipeline** pipelines[] = {&m_integratePipeline, &m_finalizePipeline, &m_aimPipeline, &m_firePipeline,
                                            &m_bulletPipeline};
    for (SDL_GPUComputePipeline** pipeline : pipelines) {
        if (*pipeline) {
            SDL_ReleaseGPUComputePipeline(m_device, *pipeline);
            *pipeline = nullptr;
        }
    }

    if (m_feedbackFence) {
        SDL_WaitForGPUFences(m_device, true, &m_feedbackFence, 1);
        SDL_ReleaseGPUFence(m_device, m_feedbackFence);
        m_feedbackFence = nullptr;
    }

    for (SDL_GPUTransferBuffer** transfer : {&m_spawnTransfer, &m_spawnHealthTransfer, &m_feedbackTransfer}) {
        if (*transfer) {
            SDL_ReleaseGPUTransferBuffer(m_device, *transfer);
            *transfer = nullptr;
        }
    }

    SDL_GPUBuffer** buffers[] = {&m_units[0],      &m_units[1],  &m_prevPositions, &m_health,      &m_fling,
                                 &m_ids,           &m_prevSpare, &m_healthSpare,   &m_flingSpare,  &m_idsSpare,
                                 &m_slots,         &m_feedback,  &m_status,        &m_statusSpare, &m_statusTable,
                                 &m_pendingDamage, &m_bullets,   &m_bulletPrev,    &m_bulletParams};
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
    // A blast with no push still counts if it does damage.
    if (explosion.radius <= 0.0f || (explosion.strength == 0.0f && explosion.damage <= 0.0f)) {
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

    const std::vector<BaseUnitData> units =
        spawnInAreas(count, flow.spawns, m_config.worldSize, m_config.unitSize, m_spawnRng, m_config.walls.get());
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

// The download goes on a command buffer submitted now, ahead of the frame's
// own, so it reads the feedback as of the last frame: fine for a HUD, and
// turrets allow for it.
void GpuCollidePhysics::sampleFeedback() {
    if (m_feedbackFence) {
        if (!SDL_QueryGPUFence(m_device, m_feedbackFence)) {
            return;
        }
        SDL_ReleaseGPUFence(m_device, m_feedbackFence);
        m_feedbackFence = nullptr;
        if (const auto* words =
                static_cast<const std::uint32_t*>(SDL_MapGPUTransferBuffer(m_device, m_feedbackTransfer, false))) {
            m_tally.kills = words[0];
            m_tally.leaks = words[1];
            m_turrets.readFeedback(words + kFeedbackHeader, m_grid.cellSize(), m_grid.gridDim());
            SDL_UnmapGPUTransferBuffer(m_device, m_feedbackTransfer);
        }
    }

    SDL_GPUCommandBuffer* commands = SDL_AcquireGPUCommandBuffer(m_device);
    if (commands == nullptr) {
        return;
    }
    if (SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands)) {
        SDL_GPUBufferRegion src{};
        src.buffer = m_feedback;
        src.size = sizeof(std::uint32_t) * kFeedbackWords;
        SDL_GPUTransferBufferLocation dst{};
        dst.transfer_buffer = m_feedbackTransfer;
        SDL_DownloadFromGPUBuffer(copy, &src, &dst);
        SDL_EndGPUCopyPass(copy);
    }
    m_feedbackFence = SDL_SubmitGPUCommandBufferAndAcquireFence(commands);
}

bool GpuCollidePhysics::setTurrets(const std::vector<logic::Turret>& turrets) {
    m_turrets.set(turrets);
    rebuildWalls();
    uploadStatusTable();
    return true;
}

bool GpuCollidePhysics::addTurrets(const std::vector<logic::Turret>& turrets) {
    if (!m_turrets.add(turrets)) {
        return false;
    }
    const bool blocker = std::any_of(turrets.begin(), turrets.end(), [](const logic::Turret& turret) {
        return logic::turretDef(turret.kind).placement == logic::TurretClass::Blocker;
    });
    if (blocker) {
        rebuildWalls();
    }
    uploadStatusTable();
    return true;
}

void GpuCollidePhysics::uploadStatusTable() {
    const std::vector<StatusEffect> table = m_turrets.statusTable();
    if (!uploadBytesNow(m_device, table.data(), static_cast<std::uint32_t>(sizeof(StatusEffect) * table.size()),
                        m_statusTable)) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuCollidePhysics: failed to upload the status table");
    }
}

// Blockers are walls while they stand: the level's own walls plus a box per
// blocker, binned afresh, so every pass that keeps units out of walls keeps
// them out of blockers too. Only when blockers change, which is rarely.
void GpuCollidePhysics::rebuildWalls() {
    const std::vector<WallPrimitive> blockers = m_turrets.blockerWalls();
    if (blockers.empty() && m_config.walls == m_baseWalls) {
        return;
    }
    const WallColliders base = m_baseWalls ? *m_baseWalls : WallColliders{};
    m_config.walls = blockers.empty() ? m_baseWalls : std::make_shared<const WallColliders>(base.withExtra(blockers));
    m_walls.shutdown();
    if (!m_walls.init(m_device, m_config.walls.get())) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuCollidePhysics: failed to rebuild walls for blockers");
    }
}

void GpuCollidePhysics::aimMortars(SDL_GPUCommandBuffer* commands, const std::vector<AimRequest>& aims) {
    if (aims.empty()) {
        return;
    }
    GPUAimUniforms uniforms{};
    uniforms.gridDim = m_grid.gridDim();
    uniforms.cellSize = m_grid.cellSize();
    uniforms.requestCount = static_cast<Uint32>(std::min(aims.size(), logic::kMaxTurrets));
    for (Uint32 i = 0; i < uniforms.requestCount; ++i) {
        uniforms.requests[i] = glm::vec4(aims[i].center, aims[i].range, 0.0f);
        uniforms.requestIds[i] = glm::uvec4(aims[i].slot, aims[i].shotId, 0u, 0u);
    }
    SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));

    SDL_GPUStorageBufferReadWriteBinding binding{};
    binding.buffer = m_feedback;
    if (SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &binding, 1)) {
        SDL_BindGPUComputePipeline(pass, m_aimPipeline);
        SDL_GPUBuffer* ranges = m_grid.gridB();
        SDL_BindGPUComputeStorageBuffers(pass, 0, &ranges, 1);
        SDL_DispatchGPUCompute(pass, uniforms.requestCount, 1, 1);
        SDL_EndGPUComputePass(pass);
    }
}

void GpuCollidePhysics::fireGuns(SDL_GPUCommandBuffer* commands, const std::vector<FireRequest>& fires) {
    if (fires.empty()) {
        return;
    }
    const float now = static_cast<float>(m_frame) * m_dt;
    GPUFireUniforms uniforms{};
    uniforms.gridDim = m_grid.gridDim();
    uniforms.cellSize = m_grid.cellSize();
    uniforms.shotCount = static_cast<Uint32>(std::min(fires.size(), logic::kMaxTurrets));
    uniforms.unitRadius = m_config.unitSize * 0.5f;
    uniforms.bulletRadius = kBulletSize * 0.5f;
    uniforms.spread = kBulletSpread;
    uniforms.seed = m_frame;
    for (Uint32 i = 0; i < uniforms.shotCount; ++i) {
        const FireRequest& shot = fires[i];
        const std::uint32_t slot = m_nextBullet;
        m_nextBullet = (m_nextBullet + 1) % kMaxBullets;
        m_bulletExpiry[slot] = now + shot.life;
        uniforms.shotAim[i] = glm::vec4(shot.center, shot.range, shot.speed);
        uniforms.shotHit[i] = glm::vec4(shot.damage, shot.life, 0.0f, 0.0f);
        uniforms.shotIds[i] = glm::uvec4(slot, shot.pierce, shot.status, 0u);
    }
    SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));

    SDL_GPUStorageBufferReadWriteBinding bindings[3]{};
    bindings[0].buffer = m_bullets;
    bindings[1].buffer = m_bulletPrev;
    bindings[2].buffer = m_bulletParams;
    if (SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, bindings, 3)) {
        SDL_BindGPUComputePipeline(pass, m_firePipeline);
        SDL_GPUBuffer* readOnly[3] = {m_grid.gridB(), m_grid.gridA(), m_units[m_current]};
        SDL_BindGPUComputeStorageBuffers(pass, 0, readOnly, 3);
        SDL_DispatchGPUCompute(pass, 1, 1, 1);
        SDL_EndGPUComputePass(pass);
    }
}

void GpuCollidePhysics::stepBullets(SDL_GPUCommandBuffer* commands) {
    // Nothing to move if no bullet could still be flying.
    const float since = static_cast<float>(m_frame - 1) * m_dt;
    if (std::none_of(m_bulletExpiry.begin(), m_bulletExpiry.end(), [since](float t) { return t > since; })) {
        return;
    }
    GPUBulletStepUniforms uniforms{};
    uniforms.dt = m_dt;
    uniforms.bulletCount = kMaxBullets;
    uniforms.unitRadius = m_config.unitSize * 0.5f;
    uniforms.cellSize = m_grid.cellSize();
    uniforms.gridDim = m_grid.gridDim();
    uniforms.worldSize = m_config.worldSize;
    uniforms.wallBinDim = m_walls.binDim();
    uniforms.wallBinSize = m_walls.binSize();
    uniforms.wallCount = m_walls.primitiveCount();
    SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));

    SDL_GPUStorageBufferReadWriteBinding bindings[5]{};
    bindings[0].buffer = m_bullets;
    bindings[1].buffer = m_bulletPrev;
    bindings[2].buffer = m_bulletParams;
    bindings[3].buffer = m_pendingDamage;
    bindings[4].buffer = m_status;
    if (SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, bindings, 5)) {
        SDL_BindGPUComputePipeline(pass, m_bulletPipeline);
        SDL_GPUBuffer* readOnly[8] = {m_walls.buffers()[0],
                                      m_walls.buffers()[1],
                                      m_walls.buffers()[2],
                                      m_grid.gridB(),
                                      m_grid.gridA(),
                                      m_units[m_current],
                                      m_ids,
                                      m_statusTable};
        SDL_BindGPUComputeStorageBuffers(pass, 0, readOnly, 8);
        SDL_DispatchGPUCompute(pass, groups(kMaxBullets), 1, 1);
        SDL_EndGPUComputePass(pass);
    }
}

void GpuCollidePhysics::compute(SDL_GPUCommandBuffer* commands) {
    if (!m_finalizePipeline || m_dt <= 0.0f) {
        return;
    }

    sampleFeedback();
    if (m_turrets.takeBlockersChanged()) {
        rebuildWalls();
    }

    // 0. Bring in whoever is due.
    spawnPending(commands);
    if (m_unitCount == 0) {
        return;
    }
    ++m_frame;

    const glm::vec2 worldMax = glm::max(m_config.worldSize - glm::vec2(m_config.unitSize), glm::vec2(0.0f));
    const float radius = m_config.unitSize * 0.5f;

    // Mortars wanting a target this step: aimed once the grid is built. Guns
    // firing: aimed once units are in grid order.
    std::vector<AimRequest> aims;
    std::vector<FireRequest> fires;

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
        // Turrets first: their bursts join this step's explosions.
        std::vector<Explosion> blasts;
        uniforms.turretCount = m_turrets.step(m_dt, uniforms.turrets, blasts, aims, fires);
        for (const Explosion& blast : blasts) {
            addExplosion(blast);
        }
        uniforms.explosionCount = static_cast<Uint32>(m_explosions.size());
        for (std::size_t i = 0; i < m_explosions.size(); ++i) {
            ActiveExplosion& active = m_explosions[i];
            const Explosion& e = active.explosion;
            const float share = explosionShare(e, active.left, m_dt, kMaxBlastStepDiameters * m_config.unitSize);
            active.left -= share;
            uniforms.explosions[i * 2] = glm::vec4(e.center, e.radius, e.strength * share);
            // Damage is given out in the same shares as the push, so it all
            // lands over the blast's duration.
            uniforms.explosions[i * 2 + 1] = glm::vec4(static_cast<float>(e.falloff), e.fling ? 1.0f : 0.0f,
                                                       e.damage * share, static_cast<float>(e.status));
        }
        uniforms.flingDrag = m_flingDrag;
        // The same limit as a shove's, per step.
        uniforms.flingMaxSpeed = kMaxBlastStepDiameters * m_config.unitSize / m_dt;
        uniforms.time = static_cast<float>(m_frame) * m_dt;
        uniforms.wobbleAngle = m_config.movement.wobbleAngle;
        uniforms.wobbleRate = m_config.movement.wobbleRate;
        // Last step's grid, as a density map: none before the first build.
        uniforms.congestionEnabled = m_gridBuilt && m_config.collision.congestionStop > 0.0f ? 1u : 0u;
        uniforms.gridDim = m_grid.gridDim();
        uniforms.invCellSize = 1.0f / m_grid.cellSize();
        uniforms.congestionStart = m_config.collision.congestionStart;
        uniforms.congestionStop = m_config.collision.congestionStop;
        std::erase_if(m_explosions, [](const ActiveExplosion& active) { return active.left <= 1e-5f; });
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));

        SDL_GPUStorageBufferReadWriteBinding bindings[6]{};
        bindings[0].buffer = m_units[m_current];
        bindings[1].buffer = m_prevPositions;
        bindings[2].buffer = m_fling;
        bindings[3].buffer = m_health;
        bindings[4].buffer = m_feedback;
        bindings[5].buffer = m_status;

        if (SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, bindings, 6)) {
            SDL_BindGPUComputePipeline(pass, m_integratePipeline);
            SDL_GPUBuffer* readOnly[3] = {m_ids, m_grid.gridB(), m_statusTable};
            SDL_BindGPUComputeStorageBuffers(pass, 0, readOnly, 3);
            SDL_DispatchGPUCompute(pass, groups(m_unitCount), 1, 1);
            SDL_EndGPUComputePass(pass);
        }
    }

    // 2. Broadphase over the predicted positions.
    m_grid.build(commands, m_units[m_current], m_unitCount);
    m_gridBuilt = true;
    aimMortars(commands, aims);

    // Units into grid order, every per-unit buffer alike. Previous positions
    // too, so drawing still blends each unit from where it was.
    if (m_config.collision.reorderUnits) {
        const GpuUnitReorder::Stream streams[] = {
            {&m_units[m_current], &m_units[1 - m_current], sizeof(BaseUnitData)},
            {&m_prevPositions, &m_prevSpare, sizeof(glm::vec2)},
            {&m_health, &m_healthSpare, sizeof(float)},
            {&m_fling, &m_flingSpare, sizeof(glm::vec2)},
            {&m_ids, &m_idsSpare, sizeof(std::uint32_t)},
            {&m_status, &m_statusSpare, sizeof(StatusEffect)},
        };
        m_reorder.reorder(commands, m_grid, m_unitCount, streams);
        m_reorder.writeSlots(commands, m_ids, m_slots, m_unitCount);
    }

    // Bullets, against units where they now are.
    fireGuns(commands, fires);
    stepBullets(commands);

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
        uniforms.exitMode = static_cast<Uint32>(m_config.flow.onExit);
        uniforms.exitDamage = m_config.health.exitDamage;
        m_turrets.packContacts(m_dt, uniforms.blockerContacts);
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));

        SDL_GPUStorageBufferReadWriteBinding bindings[5]{};
        bindings[0].buffer = m_units[m_current];
        bindings[1].buffer = m_health;
        bindings[2].buffer = m_fling;
        bindings[3].buffer = m_feedback;
        bindings[4].buffer = m_pendingDamage;

        if (SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, bindings, 5)) {
            SDL_BindGPUComputePipeline(pass, m_finalizePipeline);
            SDL_GPUBuffer* readOnly[4] = {m_prevPositions, m_walls.buffers()[0], m_walls.buffers()[1],
                                          m_walls.buffers()[2]};
            SDL_BindGPUComputeStorageBuffers(pass, 0, readOnly, 4);
            SDL_DispatchGPUCompute(pass, groups(m_unitCount), 1, 1);
            SDL_EndGPUComputePass(pass);
        }
    }

    // 5. Velocity toward the neighbours' mean, if asked for.
    m_current = m_collisions.smooth(commands, m_units, m_current, m_unitCount, m_grid, m_walls);
}

void GpuCollidePhysics::renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                                   const glm::mat4& viewProjection, glm::vec2 viewportPx, float alpha) {
    // m_prevPositions holds where each unit started the latest step, which
    // unit_integrate writes before moving it: exactly the previous state.
    gfx::UnitRenderer::DrawOptions options;
    options.previous = m_frame > 0 ? m_prevPositions : nullptr;
    options.alpha = alpha;
    // Further than a unit could move in one step is a respawn, not motion.
    options.maxJump = std::max(4.0f * m_config.unitSize, 2.0f * m_config.movement.maxSpeed * m_dt);
    options.health = m_health;
    options.maxHealth = static_cast<float>(m_config.health.maxHp);
    options.ids = m_ids;
    options.slots = m_slots;
    options.viewportPx = viewportPx;
    options.sprites = m_sprites.enabled;
    options.species = m_sprites.species;
    options.lod = m_sprites.lod;
    options.thresholds = m_sprites.thresholds;
    options.rotate = m_sprites.rotate;
    options.directionSteps = static_cast<Uint32>(m_sprites.directionSteps);
    // Below a few percent of walking pace there's no clear direction to face.
    options.minTurnSpeed = 0.05f * m_config.movement.maxSpeed;
    options.integerScale = m_sprites.integerScale;
    options.seed = m_config.flow.seed;
    options.status = m_status;
    options.time = static_cast<float>(m_frame) * m_dt;
    m_renderer.draw(commands, pass, m_units[m_current], m_unitCount, viewProjection, m_config.unitSize, options);

    // Bullets on top, blended between steps like units. A fresh bullet's
    // previous position is where it was fired from, so it never jumps.
    gfx::UnitRenderer::DrawOptions bulletOptions;
    bulletOptions.previous = m_frame > 0 ? m_bulletPrev : nullptr;
    bulletOptions.alpha = alpha;
    bulletOptions.maxJump = 1.0e6f;
    bulletOptions.color = glm::vec4(1.0f, 0.92f, 0.35f, 1.0f);
    bulletOptions.markFirst = false;
    bulletOptions.sprites = false; // plain discs, not crowd sprites
    m_renderer.draw(commands, pass, m_bullets, kMaxBullets, viewProjection, kBulletSize, bulletOptions);
}

void GpuCollidePhysics::debugUi() {
    // Live: the shader reads these every step.
    float wobbleDegrees = glm::degrees(m_config.movement.wobbleAngle);
    if (ImGui::SliderFloat(tr("Wobble"), &wobbleDegrees, 0.0f, 60.0f, tr("%.0f deg"))) {
        m_config.movement.wobbleAngle = glm::radians(wobbleDegrees);
    }
    ImGui::SliderFloat(tr("Wobble rate"), &m_config.movement.wobbleRate, 0.1f, 5.0f, "%.1f Hz");

    // An upper bound: bullets that hit something are spent before their life is up.
    const float now = static_cast<float>(m_frame) * m_dt;
    const auto flying = std::count_if(m_bulletExpiry.begin(), m_bulletExpiry.end(), [now](float t) { return t > now; });
    ImGui::Text("Bullets: up to %td / %u in flight", flying, kMaxBullets);

    using Renderer = gfx::UnitRenderer;
    ImGui::SeparatorText(tr("Unit sprites"));
    if (!m_renderer.hasSprites()) {
        ImGui::TextDisabled(tr("textures/units.png not loaded: drawing discs"));
        return;
    }
    ImGui::Checkbox(tr("Sprites"), &m_sprites.enabled);
    if (!m_sprites.enabled) {
        return;
    }
    int species = static_cast<int>(m_sprites.species);
    if (ImGui::Combo(tr("Crowd"), &species, "Humans\0Aliens\0")) {
        m_sprites.species = static_cast<Renderer::Species>(species);
    }
    // Combo index 0 is Auto (-1); the rest follow Lod's values.
    int lod = static_cast<int>(m_sprites.lod) + 1;
    if (ImGui::Combo(tr("LOD"), &lod, "Auto\0Near\0Mid\0Low\0Circle\0")) {
        m_sprites.lod = static_cast<Renderer::Lod>(lod - 1);
    }
    // Screen pixels a unit must span for each LOD; below low it's a circle.
    Renderer::LodThresholds& t = m_sprites.thresholds;
    ImGui::BeginDisabled(m_sprites.lod != Renderer::Lod::Auto);
    ImGui::SliderFloat(tr("Near from"), &t.nearPixels, 1.0f, 60.0f, "%.1f px");
    ImGui::SliderFloat(tr("Mid from"), &t.midPixels, 1.0f, 60.0f, "%.1f px");
    ImGui::SliderFloat(tr("Low from"), &t.lowPixels, 1.0f, 60.0f, "%.1f px");
    ImGui::SliderFloat(tr("Hysteresis"), &t.hysteresis, 0.0f, 3.0f, "%.1f px");
    t.midPixels = std::min(t.midPixels, t.nearPixels); // mid can't start above near
    t.lowPixels = std::min(t.lowPixels, t.midPixels);
    if (ImGui::Button(tr("Reset LOD thresholds"))) {
        t = Renderer::LodThresholds{};
    }
    ImGui::EndDisabled();
    ImGui::Checkbox(tr("Whole-pixel scale"), &m_sprites.integerScale);
    ImGui::Checkbox(tr("Face direction of travel"), &m_sprites.rotate);
    ImGui::BeginDisabled(!m_sprites.rotate);
    ImGui::SliderInt(tr("Directions"), &m_sprites.directionSteps, 0, 32, m_sprites.directionSteps == 0 ? "any" : "%d");
    ImGui::EndDisabled();
    static const char* const kLodNames[] = {"near", "mid", "low", "circle"};
    ImGui::Text("Drawing %s, %.1f px per unit", kLodNames[static_cast<int>(m_renderer.lastLod())],
                m_renderer.lastUnitPixels());
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

// Live units only: despawned ones sit at kDeadPosition and would otherwise
// count as one huge overlapping pile.
bool GpuCollidePhysics::readUnits(std::vector<BaseUnitData>& out) {
    if (!m_device || !downloadUnits(m_device, m_units[m_current], m_unitCount, out)) {
        return false;
    }
    std::erase_if(out, [](const BaseUnitData& unit) { return isDeadUnit(unit.pos); });
    return true;
}

} // namespace horde::sim
