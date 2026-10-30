#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>

#include <cstdint>
#include <vector>

#include "sim/PhysicsSystem.hpp"

namespace horde::scene {
struct Services;
}

namespace horde::sim {

// GPU broadphase: buckets units into a uniform grid so a shader can find every
// unit near a point by looking at a few cells instead of every unit.
//
// Built with a counting sort (count per cell, prefix sum, scatter), producing
// the layout unit_collide.comp reads as GridA / GridB:
//
//   GridA = sorted (cellId, unitIndex) keys
//   GridB = per cell [start, end) into GridA, or 0xFFFFFFFF start when empty
//
// Anything that needs neighbours can share one grid per frame: unit collisions
// now, and later separation steering, target finding, area damage and so on.
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

    SDL_GPUBuffer* gridA() const {
        return m_keys;
    }

    SDL_GPUBuffer* gridB() const {
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

    // GPU passes build() records per call: a rough cost indicator.
    std::uint32_t passCount() const;

private:
    void fill(SDL_GPUCommandBuffer* commands, SDL_GPUBuffer* buffer, std::uint32_t count, std::uint32_t value,
              std::uint32_t stride);

    SDL_GPUDevice* m_device = nullptr;

    SDL_GPUComputePipeline* m_fillPipeline = nullptr; // clear counters
    SDL_GPUComputePipeline* m_countPipeline = nullptr;
    SDL_GPUComputePipeline* m_scanPipeline = nullptr;
    SDL_GPUComputePipeline* m_scanAddPipeline = nullptr;
    SDL_GPUComputePipeline* m_scatterPipeline = nullptr;

    // Sorted layout.
    SDL_GPUBuffer* m_keys = nullptr;
    SDL_GPUBuffer* m_ranges = nullptr;
    // Sort scratch.
    SDL_GPUBuffer* m_counts = nullptr;
    SDL_GPUBuffer* m_unitCells = nullptr;
    // Prefix-sum levels: [0] is the per-cell starts, each next one holds the
    // block sums of the one before, down to a single block.
    struct ScanLevel {
        SDL_GPUBuffer* buffer = nullptr;
        std::uint32_t count = 0;
    };
    std::vector<ScanLevel> m_scanLevels;
    SDL_GPUBuffer* m_scanScratch = nullptr; // block sum of the last level, unused

    glm::uvec2 m_gridDim{0, 0};
    float m_cellSize = 1.0f;
};

} // namespace horde::sim
