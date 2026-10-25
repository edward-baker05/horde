#pragma once

#include <glm/glm.hpp>

#include "gfx/UnitRenderer.hpp"
#include "sim/PhysicsSystem.hpp"

namespace horde::sim {

// GPU compute simulation: units are hashed into a uniform grid, the
// (cell, unit) keys bitonic-sorted, per-cell ranges found, and positions
// integrated with neighbour collisions — all without leaving the GPU.
class GpuGridPhysics : public PhysicsSystem {
public:
    ~GpuGridPhysics() override;

    bool init(scene::Services& services, const SimConfig& config) override;
    void shutdown() override;
    void update(float dt) override;
    void compute(SDL_GPUCommandBuffer* commands) override;
    void renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const glm::mat4& viewProjection,
                    float alpha) override;
    void debugUi() override;
    bool readUnits(std::vector<BaseUnitData>& out) override;

    std::uint32_t unitCount() const override {
        return m_unitCount;
    }

private:
    SDL_GPUDevice* m_device = nullptr;
    gfx::UnitRenderer m_renderer;

    glm::vec2 m_worldSize{0.0f, 0.0f};
    float m_unitSize = 5.0f;

    // GPU Simulation Pipelines
    SDL_GPUComputePipeline* m_gridClearPipeline = nullptr;
    SDL_GPUComputePipeline* m_gridBuildPipeline = nullptr;
    SDL_GPUComputePipeline* m_bitonicSortPipeline = nullptr;
    SDL_GPUComputePipeline* m_gridFindRangesPipeline = nullptr;
    SDL_GPUComputePipeline* m_computePipeline = nullptr;

    // Double buffers for deterministic collision simulation
    SDL_GPUBuffer* m_unitBuffers[2] = {nullptr, nullptr};
    Uint32 m_currentBufferIndex = 0;

    // GPU Sort-Based Uniform Grid Buffers
    SDL_GPUBuffer* m_gridKeysBuffer = nullptr;
    SDL_GPUBuffer* m_cellRangesBuffer = nullptr;
    Uint32 m_allocatedUnits = 0;
    glm::uvec2 m_gridDim{0, 0};
    Uint32 m_totalCells = 0;

    float m_dt = 0.0f;
    Uint32 m_unitCount = 0;
};

} // namespace horde::sim
