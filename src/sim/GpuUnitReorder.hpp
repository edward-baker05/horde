#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <span>

namespace horde::scene {
struct Services;
}

namespace horde::sim {

class GpuSpatialGrid;

// Puts units in the order of the grid they were just sorted into, so that
// units sharing a cell sit next to each other in every per-unit buffer. Passes
// that read neighbours (collisions above all) then read nearby memory instead
// of jumping across the whole buffer, and the threads of one workgroup handle
// units in one part of the level.
//
// Every per-unit buffer has to move the same way, or a unit's position, health
// and so on would part company. The caller lists them all as streams; each is
// gathered into its spare, and the two swap. Afterwards the grid's keys are
// renumbered to match, so key s names unit s and a cell's range [start, end)
// is exactly the units in it.
//
// A unit's index changes every frame, so anything that has to follow a unit
// (a per-unit random phase, picking one out) needs a stable ID stream of its
// own. So does drawing: units drawn in index order would swap which is on top
// of an overlap whenever the order within a cell changes, which is every step
// (writeSlots).
class GpuUnitReorder {
public:
    GpuUnitReorder() = default;
    ~GpuUnitReorder();

    GpuUnitReorder(const GpuUnitReorder&) = delete;
    GpuUnitReorder& operator=(const GpuUnitReorder&) = delete;

    // One per-unit buffer: `*live` is gathered into `*spare`, then the
    // pointers swap. `stride` is the size of one unit's element, a whole
    // number of 32-bit words.
    struct Stream {
        SDL_GPUBuffer** live = nullptr;
        SDL_GPUBuffer** spare = nullptr;
        std::uint32_t stride = 0;
    };

    bool init(scene::Services& services);
    void shutdown();

    // Records one pass per stream. `grid` must have just been built over
    // these `unitCount` units, and no stream may be left out.
    void reorder(SDL_GPUCommandBuffer* commands, GpuSpatialGrid& grid, std::uint32_t unitCount,
                 std::span<const Stream> streams) const;

    // Records, after reorder(), where each unit now is by its stable ID:
    // slots[id] = index. `ids` holds each unit's ID (a permutation of [0,
    // unitCount)), already reordered. Lets drawing go in ID order, which
    // stays put while the grid order changes every step.
    void writeSlots(SDL_GPUCommandBuffer* commands, SDL_GPUBuffer* ids, SDL_GPUBuffer* slots,
                    std::uint32_t unitCount) const;

private:
    SDL_GPUDevice* m_device = nullptr;
    SDL_GPUComputePipeline* m_pipeline = nullptr;
    SDL_GPUComputePipeline* m_slotsPipeline = nullptr;
};

} // namespace horde::sim
