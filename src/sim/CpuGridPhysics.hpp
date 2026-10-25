#pragma once

#include <vector>

#include "gfx/UnitRenderer.hpp"
#include "logic/UnitData.hpp"
#include "sim/PhysicsSystem.hpp"
#include "sim/UnitBuffers.hpp"

namespace horde::sim {

// The CPU UnitManager simulation, streamed to the GPU each frame for drawing.
class CpuGridPhysics : public PhysicsSystem {
public:
    ~CpuGridPhysics() override;

    bool init(scene::Services& services, const SimConfig& config) override;
    void shutdown() override;
    void update(float dt) override;
    void compute(SDL_GPUCommandBuffer* commands) override;
    void renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                    const glm::mat4& viewProjection) override;

    bool readUnits(std::vector<BaseUnitData>& out) override {
        out = m_staging;
        return true;
    }

    std::uint32_t unitCount() const override {
        return static_cast<std::uint32_t>(m_units.GetCurrentUnits());
    }

private:
    UnitManager m_units;
    std::vector<BaseUnitData> m_staging;
    UnitStreamer m_streamer;
    gfx::UnitRenderer m_renderer;
    float m_unitSize = 5.0f;
};

} // namespace horde::sim
