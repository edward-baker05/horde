#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>

#include <cstdint>

namespace horde::scene {
struct Services;
}

namespace horde::sim {

// GPU broadphase: buckets units into a uniform grid so a shader can find every
// unit near a point by looking at a few cells instead of every unit.
//
// After build(), for every grid cell c:
//   cellRanges()[c]  = [start, end) into sortedKeys(), or 0xFFFFFFFF if empty
//   sortedKeys()[i]  = (cellId, unitIndex), sorted by cell
//
// Anything that needs neighbours can share one grid per frame: unit collisions
// now, and later separation steering, target finding, area damage and so on.
// How the keys are sorted is private to this class, so the bitonic sort can be
// replaced (e.g. by a counting sort) without touching its users.
class GpuSpatialGrid {
public:
    GpuSpatialGrid() = default;
    ~GpuSpatialGrid();

    GpuSpatialGrid(const GpuSpatialGrid&) = delete;
    GpuSpatialGrid& operator=(const GpuSpatialGrid&) = delete;

    // `cellSize` should be at least the largest interaction distance, so that
    // a 3x3 block of cells covers every possible neighbour.
    bool init(scene::Services& services, std::uint32_t maxUnits, glm::vec2 worldSize, float cellSize);
    void shutdown();

    // Records the passes that rebuild the grid from `units` (BaseUnitData).
    // Must be called outside any other pass.
    void build(SDL_GPUCommandBuffer* commands, SDL_GPUBuffer* units, std::uint32_t unitCount);

    SDL_GPUBuffer* sortedKeys() const {
        return m_keys;
    }

    SDL_GPUBuffer* cellRanges() const {
        return m_ranges;
    }

    glm::uvec2 gridDim() const {
        return m_gridDim;
    }

    std::uint32_t cellCount() const {
        return m_gridDim.x * m_gridDim.y;
    }

    float cellSize() const {
        return m_cellSize;
    }

    // Compute passes build() records per call: a rough cost indicator.
    std::uint32_t passCount() const;

private:
    SDL_GPUDevice* m_device = nullptr;

    SDL_GPUComputePipeline* m_clearPipeline = nullptr;
    SDL_GPUComputePipeline* m_buildPipeline = nullptr;
    SDL_GPUComputePipeline* m_sortPipeline = nullptr;
    SDL_GPUComputePipeline* m_rangesPipeline = nullptr;

    SDL_GPUBuffer* m_keys = nullptr;
    SDL_GPUBuffer* m_ranges = nullptr;

    glm::uvec2 m_gridDim{0, 0};
    float m_cellSize = 1.0f;
    // Key count rounded up to a power of two, as the bitonic sort needs.
    std::uint32_t m_sortSize = 0;
};

} // namespace horde::sim
