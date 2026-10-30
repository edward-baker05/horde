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

constexpr std::uint32_t kScanBlock = 512; // elements per scan_blocks.comp workgroup

std::uint32_t groups(std::uint32_t threads) {
    return std::max<std::uint32_t>(1, (threads + 255) / 256);
}

SDL_GPUBuffer* createBuffer(SDL_GPUDevice* device, std::uint64_t size) {
    SDL_GPUBufferCreateInfo info{};
    info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    info.size = static_cast<Uint32>(std::max<std::uint64_t>(size, 16));
    return SDL_CreateGPUBuffer(device, &info);
}

// Matches cbuffer FillParams in buffer_fill.comp.hlsl
struct alignas(16) GPUFillUniforms {
    Uint32 count;
    Uint32 value;
    Uint32 stride;
    Uint32 padding;
};

// Matches cbuffer CountParams in grid_count.comp.hlsl
struct alignas(16) GPUGridCountUniforms {
    glm::uvec2 gridDim;
    float invCellSize;
    Uint32 unitCount;
};

// Matches cbuffer ScanParams in scan_blocks.comp.hlsl and scan_add.comp.hlsl
struct alignas(16) GPUScanUniforms {
    Uint32 count;
    Uint32 padding[3];
};

// Matches cbuffer ScatterParams in grid_scatter.comp.hlsl
struct alignas(16) GPUScatterUniforms {
    Uint32 unitCount;
    Uint32 cellCount;
    Uint32 padding[2];
};

// One compute pass with the given read-write buffers.
SDL_GPUComputePass* beginPass(SDL_GPUCommandBuffer* commands, std::initializer_list<SDL_GPUBuffer*> readWrite) {
    SDL_GPUStorageBufferReadWriteBinding bindings[4]{};
    Uint32 n = 0;
    for (SDL_GPUBuffer* buffer : readWrite) {
        bindings[n++].buffer = buffer;
    }
    return SDL_BeginGPUComputePass(commands, nullptr, 0, bindings, n);
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
    const std::uint32_t cells = cellCount();
    maxUnits = std::max<std::uint32_t>(maxUnits, 1);

    // Resource counts: samplers, ro textures, ro buffers, rw textures, rw
    // buffers, uniform buffers, then thread counts.
    gfx::ShaderLoader& shaders = *services.shaders;
    bool ok = true;

    m_fillPipeline = shaders.loadCompute("buffer_fill.comp", 0, 0, 0, 0, 1, 1, 256, 1, 1);
    m_countPipeline = shaders.loadCompute("grid_count.comp", 0, 0, 1, 0, 2, 1, 256, 1, 1);
    m_scanPipeline = shaders.loadCompute("scan_blocks.comp", 0, 0, 0, 0, 2, 1, 256, 1, 1);
    m_scanAddPipeline = shaders.loadCompute("scan_add.comp", 0, 0, 1, 0, 1, 1, 256, 1, 1);
    m_scatterPipeline = shaders.loadCompute("grid_scatter.comp", 0, 0, 3, 0, 2, 1, 256, 1, 1);
    m_keys = createBuffer(m_device, sizeof(glm::uvec2) * std::uint64_t{maxUnits});
    m_ranges = createBuffer(m_device, sizeof(glm::uvec2) * std::uint64_t{cells});
    m_counts = createBuffer(m_device, sizeof(std::uint32_t) * std::uint64_t{cells});
    m_unitCells = createBuffer(m_device, sizeof(glm::uvec2) * std::uint64_t{maxUnits});
    m_scanScratch = createBuffer(m_device, sizeof(std::uint32_t));

    for (std::uint32_t count = cells;;) {
        ScanLevel level{createBuffer(m_device, sizeof(std::uint32_t) * std::uint64_t{count}), count};
        ok = ok && level.buffer != nullptr;
        m_scanLevels.push_back(level);
        if (count <= kScanBlock) {
            break;
        }
        count = (count + kScanBlock - 1) / kScanBlock;
    }

    ok = ok && m_fillPipeline && m_countPipeline && m_scanPipeline && m_scanAddPipeline && m_scatterPipeline && m_keys &&
         m_ranges && m_counts && m_unitCells && m_scanScratch;

    if (!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuSpatialGrid: failed to create pipelines or buffers: %s",
                     SDL_GetError());
    }
    return ok;
}

void GpuSpatialGrid::shutdown() {
    if (m_device == nullptr) {
        return;
    }

    SDL_GPUComputePipeline** pipelines[] = {&m_fillPipeline, &m_countPipeline, &m_scanPipeline, &m_scanAddPipeline,
                                            &m_scatterPipeline};
    for (SDL_GPUComputePipeline** pipeline : pipelines) {
        if (*pipeline) {
            SDL_ReleaseGPUComputePipeline(m_device, *pipeline);
            *pipeline = nullptr;
        }
    }

    SDL_GPUBuffer** buffers[] = {&m_keys, &m_ranges, &m_counts, &m_unitCells, &m_scanScratch};
    for (SDL_GPUBuffer** buffer : buffers) {
        if (*buffer) {
            SDL_ReleaseGPUBuffer(m_device, *buffer);
            *buffer = nullptr;
        }
    }
    for (ScanLevel& level : m_scanLevels) {
        if (level.buffer) {
            SDL_ReleaseGPUBuffer(m_device, level.buffer);
        }
    }
    m_scanLevels.clear();

    m_device = nullptr;
}

