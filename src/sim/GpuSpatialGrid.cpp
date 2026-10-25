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

// Matches cbuffer BucketParams in bucket_insert.comp.hlsl
struct alignas(16) GPUBucketUniforms {
    glm::uvec2 gridDim;
    float invCellSize;
    Uint32 unitCount;
    Uint32 capacity;
    Uint32 padding[3];
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

bool GpuSpatialGrid::init(scene::Services& services, std::uint32_t maxUnits, glm::vec2 worldSize, float cellSize,
                          Broadphase method, std::uint32_t bucketCapacity) {
    m_device = services.gpu->device();
    m_method = method;
    m_cellSize = cellSize;
    m_bucketCapacity = std::max<std::uint32_t>(bucketCapacity, 1);
    m_gridDim = glm::uvec2(static_cast<std::uint32_t>(std::max(1.0f, std::ceil(worldSize.x / cellSize))),
                           static_cast<std::uint32_t>(std::max(1.0f, std::ceil(worldSize.y / cellSize))));
    const std::uint32_t cells = cellCount();
    maxUnits = std::max<std::uint32_t>(maxUnits, 1);

    // Resource counts: samplers, ro textures, ro buffers, rw textures, rw
    // buffers, uniform buffers, then thread counts.
    gfx::ShaderLoader& shaders = *services.shaders;
    bool ok = true;

    switch (method) {
        case Broadphase::BitonicSort:
            m_sortSize = nextPowerOfTwo(std::max<std::uint32_t>(maxUnits, 2));
            m_clearPipeline = shaders.loadCompute("grid_clear.comp", 0, 0, 0, 0, 1, 1, 256, 1, 1);
            m_buildPipeline = shaders.loadCompute("grid_build.comp", 0, 0, 1, 0, 1, 1, 256, 1, 1);
            m_sortPipeline = shaders.loadCompute("bitonic_sort_step.comp", 0, 0, 0, 0, 1, 1, 256, 1, 1);
            m_rangesPipeline = shaders.loadCompute("grid_find_ranges.comp", 0, 0, 1, 0, 1, 1, 256, 1, 1);
            m_keys = createBuffer(m_device, sizeof(glm::uvec2) * std::uint64_t{m_sortSize});
            m_ranges = createBuffer(m_device, sizeof(glm::uvec2) * std::uint64_t{cells});
            ok = m_clearPipeline && m_buildPipeline && m_sortPipeline && m_rangesPipeline && m_keys && m_ranges;
            break;

        case Broadphase::CountingSort: {
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

            ok = ok && m_fillPipeline && m_countPipeline && m_scanPipeline && m_scanAddPipeline && m_scatterPipeline &&
                 m_keys && m_ranges && m_counts && m_unitCells && m_scanScratch;
            break;
        }

        case Broadphase::Buckets:
            m_fillPipeline = shaders.loadCompute("buffer_fill.comp", 0, 0, 0, 0, 1, 1, 256, 1, 1);
            m_bucketPipeline = shaders.loadCompute("bucket_insert.comp", 0, 0, 1, 0, 1, 1, 256, 1, 1);
            m_buckets = createBuffer(m_device, sizeof(std::uint32_t) * std::uint64_t{cells} * (m_bucketCapacity + 1));
            ok = m_fillPipeline && m_bucketPipeline && m_buckets;
            break;
    }

    if (!ok) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "GpuSpatialGrid (%s): failed to create pipelines or buffers: %s",
                     broadphaseName(method), SDL_GetError());
    }
    return ok;
}

