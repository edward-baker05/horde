#include "sim/GpuGridPhysics.hpp"

#include <SDL3/SDL_log.h>

#include <imgui.h>

#include <cmath>

#include "gfx/GpuContext.hpp"
#include "gfx/ShaderLoader.hpp"
#include "logic/UnitData.hpp"
#include "scene/Scene.hpp"
#include "sim/UnitBuffers.hpp"

namespace horde::sim {

namespace {

inline Uint32 nextPowerOfTwo(Uint32 v) {
    if (v == 0) return 1;
    v--;
    v |= v >> 1;
    v |= v >> 2;
    v |= v >> 4;
    v |= v >> 8;
    v |= v >> 16;
    return v + 1;
}

} // namespace

GpuGridPhysics::~GpuGridPhysics() {
    shutdown();
}

bool GpuGridPhysics::init(scene::Services& services, const SimConfig& config) {
    m_device = services.gpu->device();
    m_worldSize = config.worldSize;
    m_unitSize = config.unitSize;
    m_unitCount = static_cast<Uint32>(config.initialUnits.size());

    // Compute grid dimensions and power-of-two allocated unit count for bitonic sort
    m_allocatedUnits = nextPowerOfTwo(m_unitCount);
    const Uint32 gridCols = static_cast<Uint32>(std::ceil(m_worldSize.x / m_unitSize));
    const Uint32 gridRows = static_cast<Uint32>(std::ceil(m_worldSize.y / m_unitSize));
    m_gridDim = glm::uvec2(gridCols, gridRows);
    m_totalCells = gridCols * gridRows;

    // 1. Load GPU Simulation Compute Pipelines
    // 1a. grid_clear.comp: 0 ro buf, 1 rw buf (CellRanges at u0, space1), 1 ubuf (ClearParams at b0, space2)
    m_gridClearPipeline = services.shaders->loadCompute("grid_clear.comp", 0, 0, 0, 0, 1, 1, 256, 1, 1);

    // 1b. grid_build.comp: 1 ro buf (InUnits at t0, space0), 1 rw buf (GridKeys at u0, space1), 1 ubuf (GridBuildParams at b0, space2)
    m_gridBuildPipeline = services.shaders->loadCompute("grid_build.comp", 0, 0, 1, 0, 1, 1, 256, 1, 1);

    // 1c. bitonic_sort_step.comp: 0 ro buf, 1 rw buf (GridKeys at u0, space1), 1 ubuf (SortParams at b0, space2)
    m_bitonicSortPipeline = services.shaders->loadCompute("bitonic_sort_step.comp", 0, 0, 0, 0, 1, 1, 256, 1, 1);

    // 1d. grid_find_ranges.comp: 1 ro buf (SortedKeys at t0, space0), 1 rw buf (CellRanges at u0, space1), 1 ubuf (RangeParams at b0, space2)
    m_gridFindRangesPipeline = services.shaders->loadCompute("grid_find_ranges.comp", 0, 0, 1, 0, 1, 1, 256, 1, 1);

    // 1e. update_positions.comp: 3 ro buf (InUnits at t0, SortedKeys at t1, CellRanges at t2), 1 rw buf (OutUnits at u0), 1 ubuf (SimParams at b0)
    m_computePipeline = services.shaders->loadCompute("update_positions.comp", 0, 0, 3, 0, 1, 1, 256, 1, 1);

    if (!m_gridClearPipeline || !m_gridBuildPipeline || !m_bitonicSortPipeline || !m_gridFindRangesPipeline ||
        !m_computePipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load GPU grid simulation compute pipelines");
        return false;
    }

    // 2. Unit drawing straight from the simulation buffers
    if (!m_renderer.init(services)) {
        return false;
    }

    // 3. Create Double-Buffered GPU Unit Storage Buffers
    const SDL_GPUBufferUsageFlags unitUsage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                                              SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ |
                                              SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    m_unitBuffers[0] = createUnitBuffer(m_device, m_unitCount, unitUsage);
    m_unitBuffers[1] = createUnitBuffer(m_device, m_unitCount, unitUsage);

    if (!m_unitBuffers[0] || !m_unitBuffers[1]) {
        return false;
    }
    m_currentBufferIndex = 0;

    // 4. Create GPU Sort-Based Uniform Grid Buffers
    SDL_GPUBufferCreateInfo keysBufferInfo{};
    keysBufferInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    keysBufferInfo.size = static_cast<Uint32>(sizeof(glm::uvec2) * m_allocatedUnits);
    m_gridKeysBuffer = SDL_CreateGPUBuffer(m_device, &keysBufferInfo);

    SDL_GPUBufferCreateInfo rangesBufferInfo{};
    rangesBufferInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    rangesBufferInfo.size = static_cast<Uint32>(sizeof(glm::uvec2) * m_totalCells);
    m_cellRangesBuffer = SDL_CreateGPUBuffer(m_device, &rangesBufferInfo);

    if (!m_gridKeysBuffer || !m_cellRangesBuffer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grid keys/ranges GPU buffers: %s",
                     SDL_GetError());
        return false;
    }

