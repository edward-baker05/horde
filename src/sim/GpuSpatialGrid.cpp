#include "sim/GpuSpatialGrid.hpp"

#include <SDL3/SDL_log.h>

#include <algorithm>
#include <cmath>

#include "gfx/GpuContext.hpp"
#include "gfx/ShaderLoader.hpp"
#include "logic/UnitData.hpp"
#include "scene/Scene.hpp"

namespace horde::sim {

namespace {

std::uint32_t nextPowerOfTwo(std::uint32_t v) {
    std::uint32_t p = 1;
    while (p < v) {
        p <<= 1;
    }
    return p;
}

std::uint32_t groups(std::uint32_t threads) {
    return std::max<std::uint32_t>(1, (threads + 255) / 256);
}

SDL_GPUBuffer* createBuffer(SDL_GPUDevice* device, Uint32 size) {
    SDL_GPUBufferCreateInfo info{};
    info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    info.size = std::max<Uint32>(size, 16);
    return SDL_CreateGPUBuffer(device, &info);
}

} // namespace

GpuSpatialGrid::~GpuSpatialGrid() {
    shutdown();
}

bool GpuSpatialGrid::init(scene::Services& services, std::uint32_t maxUnits, glm::vec2 worldSize, float cellSize) {
    m_device = services.gpu->device();
    m_cellSize = cellSize;
    m_gridDim = glm::uvec2(static_cast<std::uint32_t>(std::max(1.0f, std::ceil(worldSize.x / cellSize))),
                           static_cast<std::uint32_t>(std::max(1.0f, std::ceil(worldSize.y / cellSize))));
    m_sortSize = nextPowerOfTwo(std::max<std::uint32_t>(maxUnits, 2));

    // Same shaders as gpu-grid. Resource counts: samplers, ro textures, ro
    // buffers, rw textures, rw buffers, uniform buffers, then thread counts.
    gfx::ShaderLoader& shaders = *services.shaders;
    m_clearPipeline = shaders.loadCompute("grid_clear.comp", 0, 0, 0, 0, 1, 1, 256, 1, 1);
    m_buildPipeline = shaders.loadCompute("grid_build.comp", 0, 0, 1, 0, 1, 1, 256, 1, 1);
    m_sortPipeline = shaders.loadCompute("bitonic_sort_step.comp", 0, 0, 0, 0, 1, 1, 256, 1, 1);
    m_rangesPipeline = shaders.loadCompute("grid_find_ranges.comp", 0, 0, 1, 0, 1, 1, 256, 1, 1);

    if (!m_clearPipeline || !m_buildPipeline || !m_sortPipeline || !m_rangesPipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuSpatialGrid: failed to load grid pipelines");
        return false;
    }

    m_keys = createBuffer(m_device, static_cast<Uint32>(sizeof(glm::uvec2) * m_sortSize));
    m_ranges = createBuffer(m_device, static_cast<Uint32>(sizeof(glm::uvec2) * cellCount()));

    if (!m_keys || !m_ranges) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuSpatialGrid: failed to create buffers: %s", SDL_GetError());
        return false;
    }
    return true;
}

void GpuSpatialGrid::shutdown() {
    if (m_device == nullptr) {
        return;
    }

    SDL_GPUComputePipeline** pipelines[] = {&m_clearPipeline, &m_buildPipeline, &m_sortPipeline, &m_rangesPipeline};
    for (SDL_GPUComputePipeline** pipeline : pipelines) {
        if (*pipeline) {
            SDL_ReleaseGPUComputePipeline(m_device, *pipeline);
            *pipeline = nullptr;
        }
    }

    SDL_GPUBuffer** buffers[] = {&m_keys, &m_ranges};
    for (SDL_GPUBuffer** buffer : buffers) {
        if (*buffer) {
            SDL_ReleaseGPUBuffer(m_device, *buffer);
            *buffer = nullptr;
        }
    }

    m_device = nullptr;
}

std::uint32_t GpuSpatialGrid::passCount() const {
    std::uint32_t log2 = 0;
    while ((1u << log2) < m_sortSize) {
        ++log2;
    }
    return 3 + log2 * (log2 + 1) / 2;
}

void GpuSpatialGrid::build(SDL_GPUCommandBuffer* commands, SDL_GPUBuffer* units, std::uint32_t unitCount) {
    if (!m_rangesPipeline || unitCount == 0) {
        return;
    }

    const auto pass = [&](SDL_GPUBuffer* readWrite) {
        SDL_GPUStorageBufferReadWriteBinding binding{};
        binding.buffer = readWrite;
        return SDL_BeginGPUComputePass(commands, nullptr, 0, &binding, 1);
    };

    // 1. Every cell starts empty.
    {
        const GPUGridClearUniforms uniforms{cellCount(), {0, 0, 0}};
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
        if (SDL_GPUComputePass* p = pass(m_ranges)) {
            SDL_BindGPUComputePipeline(p, m_clearPipeline);
            SDL_DispatchGPUCompute(p, groups(cellCount()), 1, 1);
            SDL_EndGPUComputePass(p);
        }
    }

    // 2. One (cellId, unitIndex) key per unit; padding keys sort to the end.
    {
        GPUGridBuildUniforms uniforms{};
        uniforms.worldOrigin = glm::vec2(0.0f);
        uniforms.invCellSize = glm::vec2(1.0f / m_cellSize);
        uniforms.gridDim = m_gridDim;
        uniforms.unitCount = unitCount;
        uniforms.allocatedUnits = m_sortSize;
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
        if (SDL_GPUComputePass* p = pass(m_keys)) {
            SDL_BindGPUComputePipeline(p, m_buildPipeline);
            SDL_BindGPUComputeStorageBuffers(p, 0, &units, 1);
            SDL_DispatchGPUCompute(p, groups(m_sortSize), 1, 1);
            SDL_EndGPUComputePass(p);
        }
    }

    // 3. Sort keys by cell. A pass per step: SDL_GPU orders passes, not dispatches.
    for (std::uint32_t k = 2; k <= m_sortSize; k *= 2) {
        for (std::uint32_t j = k / 2; j > 0; j /= 2) {
            const GPUSortUniforms uniforms{k, j, m_sortSize, 0};
            SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
            if (SDL_GPUComputePass* p = pass(m_keys)) {
                SDL_BindGPUComputePipeline(p, m_sortPipeline);
                SDL_DispatchGPUCompute(p, groups(m_sortSize / 2), 1, 1);
                SDL_EndGPUComputePass(p);
            }
        }
    }

    // 4. Where each cell's run of keys starts and ends.
    {
        const GPURangeUniforms uniforms{unitCount, {0, 0, 0}};
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
        if (SDL_GPUComputePass* p = pass(m_ranges)) {
            SDL_BindGPUComputePipeline(p, m_rangesPipeline);
            SDL_BindGPUComputeStorageBuffers(p, 0, &m_keys, 1);
            SDL_DispatchGPUCompute(p, groups(unitCount), 1, 1);
            SDL_EndGPUComputePass(p);
        }
    }
}

} // namespace horde::sim
