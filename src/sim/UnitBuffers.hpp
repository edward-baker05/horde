#pragma once

#include <SDL3/SDL_gpu.h>

#include <cstdint>
#include <vector>

#include "logic/UnitData.hpp"

namespace horde::sim {

// A storage buffer large enough for `count` BaseUnitData. Never zero-sized.
SDL_GPUBuffer* createUnitBuffer(SDL_GPUDevice* device, std::uint32_t count, SDL_GPUBufferUsageFlags usage);

// Copies `count` units into every buffer in `targets` on a command buffer of
// its own, submitted immediately. For one-off initial uploads.
bool uploadUnitsNow(SDL_GPUDevice* device, const BaseUnitData* units, std::uint32_t count,
                    SDL_GPUBuffer* const* targets, int targetCount);

// Uploads `size` bytes into the start of `target`, on a command buffer of its
// own submitted immediately. For one-off uploads of any data.
bool uploadBytesNow(SDL_GPUDevice* device, const void* data, std::uint32_t size, SDL_GPUBuffer* target);

// Copies `count` units from `source` back to the CPU, waiting for the GPU.
// Slow: for tests and measurements, not every frame.
bool downloadUnits(SDL_GPUDevice* device, SDL_GPUBuffer* source, std::uint32_t count,
                   std::vector<BaseUnitData>& out);

} // namespace horde::sim
