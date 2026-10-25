#include "sim/CpuGridPhysics.hpp"

#include "gfx/GpuContext.hpp"
#include "scene/Scene.hpp"

namespace horde::sim {

CpuGridPhysics::~CpuGridPhysics() {
    shutdown();
}

bool CpuGridPhysics::init(scene::Services& services, const SimConfig& config) {
    m_unitSize = config.unitSize;

    const auto count = static_cast<std::uint32_t>(config.initialUnits.size());
    m_units = UnitManager(count, config.worldSize, config.unitSize);
    for (const BaseUnitData& unit : config.initialUnits) {
        m_units.SpawnUnit(unit.pos, unit.vel, 10);
    }
    m_staging = config.initialUnits;

    return m_renderer.init(services) && m_streamer.init(services.gpu->device(), count);
}

void CpuGridPhysics::shutdown() {
    m_streamer.shutdown();
    m_renderer.shutdown();
}

void CpuGridPhysics::update(float dt) {
    m_units.UpdatePhysics(dt);

    // UnitManager's own BaseUnitData copy is refreshed before collisions are
    // resolved, so take positions after the whole step.
    const glm::vec2* positions = m_units.GetPositionsPtr();
    const std::size_t count = m_units.GetCurrentUnits();
    for (std::size_t i = 0; i < count; ++i) {
        m_staging[i].pos = positions[i];
    }
}

void CpuGridPhysics::compute(SDL_GPUCommandBuffer* commands) {
    m_streamer.upload(commands, m_staging.data(), unitCount());
}

void CpuGridPhysics::renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                                const glm::mat4& viewProjection) {
    m_renderer.draw(commands, pass, m_streamer.buffer(), unitCount(), viewProjection, m_unitSize);
}

} // namespace horde::sim
