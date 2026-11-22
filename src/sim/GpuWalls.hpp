#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>

#include <array>
#include <cstdint>

#include "sim/CrowdEffects.hpp"

namespace horde::sim {

class WallColliders;

// A level's WallColliders on the GPU: the three read-only buffers walls.hlsli
// reads (primitives, bins, bin indices), and the numbers its cbuffer needs.
//
// With no walls the buffers still exist, holding one dummy entry, so shaders
// always have something bound and simply see WallCount == 0.
class GpuWalls {
public:
    GpuWalls() = default;
    ~GpuWalls();

    GpuWalls(const GpuWalls&) = delete;
    GpuWalls& operator=(const GpuWalls&) = delete;

    // `walls` may be null for a level without walls.
    bool init(SDL_GPUDevice* device, const WallColliders* walls);
    void shutdown();

    // In binding order: primitives, bins, bin indices.
    SDL_GPUBuffer* const* buffers() const {
        return m_buffers;
    }

    glm::uvec2 binDim() const {
        return m_binDim;
    }

    float binSize() const {
        return m_binSize;
    }

    std::uint32_t primitiveCount() const {
        return m_count;
    }

    // A round's towers, pushed out of like walls but passed to shaders in
    // their uniforms (TowerCount, Towers in walls.hlsli), not binned: there
    // are few, and they change size while they grow in.
    void setTowers(const std::array<TowerCollider, kMaxTowers>& towers);

    // Highest live slot + 1: shaders loop this far.
    std::uint32_t towerCount() const {
        return m_towerCount;
    }

    // kMaxTowers entries: (centre, radius, 0, 0); radius 0 is an empty slot.
    const glm::vec4* towerUniforms() const {
        return m_towers.data();
    }

private:
    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUBuffer* m_buffers[3] = {nullptr, nullptr, nullptr};
    glm::uvec2 m_binDim{1, 1};
    float m_binSize = 1.0f;
    std::uint32_t m_count = 0;
    std::array<glm::vec4, kMaxTowers> m_towers{};
    std::uint32_t m_towerCount = 0;
};

} // namespace horde::sim