    // 5. Upload initial unit data to both GPU buffers
    return uploadUnitsNow(m_device, config.initialUnits.data(), m_unitCount, m_unitBuffers, 2);
}

void GpuGridPhysics::shutdown() {
    m_renderer.shutdown();

    if (m_device == nullptr) {
        return;
    }

    SDL_GPUComputePipeline** pipelines[] = {&m_gridClearPipeline, &m_gridBuildPipeline, &m_bitonicSortPipeline,
                                            &m_gridFindRangesPipeline, &m_computePipeline};
    for (SDL_GPUComputePipeline** pipeline : pipelines) {
        if (*pipeline) {
            SDL_ReleaseGPUComputePipeline(m_device, *pipeline);
            *pipeline = nullptr;
        }
    }

    SDL_GPUBuffer** buffers[] = {&m_gridKeysBuffer, &m_cellRangesBuffer, &m_unitBuffers[0], &m_unitBuffers[1]};
    for (SDL_GPUBuffer** buffer : buffers) {
        if (*buffer) {
            SDL_ReleaseGPUBuffer(m_device, *buffer);
            *buffer = nullptr;
        }
    }

    m_device = nullptr;
}

void GpuGridPhysics::update(float dt) {
    m_dt = dt;
}

void GpuGridPhysics::compute(SDL_GPUCommandBuffer* commands) {
    if (!m_computePipeline || !m_unitBuffers[0] || !m_unitBuffers[1] || m_unitCount == 0 || !m_gridKeysBuffer ||
        !m_cellRangesBuffer) {
        return;
    }

    const Uint32 srcIndex = m_currentBufferIndex;
    const Uint32 dstIndex = 1 - m_currentBufferIndex;
    const glm::vec2 invCellSize{1.0f / m_unitSize, 1.0f / m_unitSize};

    // Stage 1: Grid Clear (reset all cell ranges to 0xFFFFFFFF)
    {
        GPUGridClearUniforms clearUniforms{m_totalCells, {0, 0, 0}};
        SDL_PushGPUComputeUniformData(commands, 0, &clearUniforms, sizeof(clearUniforms));

        SDL_GPUStorageBufferReadWriteBinding rwBinding{};
        rwBinding.buffer = m_cellRangesBuffer;
        rwBinding.cycle = false;

        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &rwBinding, 1);
        if (pass != nullptr) {
            SDL_BindGPUComputePipeline(pass, m_gridClearPipeline);
            const Uint32 groupCount = (m_totalCells + 255) / 256;
            SDL_DispatchGPUCompute(pass, groupCount, 1, 1);
            SDL_EndGPUComputePass(pass);
        }
    }

    // Stage 2: Grid Build (hash unit positions into cellId, write (cellId, unitId) pairs)
    {
        GPUGridBuildUniforms buildUniforms{};
        buildUniforms.worldOrigin = glm::vec2{0.0f, 0.0f};
        buildUniforms.invCellSize = invCellSize;
        buildUniforms.gridDim = m_gridDim;
        buildUniforms.unitCount = m_unitCount;
        buildUniforms.allocatedUnits = m_allocatedUnits;
        SDL_PushGPUComputeUniformData(commands, 0, &buildUniforms, sizeof(buildUniforms));

        SDL_GPUStorageBufferReadWriteBinding rwBinding{};
        rwBinding.buffer = m_gridKeysBuffer;
        rwBinding.cycle = false;

        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &rwBinding, 1);
        if (pass != nullptr) {
            SDL_BindGPUComputePipeline(pass, m_gridBuildPipeline);
            SDL_BindGPUComputeStorageBuffers(pass, 0, &m_unitBuffers[srcIndex], 1);

            const Uint32 groupCount = (m_allocatedUnits + 255) / 256;
            SDL_DispatchGPUCompute(pass, groupCount, 1, 1);
            SDL_EndGPUComputePass(pass);
        }
    }

    // Stage 3: Bitonic Sort (sort GridKeys by cellId ascending)
    for (Uint32 k = 2; k <= m_allocatedUnits; k *= 2) {
        for (Uint32 j = k / 2; j > 0; j /= 2) {
            GPUSortUniforms sortUniforms{k, j, m_allocatedUnits, 0};
            SDL_PushGPUComputeUniformData(commands, 0, &sortUniforms, sizeof(sortUniforms));

            SDL_GPUStorageBufferReadWriteBinding rwBinding{};
            rwBinding.buffer = m_gridKeysBuffer;
            rwBinding.cycle = false;

            SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &rwBinding, 1);
            if (pass != nullptr) {
                SDL_BindGPUComputePipeline(pass, m_bitonicSortPipeline);
                const Uint32 groupCount = (m_allocatedUnits / 2 + 255) / 256;
                SDL_DispatchGPUCompute(pass, groupCount, 1, 1);
                SDL_EndGPUComputePass(pass);
            }
        }
    }

    // Stage 4: Find Cell Ranges (detect cell start and end offsets in sorted keys)
    {
        GPURangeUniforms rangeUniforms{m_unitCount, {0, 0, 0}};
        SDL_PushGPUComputeUniformData(commands, 0, &rangeUniforms, sizeof(rangeUniforms));

        SDL_GPUStorageBufferReadWriteBinding rwBinding{};
        rwBinding.buffer = m_cellRangesBuffer;
        rwBinding.cycle = false;

        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &rwBinding, 1);
        if (pass != nullptr) {
            SDL_BindGPUComputePipeline(pass, m_gridFindRangesPipeline);
            SDL_BindGPUComputeStorageBuffers(pass, 0, &m_gridKeysBuffer, 1);

            const Uint32 groupCount = (m_unitCount + 255) / 256;
            SDL_DispatchGPUCompute(pass, groupCount, 1, 1);
            SDL_EndGPUComputePass(pass);
        }
    }

    // Stage 5: Update Positions & Uniform Grid Collision Resolution
    {
        GPUSimUniforms simParams{};
        simParams.worldOrigin = glm::vec2{0.0f, 0.0f};
        simParams.worldBounds = m_worldSize - glm::vec2(m_unitSize, m_unitSize);
        simParams.dt = m_dt;
        simParams.unitCount = m_unitCount;
        simParams.restitution = 0.8f;
        simParams.gravity = 98.0f;
        simParams.unitSize = m_unitSize;
        simParams.cellSize = m_unitSize;
        simParams.gridDim = m_gridDim;
        simParams.invCellSize = invCellSize;
        simParams.padding = glm::vec2{0.0f, 0.0f};
        SDL_PushGPUComputeUniformData(commands, 0, &simParams, sizeof(simParams));

        SDL_GPUStorageBufferReadWriteBinding rwBinding{};
        rwBinding.buffer = m_unitBuffers[dstIndex];
        rwBinding.cycle = false;

        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &rwBinding, 1);
        if (pass != nullptr) {
            SDL_BindGPUComputePipeline(pass, m_computePipeline);

            SDL_GPUBuffer* roBuffers[3] = {m_unitBuffers[srcIndex], m_gridKeysBuffer, m_cellRangesBuffer};
            SDL_BindGPUComputeStorageBuffers(pass, 0, roBuffers, 3);

            const Uint32 groupCountX = (m_unitCount + 255) / 256;
            SDL_DispatchGPUCompute(pass, groupCountX, 1, 1);
            SDL_EndGPUComputePass(pass);
        }
    }

    m_currentBufferIndex = dstIndex;
}

void GpuGridPhysics::renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass,
                                const glm::mat4& viewProjection) {
    m_renderer.draw(commands, pass, m_unitBuffers[m_currentBufferIndex], m_unitCount, viewProjection, m_unitSize);
}

bool GpuGridPhysics::readUnits(std::vector<BaseUnitData>& out) {
    return m_device && downloadUnits(m_device, m_unitBuffers[m_currentBufferIndex], m_unitCount, out);
}

void GpuGridPhysics::debugUi() {
    ImGui::Text("Grid Dim: %u x %u (%u cells)", m_gridDim.x, m_gridDim.y, m_totalCells);
    ImGui::Text("Allocated Keys: %u (padded for bitonic sort)", m_allocatedUnits);
}

} // namespace horde::sim
