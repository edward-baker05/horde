#include "sim/StaticPhysics.hpp"

#include "gfx/GpuContext.hpp"
#include "scene/Scene.hpp"
#include "sim/UnitBuffers.hpp"

namespace horde::sim {

StaticPhysics::~StaticPhysics() {
    shutdown();
}

bool StaticPhysics::init(scene::Services& services, const SimConfig& config) {
    m_device = services.gpu->device();
    m_unitCount = static_cast<std::uint32_t>(config.initialUnits.size());
    m_unitSize = config.unitSize;

    if (!m_renderer.init(services)) {
        return false;
    }

    m_units = createUnitBuffer(m_device, m_unitCount, SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ);
    if (m_units == nullptr) {
        return false;
    }

    return uploadUnitsNow(m_device, config.initialUnits.data(), m_unitCount, &m_units, 1);
}

void StaticPhysics::shutdown() {
    m_renderer.shutdown();
    if (m_device && m_units) {
        SDL_ReleaseGPUBuffer(m_device, m_units);
        m_units = nullptr;
    }
}

void StaticPhysics::renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                               const glm::mat4& viewProjection) {
    m_renderer.draw(commands, pass, m_units, m_unitCount, viewProjection, m_unitSize);
}

} // namespace horde::sim
