#pragma once

#include "gfx/UnitRenderer.hpp"
#include "sim/PhysicsSystem.hpp"

namespace horde::sim {

// Does no simulation at all: units are uploaded once and drawn where they
// spawned. Benchmarking it measures the frame cost of everything except physics.
class StaticPhysics : public PhysicsSystem {
public:
    ~StaticPhysics() override;

    bool init(scene::Services& services, const SimConfig& config) override;
    void shutdown() override;
    void renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                    const glm::mat4& viewProjection) override;

    std::uint32_t unitCount() const override {
        return m_unitCount;
    }

private:
    SDL_GPUDevice* m_device = nullptr;
    gfx::UnitRenderer m_renderer;
    SDL_GPUBuffer* m_units = nullptr;
    std::uint32_t m_unitCount = 0;
    float m_unitSize = 5.0f;
};

} // namespace horde::sim