std::uint32_t GpuSpatialGrid::passCount() const {
    // fill, count, copy, one scan per level, one add per level but the last, scatter
    return 4 + 2 * static_cast<std::uint32_t>(m_scanLevels.size()) - 1;
}

void GpuSpatialGrid::fill(SDL_GPUCommandBuffer* commands, SDL_GPUBuffer* buffer, std::uint32_t count,
                          std::uint32_t value, std::uint32_t stride) {
    const GPUFillUniforms uniforms{count, value, stride, 0};
    SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
    if (SDL_GPUComputePass* p = beginPass(commands, {buffer})) {
        SDL_BindGPUComputePipeline(p, m_fillPipeline);
        SDL_DispatchGPUCompute(p, groups(count), 1, 1);
        SDL_EndGPUComputePass(p);
    }
}

void GpuSpatialGrid::build(SDL_GPUCommandBuffer* commands, SDL_GPUBuffer* units, std::uint32_t unitCount) {
    if (unitCount == 0) {
        return;
    }
    const std::uint32_t cells = cellCount();

    // 1. Zero the per-cell counts.
    fill(commands, m_counts, cells, 0, 1);

    // 2. Count units per cell; each unit remembers its (cell, slot).
    {
        const GPUGridCountUniforms uniforms{m_gridDim, 1.0f / m_cellSize, unitCount};
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
        if (SDL_GPUComputePass* p = beginPass(commands, {m_counts, m_unitCells})) {
            SDL_BindGPUComputePipeline(p, m_countPipeline);
            SDL_BindGPUComputeStorageBuffers(p, 0, &units, 1);
            SDL_DispatchGPUCompute(p, groups(unitCount), 1, 1);
            SDL_EndGPUComputePass(p);
        }
    }

    // 3. Starts = exclusive prefix sum of counts, scanned in place in a copy.
    if (SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(commands)) {
        SDL_GPUBufferLocation src{m_counts, 0};
        SDL_GPUBufferLocation dst{m_scanLevels[0].buffer, 0};
        SDL_CopyGPUBufferToBuffer(copy, &src, &dst, static_cast<Uint32>(sizeof(std::uint32_t) * cells), false);
        SDL_EndGPUCopyPass(copy);
    }

    // Scan each level in blocks, writing block sums into the next level...
    for (std::size_t i = 0; i < m_scanLevels.size(); ++i) {
        const ScanLevel& level = m_scanLevels[i];
        SDL_GPUBuffer* sums = i + 1 < m_scanLevels.size() ? m_scanLevels[i + 1].buffer : m_scanScratch;

        const GPUScanUniforms uniforms{level.count, {0, 0, 0}};
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
        if (SDL_GPUComputePass* p = beginPass(commands, {level.buffer, sums})) {
            SDL_BindGPUComputePipeline(p, m_scanPipeline);
            SDL_DispatchGPUCompute(p, (level.count + kScanBlock - 1) / kScanBlock, 1, 1);
            SDL_EndGPUComputePass(p);
        }
    }

    // ...then add each level's scanned block offsets back into the one below.
    for (std::size_t i = m_scanLevels.size() - 1; i-- > 0;) {
        const ScanLevel& level = m_scanLevels[i];
        const GPUScanUniforms uniforms{level.count, {0, 0, 0}};
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
        if (SDL_GPUComputePass* p = beginPass(commands, {level.buffer})) {
            SDL_BindGPUComputePipeline(p, m_scanAddPipeline);
            SDL_BindGPUComputeStorageBuffers(p, 0, &m_scanLevels[i + 1].buffer, 1);
            SDL_DispatchGPUCompute(p, groups(level.count), 1, 1);
            SDL_EndGPUComputePass(p);
        }
    }

    // 4. Place every key and record every cell's range.
    {
        const GPUScatterUniforms uniforms{unitCount, cells, {0, 0}};
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
        if (SDL_GPUComputePass* p = beginPass(commands, {m_keys, m_ranges})) {
            SDL_BindGPUComputePipeline(p, m_scatterPipeline);
            SDL_GPUBuffer* readOnly[3] = {m_unitCells, m_scanLevels[0].buffer, m_counts};
            SDL_BindGPUComputeStorageBuffers(p, 0, readOnly, 3);
            SDL_DispatchGPUCompute(p, groups(std::max(unitCount, cells)), 1, 1);
            SDL_EndGPUComputePass(p);
        }
    }
}

} // namespace horde::sim