void GpuSpatialGrid::shutdown() {
    if (m_device == nullptr) {
        return;
    }

    SDL_GPUComputePipeline** pipelines[] = {&m_clearPipeline,   &m_buildPipeline,   &m_sortPipeline,
                                            &m_rangesPipeline,  &m_fillPipeline,    &m_countPipeline,
                                            &m_scanPipeline,    &m_scanAddPipeline, &m_scatterPipeline,
                                            &m_bucketPipeline};
    for (SDL_GPUComputePipeline** pipeline : pipelines) {
        if (*pipeline) {
            SDL_ReleaseGPUComputePipeline(m_device, *pipeline);
            *pipeline = nullptr;
        }
    }

    SDL_GPUBuffer** buffers[] = {&m_keys, &m_ranges, &m_counts, &m_unitCells, &m_scanScratch, &m_buckets};
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

SDL_GPUBuffer* GpuSpatialGrid::gridA() const {
    return m_method == Broadphase::Buckets ? m_buckets : m_keys;
}

SDL_GPUBuffer* GpuSpatialGrid::gridB() const {
    return m_method == Broadphase::Buckets ? m_buckets : m_ranges;
}

std::uint32_t GpuSpatialGrid::passCount() const {
    switch (m_method) {
        case Broadphase::BitonicSort: {
            std::uint32_t log2 = 0;
            while ((1u << log2) < m_sortSize) {
                ++log2;
            }
            return 3 + log2 * (log2 + 1) / 2;
        }
        case Broadphase::CountingSort:
            // fill, count, copy, one scan per level, one add per level but the last, scatter
            return 4 + 2 * static_cast<std::uint32_t>(m_scanLevels.size()) - 1;
        case Broadphase::Buckets:
            return 2;
    }
    return 0;
}

void GpuSpatialGrid::build(SDL_GPUCommandBuffer* commands, SDL_GPUBuffer* units, std::uint32_t unitCount) {
    if (unitCount == 0) {
        return;
    }
    switch (m_method) {
        case Broadphase::BitonicSort:
            buildBitonic(commands, units, unitCount);
            break;
        case Broadphase::CountingSort:
            buildCounting(commands, units, unitCount);
            break;
        case Broadphase::Buckets:
            buildBuckets(commands, units, unitCount);
            break;
    }
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

void GpuSpatialGrid::buildBitonic(SDL_GPUCommandBuffer* commands, SDL_GPUBuffer* units, std::uint32_t unitCount) {
    // 1. Every cell starts empty.
    {
        const GPUGridClearUniforms uniforms{cellCount(), {0, 0, 0}};
        SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
        if (SDL_GPUComputePass* p = beginPass(commands, {m_ranges})) {
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
        if (SDL_GPUComputePass* p = beginPass(commands, {m_keys})) {
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
            if (SDL_GPUComputePass* p = beginPass(commands, {m_keys})) {
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
        if (SDL_GPUComputePass* p = beginPass(commands, {m_ranges})) {
            SDL_BindGPUComputePipeline(p, m_rangesPipeline);
            SDL_BindGPUComputeStorageBuffers(p, 0, &m_keys, 1);
            SDL_DispatchGPUCompute(p, groups(unitCount), 1, 1);
            SDL_EndGPUComputePass(p);
        }
    }
}

void GpuSpatialGrid::buildCounting(SDL_GPUCommandBuffer* commands, SDL_GPUBuffer* units, std::uint32_t unitCount) {
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

void GpuSpatialGrid::buildBuckets(SDL_GPUCommandBuffer* commands, SDL_GPUBuffer* units, std::uint32_t unitCount) {
    // 1. Zero every bucket's count (the first uint of each cell's run).
    fill(commands, m_buckets, cellCount(), 0, m_bucketCapacity + 1);

    // 2. Every unit appends itself to its cell's bucket.
    GPUBucketUniforms uniforms{};
    uniforms.gridDim = m_gridDim;
    uniforms.invCellSize = 1.0f / m_cellSize;
    uniforms.unitCount = unitCount;
    uniforms.capacity = m_bucketCapacity;
    SDL_PushGPUComputeUniformData(commands, 0, &uniforms, sizeof(uniforms));
    if (SDL_GPUComputePass* p = beginPass(commands, {m_buckets})) {
        SDL_BindGPUComputePipeline(p, m_bucketPipeline);
        SDL_BindGPUComputeStorageBuffers(p, 0, &units, 1);
        SDL_DispatchGPUCompute(p, groups(unitCount), 1, 1);
        SDL_EndGPUComputePass(p);
    }
}

} // namespace horde::sim
