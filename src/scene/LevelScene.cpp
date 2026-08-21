#include "LevelScene.hpp"

#include <SDL3/SDL_log.h>
#include <SDL3/SDL_timer.h>

#include <imgui.h>
#include <immintrin.h>

#include <optional>
#include <string>

#include "core/Paths.hpp"
#include "gfx/GpuContext.hpp"
#include "gfx/LevelRenderer.hpp"
#include "gfx/ShaderLoader.hpp"
#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
#include "logic/LevelIO.hpp"
#include "scene/SceneStack.hpp"

namespace horde::scene {

LevelScene::~LevelScene() {
    m_simRunning = false;
    if (m_simThread.joinable()) {
        m_simThread.request_stop();
        m_simThread.join();
    }
    releaseGpuResources();
}

void LevelScene::releaseGpuResources() {
    if (m_services == nullptr || m_services->gpu == nullptr || m_services->gpu->device() == nullptr) {
        return;
    }
    SDL_GPUDevice* device = m_services->gpu->device();

    if (m_unitPipeline != nullptr) {
        SDL_ReleaseGPUGraphicsPipeline(device, m_unitPipeline);
        m_unitPipeline = nullptr;
    }
    if (m_unitSampler != nullptr) {
        SDL_ReleaseGPUSampler(device, m_unitSampler);
        m_unitSampler = nullptr;
    }

    if (m_clearGridPipeline != nullptr) {
        SDL_ReleaseGPUComputePipeline(device, m_clearGridPipeline);
        m_clearGridPipeline = nullptr;
    }
    if (m_updatePositionsPipeline != nullptr) {
        SDL_ReleaseGPUComputePipeline(device, m_updatePositionsPipeline);
        m_updatePositionsPipeline = nullptr;
    }
    if (m_prefixSumBlocksPipeline != nullptr) {
        SDL_ReleaseGPUComputePipeline(device, m_prefixSumBlocksPipeline);
        m_prefixSumBlocksPipeline = nullptr;
    }
    if (m_prefixSumTopPipeline != nullptr) {
        SDL_ReleaseGPUComputePipeline(device, m_prefixSumTopPipeline);
        m_prefixSumTopPipeline = nullptr;
    }
    if (m_prefixSumAddPipeline != nullptr) {
        SDL_ReleaseGPUComputePipeline(device, m_prefixSumAddPipeline);
        m_prefixSumAddPipeline = nullptr;
    }
    if (m_reorderUnitsPipeline != nullptr) {
        SDL_ReleaseGPUComputePipeline(device, m_reorderUnitsPipeline);
        m_reorderUnitsPipeline = nullptr;
    }
    if (m_resolveCollisionsPipeline != nullptr) {
        SDL_ReleaseGPUComputePipeline(device, m_resolveCollisionsPipeline);
        m_resolveCollisionsPipeline = nullptr;
    }

    if (m_gpuPositions != nullptr) {
        SDL_ReleaseGPUBuffer(device, m_gpuPositions);
        m_gpuPositions = nullptr;
    }
    if (m_gpuSortedPositions != nullptr) {
        SDL_ReleaseGPUBuffer(device, m_gpuSortedPositions);
        m_gpuSortedPositions = nullptr;
    }
    if (m_gpuVelocities != nullptr) {
        SDL_ReleaseGPUBuffer(device, m_gpuVelocities);
        m_gpuVelocities = nullptr;
    }
    if (m_gpuSortedVelocities != nullptr) {
        SDL_ReleaseGPUBuffer(device, m_gpuSortedVelocities);
        m_gpuSortedVelocities = nullptr;
    }
    if (m_gpuUnitStates != nullptr) {
        SDL_ReleaseGPUBuffer(device, m_gpuUnitStates);
        m_gpuUnitStates = nullptr;
    }
    if (m_gpuSortedUnitStates != nullptr) {
        SDL_ReleaseGPUBuffer(device, m_gpuSortedUnitStates);
        m_gpuSortedUnitStates = nullptr;
    }
    if (m_gpuCellKeys != nullptr) {
        SDL_ReleaseGPUBuffer(device, m_gpuCellKeys);
        m_gpuCellKeys = nullptr;
    }
    if (m_gpuUnitLocalIndices != nullptr) {
        SDL_ReleaseGPUBuffer(device, m_gpuUnitLocalIndices);
        m_gpuUnitLocalIndices = nullptr;
    }

    if (m_gpuCellCounts != nullptr) {
        SDL_ReleaseGPUBuffer(device, m_gpuCellCounts);
        m_gpuCellCounts = nullptr;
    }
    if (m_gpuCellStarts != nullptr) {
        SDL_ReleaseGPUBuffer(device, m_gpuCellStarts);
        m_gpuCellStarts = nullptr;
    }
    if (m_gpuCellEnds != nullptr) {
        SDL_ReleaseGPUBuffer(device, m_gpuCellEnds);
        m_gpuCellEnds = nullptr;
    }
    if (m_gpuBlockSums != nullptr) {
        SDL_ReleaseGPUBuffer(device, m_gpuBlockSums);
        m_gpuBlockSums = nullptr;
    }
    m_gpuGridCapacityCells = 0;

    if (m_gpuPosUpload != nullptr) {
        SDL_ReleaseGPUTransferBuffer(device, m_gpuPosUpload);
        m_gpuPosUpload = nullptr;
    }
    if (m_gpuVelUpload != nullptr) {
        SDL_ReleaseGPUTransferBuffer(device, m_gpuVelUpload);
        m_gpuVelUpload = nullptr;
    }
    if (m_gpuStatesUpload != nullptr) {
        SDL_ReleaseGPUTransferBuffer(device, m_gpuStatesUpload);
        m_gpuStatesUpload = nullptr;
    }
    if (m_gpuPosDownload != nullptr) {
        SDL_ReleaseGPUTransferBuffer(device, m_gpuPosDownload);
        m_gpuPosDownload = nullptr;
    }
}

void LevelScene::rebuildGridBuffers() {
    if (m_services == nullptr || m_services->gpu == nullptr || m_services->gpu->device() == nullptr) {
        return;
    }
    SDL_GPUDevice* device = m_services->gpu->device();
    const size_t numCells = static_cast<size_t>(unit_manager.GetGridCols()) * static_cast<size_t>(unit_manager.GetGridRows());
    const size_t requiredCapacity = std::max<size_t>(numCells, 262144);

    if (m_gpuCellCounts == nullptr || m_gpuGridCapacityCells < requiredCapacity) {
        if (m_gpuCellCounts != nullptr) {
            SDL_ReleaseGPUBuffer(device, m_gpuCellCounts);
            m_gpuCellCounts = nullptr;
        }
        if (m_gpuCellStarts != nullptr) {
            SDL_ReleaseGPUBuffer(device, m_gpuCellStarts);
            m_gpuCellStarts = nullptr;
        }
        if (m_gpuCellEnds != nullptr) {
            SDL_ReleaseGPUBuffer(device, m_gpuCellEnds);
            m_gpuCellEnds = nullptr;
        }
        if (m_gpuBlockSums != nullptr) {
            SDL_ReleaseGPUBuffer(device, m_gpuBlockSums);
            m_gpuBlockSums = nullptr;
        }

        m_gpuGridCapacityCells = requiredCapacity;
        const Uint32 capacityBytes = static_cast<Uint32>(sizeof(uint32_t) * m_gpuGridCapacityCells);
        const Uint32 numBlocks = static_cast<Uint32>((m_gpuGridCapacityCells + 511) / 512);
        const Uint32 blockSumsBytes = static_cast<Uint32>(sizeof(uint32_t) * std::max<size_t>(numBlocks, 2048));

        SDL_GPUBufferCreateInfo countBufInfo{};
        countBufInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
        countBufInfo.size = capacityBytes;

        m_gpuCellCounts = SDL_CreateGPUBuffer(device, &countBufInfo);
        m_gpuCellStarts = SDL_CreateGPUBuffer(device, &countBufInfo);
        m_gpuCellEnds = SDL_CreateGPUBuffer(device, &countBufInfo);

        SDL_GPUBufferCreateInfo blockSumsBufInfo{};
        blockSumsBufInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
        blockSumsBufInfo.size = blockSumsBytes;
        m_gpuBlockSums = SDL_CreateGPUBuffer(device, &blockSumsBufInfo);
    }
}

void LevelScene::generateBackgroundTexture() {
    if (m_services == nullptr || m_services->gpu == nullptr || m_services->gpu->device() == nullptr) {
        return;
    }
    const Uint32 texW = static_cast<Uint32>(std::clamp(m_level.size.x, 64.0f, 4096.0f));
    const Uint32 texH = static_cast<Uint32>(std::clamp(m_level.size.y, 64.0f, 4096.0f));

    std::vector<Uint32> bgPixels(static_cast<size_t>(texW) * texH, 0xFF141414); // Dark background
    const Uint32 dotColor = 0xFF2A2A2A;

    const float cellSize = unit_manager.GetCellSize();
    const float rowH = unit_manager.GetRowHeight();
    const int gridC = unit_manager.GetGridCols();
    const int gridR = unit_manager.GetGridRows();

    for (int r = 0; r < gridR; ++r) {
        const int y = static_cast<int>(r * rowH);
        if (y >= static_cast<int>(texH)) break;
        const float xOffset = (r % 2 == 1) ? (0.5f * cellSize) : 0.0f;
        for (int c = 0; c < gridC; ++c) {
            const int x = static_cast<int>(c * cellSize + xOffset);
            if (x >= 0 && x < static_cast<int>(texW) && y >= 0) {
                bgPixels[x + y * texW] = dotColor;
            }
        }
    }
    m_bgTexture.createFromPixels(m_services->gpu->device(), texW, texH, bgPixels.data());
}

void LevelScene::spawnAllUnits() {
    unit_manager.ClearUnits();

    // Spawn units in true Hexagonal Close-Packing (HCP) aligned with Hex Grid at the TOP of the level
    const float spacing = enemy_size;
    const float rowHeight = spacing * 0.86602540378f; // sqrt(3)/2

    const int targetCount = std::clamp(m_spawnUnitCount, 100, static_cast<int>(MaxUnitsCapacity));

    // Dynamic columns: proportional to target count and world width
    const int maxColsForWorld = std::max(20, static_cast<int>((m_level.size.x - 40.0f) / spacing));
    const int cols = std::min(maxColsForWorld, std::max(120, static_cast<int>(std::sqrt(targetCount * 2.0f))));
    const float blockWidth = cols * spacing;
    const float startX = (m_level.size.x - blockWidth) * 0.5f;
    const float topY = enemy_size + 20.0f;

    for (int i = 0; i < targetCount; ++i) {
        const int c = i % cols;
        const int r = i / cols;
        const float xOffset = (r % 2 == 1) ? (spacing * 0.5f) : 0.0f;
        const float x = startX + c * spacing + xOffset;
        const float y = topY + (r * rowHeight);

        // Tiny initial velocity: slight lateral perturbation and downward drift
        const float angle = static_cast<float>(i) * 0.61803398875f * 6.2831853f;
        const float vx = std::sin(angle) * 0.4f;
        const float vy = 0.5f + std::abs(std::cos(angle)) * 0.5f;

        unit_manager.SpawnUnit(
            glm::vec2(x, y),
            glm::vec2(vx, vy),
            10);
    }

    unit_manager.PublishRenderSnapshot();
    m_needsGpuUpload.store(true, std::memory_order_release);
}

bool LevelScene::onEnter(Services& services) {
    m_services = &services;

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(services.window, &width, &height);
    m_camera.setViewport(static_cast<float>(width), static_cast<float>(height));
    m_camera.setZoomLimits(0.005f, 10.0f);

    m_level = logic::makeDefaultLevel();
    unit_manager.Init(MaxUnitsCapacity, m_level.size, enemy_size);
    rebuildGridBuffers();
    generateBackgroundTexture();
    spawnAllUnits();

    SDL_GPUDevice* device = services.gpu->device();

    // Initialize GPU Compute Pipelines for Counting-Sort Spatial Grid
    m_clearGridPipeline = services.shaders->loadCompute("clear_grid.comp", 0, 0, 0, 0, 3, 1, 64, 1, 1);
    m_updatePositionsPipeline = services.shaders->loadCompute("update_positions.comp", 0, 0, 0, 0, 5, 1, 64, 1, 1);
    m_prefixSumBlocksPipeline = services.shaders->loadCompute("prefix_sum_blocks.comp", 0, 0, 1, 0, 2, 1, 256, 1, 1);
    m_prefixSumTopPipeline = services.shaders->loadCompute("prefix_sum_top.comp", 0, 0, 0, 0, 1, 1, 1024, 1, 1);
    m_prefixSumAddPipeline = services.shaders->loadCompute("prefix_sum_add.comp", 0, 0, 2, 0, 2, 1, 64, 1, 1);
    m_reorderUnitsPipeline = services.shaders->loadCompute("reorder_units.comp", 0, 0, 6, 0, 3, 1, 64, 1, 1);
    m_resolveCollisionsPipeline = services.shaders->loadCompute("resolve_collisions.comp", 0, 0, 2, 0, 3, 1, 64, 1, 1);

    // Initialize GPU Unit Rendering Graphics Pipeline & Sampler
    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    m_unitSampler = SDL_CreateGPUSampler(device, &samplerInfo);

    SDL_GPUShader* vertex = services.shaders->loadGraphics(
        "unit.vert", SDL_GPU_SHADERSTAGE_VERTEX,
        gfx::ShaderResources{.samplers = 0, .storageTextures = 0, .storageBuffers = 3, .uniformBuffers = 1});
    SDL_GPUShader* fragment = services.shaders->loadGraphics(
        "sprite.frag", SDL_GPU_SHADERSTAGE_FRAGMENT,
        gfx::ShaderResources{.samplers = 1, .storageTextures = 0, .storageBuffers = 0, .uniformBuffers = 0});

    if (vertex != nullptr && fragment != nullptr) {
        SDL_GPUColorTargetBlendState blend{};
        blend.enable_blend = true;
        blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
        blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
        blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
        blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
        blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;

        SDL_GPUColorTargetDescription colorTarget{};
        colorTarget.format = services.gpu->swapchainFormat();
        colorTarget.blend_state = blend;

        SDL_GPUGraphicsPipelineCreateInfo pipelineInfo{};
        pipelineInfo.vertex_shader = vertex;
        pipelineInfo.fragment_shader = fragment;
        pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
        pipelineInfo.target_info.num_color_targets = 1;
        pipelineInfo.target_info.color_target_descriptions = &colorTarget;

        m_unitPipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);
    }

    if (vertex) SDL_ReleaseGPUShader(device, vertex);
    if (fragment) SDL_ReleaseGPUShader(device, fragment);

    // Initialize GPU Storage Buffers (Double-buffered for zero-race reordering)
    SDL_GPUBufferCreateInfo posBufInfo{};
    posBufInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    posBufInfo.size = static_cast<Uint32>(sizeof(glm::vec2) * MaxUnitsCapacity);
    m_gpuPositions = SDL_CreateGPUBuffer(device, &posBufInfo);
    m_gpuSortedPositions = SDL_CreateGPUBuffer(device, &posBufInfo);

    SDL_GPUBufferCreateInfo velBufInfo{};
    velBufInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    velBufInfo.size = static_cast<Uint32>(sizeof(glm::vec2) * MaxUnitsCapacity);
    m_gpuVelocities = SDL_CreateGPUBuffer(device, &velBufInfo);
    m_gpuSortedVelocities = SDL_CreateGPUBuffer(device, &velBufInfo);

    SDL_GPUBufferCreateInfo statesBufInfo{};
    statesBufInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ | SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    statesBufInfo.size = static_cast<Uint32>(sizeof(uint32_t) * MaxUnitsCapacity);
    m_gpuUnitStates = SDL_CreateGPUBuffer(device, &statesBufInfo);
    m_gpuSortedUnitStates = SDL_CreateGPUBuffer(device, &statesBufInfo);

    SDL_GPUBufferCreateInfo indexBufInfo{};
    indexBufInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    indexBufInfo.size = static_cast<Uint32>(sizeof(uint32_t) * MaxUnitsCapacity);
    m_gpuCellKeys = SDL_CreateGPUBuffer(device, &indexBufInfo);
    m_gpuUnitLocalIndices = SDL_CreateGPUBuffer(device, &indexBufInfo);

    // Initialize Transfer Buffers
    SDL_GPUTransferBufferCreateInfo uploadInfo{};
    uploadInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    uploadInfo.size = static_cast<Uint32>(sizeof(glm::vec2) * MaxUnitsCapacity);
    m_gpuPosUpload = SDL_CreateGPUTransferBuffer(device, &uploadInfo);
    m_gpuVelUpload = SDL_CreateGPUTransferBuffer(device, &uploadInfo);

    SDL_GPUTransferBufferCreateInfo statesUploadInfo{};
    statesUploadInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
    statesUploadInfo.size = static_cast<Uint32>(sizeof(uint32_t) * MaxUnitsCapacity);
    m_gpuStatesUpload = SDL_CreateGPUTransferBuffer(device, &statesUploadInfo);

    SDL_GPUTransferBufferCreateInfo downloadInfo{};
    downloadInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
    downloadInfo.size = static_cast<Uint32>(sizeof(glm::vec2) * MaxUnitsCapacity);
    m_gpuPosDownload = SDL_CreateGPUTransferBuffer(device, &downloadInfo);

    m_gpuRenderPositions.resize(MaxUnitsCapacity);
    m_needsGpuUpload.store(true, std::memory_order_release);

    // Start Asynchronous Simulation Worker Thread (used when CPU mode is active)
    m_simRunning = true;
    m_simThread = std::jthread([this](std::stop_token stopToken) { simLoop(stopToken); });

    return true;
}

void LevelScene::simLoop(std::stop_token stopToken) {
    while (!stopToken.stop_requested() && m_simRunning) {
        if (m_useGpuSimulation) {
            SDL_Delay(5);
            continue;
        }

        const Uint64 start = SDL_GetTicksNS();

        if (m_needsRespawn.exchange(false, std::memory_order_acq_rel)) {
            spawnAllUnits();
        }

        const float dt = 1.0f / m_targetSimHz;
        unit_manager.UpdatePhysics(dt);
        unit_manager.PublishRenderSnapshot();

        const Uint64 end = SDL_GetTicksNS();
        const float elapsedMs = static_cast<float>(end - start) / 1.0e6f;
        m_lastLogicTimeMs.store(elapsedMs, std::memory_order_relaxed);

        const float targetMs = 1000.0f / m_targetSimHz;
        if (elapsedMs < targetMs) {
            SDL_Delay(static_cast<Uint32>(targetMs - elapsedMs));
        }
    }
}

bool LevelScene::handleEvent(const SDL_Event& event) {
    if (event.type == SDL_EVENT_KEY_DOWN && event.key.key == SDLK_R && !event.key.repeat) {
        m_needsRespawn.store(true, std::memory_order_release);
        return true;
    }
    return m_cameraController.handleEvent(event, m_camera);
}

void LevelScene::update(float dt) {
    // Track true end-to-end frame duration and actual display FPS
    const float currentFrameMs = dt * 1000.0f;
    m_frameTimeHistory[m_frameHistoryOffset] = currentFrameMs;
    m_frameHistoryOffset = (m_frameHistoryOffset + 1) % m_frameTimeHistory.size();

    float minVal = 9999.0f;
    float maxVal = 0.0f;
    float sumVal = 0.0f;
    for (float val : m_frameTimeHistory) {
        if (val > 0.0f) {
            if (val < minVal) minVal = val;
            if (val > maxVal) maxVal = val;
            sumVal += val;
        }
    }
    m_minFrameMs = minVal < 9990.0f ? minVal : 0.0f;
    m_maxFrameMs = maxVal;
    m_avgFrameMs = sumVal / static_cast<float>(m_frameTimeHistory.size());

    // Display smoothing: update text readouts at 5 Hz (every 200ms) for rock-solid readability
    m_displayTimer += dt;
    if (m_displayTimer >= 0.20f) {
        m_displayRealFps = ImGui::GetIO().Framerate;
        m_displayFrameMs = currentFrameMs;
        m_displayMinMs = m_minFrameMs;
        m_displayMaxMs = m_maxFrameMs;
        m_displayAvgMs = m_avgFrameMs;
        m_displayGpuComputeMs = m_lastGpuComputeMs.load(std::memory_order_relaxed);
        m_displayRenderPrepMs = m_lastRenderPrepMs.load(std::memory_order_relaxed);
        m_displayLogicMs = m_lastLogicTimeMs.load(std::memory_order_relaxed);
        m_displayInteriorUnits = unit_manager.sleepingInteriorUnits;
        m_displayEdgeUnits = unit_manager.sleepingEdgeUnits;
        m_displayActiveUnits = unit_manager.activeUnits;
        m_displayTimer = 0.0f;
    }
}

void LevelScene::compute(SDL_GPUCommandBuffer* commands) {
    if (!m_useGpuSimulation || m_updatePositionsPipeline == nullptr || m_resolveCollisionsPipeline == nullptr ||
        m_clearGridPipeline == nullptr || m_prefixSumBlocksPipeline == nullptr ||
        m_prefixSumTopPipeline == nullptr || m_prefixSumAddPipeline == nullptr ||
        m_reorderUnitsPipeline == nullptr || m_gpuCellCounts == nullptr ||
        m_gpuCellStarts == nullptr || m_gpuCellEnds == nullptr || m_gpuBlockSums == nullptr) {
        return;
    }

    const Uint64 stepStart = SDL_GetTicksNS();

    SDL_GPUDevice* device = m_services->gpu->device();

    if (m_needsRespawn.exchange(false, std::memory_order_acq_rel)) {
        rebuildGridBuffers();
        spawnAllUnits();
        m_needsGpuUpload.store(true, std::memory_order_release);
    }

    const size_t unitCount = unit_manager.GetCurrentUnits();
    if (unitCount == 0) {
        return;
    }

    // Initial / On-demand upload of positions & velocities to GPU
    if (m_needsGpuUpload.exchange(false, std::memory_order_acq_rel)) {
        void* mappedPos = SDL_MapGPUTransferBuffer(device, m_gpuPosUpload, true);
        if (mappedPos) {
            std::memcpy(mappedPos, unit_manager.GetPositionsPtr(), unitCount * sizeof(glm::vec2));
            SDL_UnmapGPUTransferBuffer(device, m_gpuPosUpload);
        }

        void* mappedVel = SDL_MapGPUTransferBuffer(device, m_gpuVelUpload, true);
        if (mappedVel) {
            std::memcpy(mappedVel, unit_manager.GetVelocitiesPtr(), unitCount * sizeof(glm::vec2));
            SDL_UnmapGPUTransferBuffer(device, m_gpuVelUpload);
        }

        void* mappedStates = SDL_MapGPUTransferBuffer(device, m_gpuStatesUpload, true);
        if (mappedStates) {
            std::memset(mappedStates, 0, unitCount * sizeof(uint32_t));
            SDL_UnmapGPUTransferBuffer(device, m_gpuStatesUpload);
        }

        SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(commands);
        SDL_GPUTransferBufferLocation srcPosLoc{m_gpuPosUpload, 0};
        SDL_GPUBufferRegion dstPosRegion{m_gpuPositions, 0, static_cast<Uint32>(unitCount * sizeof(glm::vec2))};
        SDL_UploadToGPUBuffer(copyPass, &srcPosLoc, &dstPosRegion, true);

        SDL_GPUTransferBufferLocation srcVelLoc{m_gpuVelUpload, 0};
        SDL_GPUBufferRegion dstVelRegion{m_gpuVelocities, 0, static_cast<Uint32>(unitCount * sizeof(glm::vec2))};
        SDL_UploadToGPUBuffer(copyPass, &srcVelLoc, &dstVelRegion, true);

        SDL_GPUTransferBufferLocation srcStatesLoc{m_gpuStatesUpload, 0};
        SDL_GPUBufferRegion dstStatesRegion{m_gpuUnitStates, 0, static_cast<Uint32>(unitCount * sizeof(uint32_t))};
        SDL_UploadToGPUBuffer(copyPass, &srcStatesLoc, &dstStatesRegion, true);
        SDL_EndGPUCopyPass(copyPass);
    }

    const uint32_t numCells = static_cast<uint32_t>(unit_manager.GetGridCols() * unit_manager.GetGridRows());
    const Uint32 unitGroupCount = static_cast<Uint32>((unitCount + 63) / 64);
    const Uint32 cellGroupCount = static_cast<Uint32>((numCells + 63) / 64);
    const Uint32 blockCount = static_cast<Uint32>((numCells + 511) / 512);

    const int substeps = std::clamp(m_gpuSubsteps, 1, 10);
    const float subDeltaTime = (1.0f / 60.0f) / static_cast<float>(substeps);

    for (int step = 0; step < substeps; ++step) {
        // Pass 1: Clear GPU Spatial Grid Buckets
        {
            SDL_GPUStorageBufferReadWriteBinding clearBindings[3] = {
                {m_gpuCellCounts, false},
                {m_gpuCellStarts, false},
                {m_gpuCellEnds, false}
            };
            SDL_GPUComputePass* clearPass = SDL_BeginGPUComputePass(commands, nullptr, 0, clearBindings, 3);
            if (clearPass) {
                ClearGridUniforms clearUniforms{numCells, {0, 0, 0}};
                SDL_BindGPUComputePipeline(clearPass, m_clearGridPipeline);
                SDL_PushGPUComputeUniformData(commands, 0, &clearUniforms, sizeof(clearUniforms));
                SDL_DispatchGPUCompute(clearPass, cellGroupCount, 1, 1);
                SDL_EndGPUComputePass(clearPass);
            }
        }

        // Pass 2: Update Positions & Atomically Count Cell Occupancy
        {
            SDL_GPUStorageBufferReadWriteBinding updateBindings[5] = {
                {m_gpuPositions, false},
                {m_gpuVelocities, false},
                {m_gpuCellCounts, false},
                {m_gpuCellKeys, false},
                {m_gpuUnitLocalIndices, false}
            };
            SDL_GPUComputePass* updatePass = SDL_BeginGPUComputePass(commands, nullptr, 0, updateBindings, 5);
            if (updatePass) {
                UnitSimulationUniforms simUniforms{};
                simUniforms.deltaTime = subDeltaTime;
                simUniforms.unitCount = static_cast<uint32_t>(unitCount);
                simUniforms.gravity = m_gravity;
                simUniforms.restitution = -m_restitution;
                simUniforms.worldOrigin = glm::vec2(0.0f, 0.0f);
                simUniforms.maxPosition = m_level.size - glm::vec2(enemy_size);
                simUniforms.minVelocity = glm::vec2(-25.0f, -25.0f);
                simUniforms.maxVelocity = glm::vec2(25.0f, 25.0f);
                simUniforms.floorFriction = m_floorFriction;
                simUniforms.unitSize = enemy_size;
                simUniforms.cellSize = unit_manager.GetCellSize();
                simUniforms.invCellSize = 1.0f / simUniforms.cellSize;
                simUniforms.rowHeight = unit_manager.GetRowHeight();
                simUniforms.invRowHeight = 1.0f / simUniforms.rowHeight;
                simUniforms.gridCols = unit_manager.GetGridCols();
                simUniforms.gridRows = unit_manager.GetGridRows();
                simUniforms.maxCol = simUniforms.gridCols - 1;
                simUniforms.maxRow = simUniforms.gridRows - 1;

                SDL_BindGPUComputePipeline(updatePass, m_updatePositionsPipeline);
                SDL_PushGPUComputeUniformData(commands, 0, &simUniforms, sizeof(simUniforms));
                SDL_DispatchGPUCompute(updatePass, unitGroupCount, 1, 1);
                SDL_EndGPUComputePass(updatePass);
            }
        }

        // Pass 3: Parallel Exclusive Prefix Sum on CellCounts (Block Level)
        {
            SDL_GPUStorageBufferReadWriteBinding blockScanBindings[2] = {
                {m_gpuCellStarts, false},
                {m_gpuBlockSums, false}
            };
            SDL_GPUComputePass* blockScanPass = SDL_BeginGPUComputePass(commands, nullptr, 0, blockScanBindings, 2);
            if (blockScanPass) {
                SDL_GPUBuffer* roCounts[1] = {m_gpuCellCounts};
                SDL_BindGPUComputeStorageBuffers(blockScanPass, 0, roCounts, 1);

                ScanUniforms scanUniforms{numCells, {0, 0, 0}};
                SDL_BindGPUComputePipeline(blockScanPass, m_prefixSumBlocksPipeline);
                SDL_PushGPUComputeUniformData(commands, 0, &scanUniforms, sizeof(scanUniforms));
                SDL_DispatchGPUCompute(blockScanPass, blockCount, 1, 1);
                SDL_EndGPUComputePass(blockScanPass);
            }
        }

        // Pass 4: Top-Level Exclusive Prefix Sum on BlockSums
        {
            SDL_GPUStorageBufferReadWriteBinding topScanBindings[1] = {
                {m_gpuBlockSums, false}
            };
            SDL_GPUComputePass* topScanPass = SDL_BeginGPUComputePass(commands, nullptr, 0, topScanBindings, 1);
            if (topScanPass) {
                TopScanUniforms topScanUniforms{blockCount, {0, 0, 0}};
                SDL_BindGPUComputePipeline(topScanPass, m_prefixSumTopPipeline);
                SDL_PushGPUComputeUniformData(commands, 0, &topScanUniforms, sizeof(topScanUniforms));
                SDL_DispatchGPUCompute(topScanPass, 1, 1, 1);
                SDL_EndGPUComputePass(topScanPass);
            }
        }

        // Pass 5: Add Scanned Block Offsets & Compute Exact CellEnds
        {
            SDL_GPUStorageBufferReadWriteBinding addBindings[2] = {
                {m_gpuCellStarts, false},
                {m_gpuCellEnds, false}
            };
            SDL_GPUComputePass* addPass = SDL_BeginGPUComputePass(commands, nullptr, 0, addBindings, 2);
            if (addPass) {
                SDL_GPUBuffer* roBuffers[2] = {m_gpuCellCounts, m_gpuBlockSums};
                SDL_BindGPUComputeStorageBuffers(addPass, 0, roBuffers, 2);

                AddUniforms addUniforms{numCells, {0, 0, 0}};
                SDL_BindGPUComputePipeline(addPass, m_prefixSumAddPipeline);
                SDL_PushGPUComputeUniformData(commands, 0, &addUniforms, sizeof(addUniforms));
                SDL_DispatchGPUCompute(addPass, cellGroupCount, 1, 1);
                SDL_EndGPUComputePass(addPass);
            }
        }

        // Pass 6: Reorder Units into 100% Contiguous Sorted VRAM Layout
        {
            SDL_GPUStorageBufferReadWriteBinding reorderBindings[3] = {
                {m_gpuSortedPositions, false},
                {m_gpuSortedVelocities, false},
                {m_gpuSortedUnitStates, false}
            };
            SDL_GPUComputePass* reorderPass = SDL_BeginGPUComputePass(commands, nullptr, 0, reorderBindings, 3);
            if (reorderPass) {
                SDL_GPUBuffer* roBuffers[6] = {
                    m_gpuPositions,
                    m_gpuVelocities,
                    m_gpuUnitStates,
                    m_gpuCellKeys,
                    m_gpuUnitLocalIndices,
                    m_gpuCellStarts
                };
                SDL_BindGPUComputeStorageBuffers(reorderPass, 0, roBuffers, 6);

                ReorderUniforms reorderUniforms{static_cast<uint32_t>(unitCount), {0, 0, 0}};
                SDL_BindGPUComputePipeline(reorderPass, m_reorderUnitsPipeline);
                SDL_PushGPUComputeUniformData(commands, 0, &reorderUniforms, sizeof(reorderUniforms));
                SDL_DispatchGPUCompute(reorderPass, unitGroupCount, 1, 1);
                SDL_EndGPUComputePass(reorderPass);
            }
        }

        // Pass 7: Resolve Collisions via 100% Contiguous Sorted Ranges
        {
            SDL_GPUStorageBufferReadWriteBinding colRwBindings[3] = {
                {m_gpuSortedPositions, false},
                {m_gpuSortedVelocities, false},
                {m_gpuSortedUnitStates, false}
            };
            SDL_GPUComputePass* colPass = SDL_BeginGPUComputePass(commands, nullptr, 0, colRwBindings, 3);
            if (colPass) {
                SDL_GPUBuffer* roBuffers[2] = {m_gpuCellStarts, m_gpuCellEnds};
                SDL_BindGPUComputeStorageBuffers(colPass, 0, roBuffers, 2);

                UnitCollisionUniforms colUniforms{};
                colUniforms.unitCount = static_cast<uint32_t>(unitCount);
                colUniforms.unitSize = enemy_size;
                colUniforms.desiredDistSq = enemy_size * enemy_size;
                colUniforms.cellSize = unit_manager.GetCellSize();
                colUniforms.invCellSize = 1.0f / colUniforms.cellSize;
                colUniforms.rowHeight = unit_manager.GetRowHeight();
                colUniforms.invRowHeight = 1.0f / colUniforms.rowHeight;
                colUniforms.restitution = -m_restitution;
                colUniforms.gridCols = unit_manager.GetGridCols();
                colUniforms.gridRows = unit_manager.GetGridRows();
                colUniforms.maxCol = colUniforms.gridCols - 1;
                colUniforms.maxRow = colUniforms.gridRows - 1;
                colUniforms.worldOrigin = glm::vec2(0.0f, 0.0f);
                colUniforms.maxPosition = m_level.size - glm::vec2(enemy_size);
                colUniforms.damping = m_collisionDamping;
                colUniforms.maxDisplacement = enemy_size;
                colUniforms.enableSleeping = m_enableGpuSleeping ? 1 : 0;
                colUniforms.deepSleepMinContacts = static_cast<uint32_t>(m_deepSleepMinContacts);
                colUniforms.sleepMaxRelSpeed = m_sleepMaxRelSpeed;
                colUniforms.overRelaxation = m_overRelaxation;
                colUniforms.contactFriction = m_contactFriction;

                SDL_BindGPUComputePipeline(colPass, m_resolveCollisionsPipeline);
                SDL_PushGPUComputeUniformData(commands, 0, &colUniforms, sizeof(colUniforms));

                for (int iter = 0; iter < m_gpuSolverIterations; ++iter) {
                    SDL_DispatchGPUCompute(colPass, unitGroupCount, 1, 1);
                }
                SDL_EndGPUComputePass(colPass);
            }
        }

        // Pass 8: Zero-Copy Ping-Pong Buffer Swap
        std::swap(m_gpuPositions, m_gpuSortedPositions);
        std::swap(m_gpuVelocities, m_gpuSortedVelocities);
        std::swap(m_gpuUnitStates, m_gpuSortedUnitStates);
    }

    const Uint64 stepEnd = SDL_GetTicksNS();
    const float gpuMs = static_cast<float>(stepEnd - stepStart) / 1.0e6f;
    m_lastGpuComputeMs.store(gpuMs, std::memory_order_relaxed);
    m_lastLogicTimeMs.store(gpuMs, std::memory_order_relaxed);
}

void LevelScene::render(gfx::SpriteBatch& batch) {
    // 1. Draw dedicated background texture (NEVER uses atlas for background)
    if (m_bgTexture && m_showBackgroundGrid) {
        const float uMax = m_level.size.x / static_cast<float>(m_bgTexture.width());
        const float vMax = m_level.size.y / static_cast<float>(m_bgTexture.height());
        gfx::drawCentered(batch, m_bgTexture.handle(), m_level.size * 0.5f, m_level.size, 0.0f,
                          {0.0f, 0.0f, uMax, vMax}, {1.0f, 1.0f, 1.0f, 1.0f});
    }

    // 2. When using CPU simulation, draw units via CPU SpriteBatch snapshots
    if (!m_useGpuSimulation) {
        const Uint64 renderStart = SDL_GetTicksNS();

        const RenderSnapshot* snapshot = unit_manager.AcquireRenderSnapshot();
        const size_t snapCount = snapshot ? snapshot->unitCount : unit_manager.GetCurrentUnits();
        const glm::vec2* positions = snapshot ? snapshot->positions.data() : unit_manager.GetPositionsPtr();
        const int* snapHealths = snapshot ? snapshot->health.data() : unit_manager.GetHealthsPtr();

        if (snapCount > 0 && positions != nullptr && snapHealths != nullptr) {
            const glm::vec2 unitSize = {enemy_size, enemy_size};
            const glm::vec4 unitUv = gfx::atlasCell(1, 0, 4, 4);
            batch.drawUnits(positions, snapHealths, snapCount, unitSize, unitUv, m_services->atlas->handle());
        }

        const Uint64 renderEnd = SDL_GetTicksNS();
        const float renderMs = static_cast<float>(renderEnd - renderStart) / 1.0e6f;
        m_lastRenderPrepMs.store(renderMs, std::memory_order_relaxed);
    }
}

void LevelScene::renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const glm::mat4& viewProjection) {
    if (!m_useGpuSimulation || m_unitPipeline == nullptr || m_gpuPositions == nullptr) {
        return;
    }

    const size_t unitCount = unit_manager.GetCurrentUnits();
    if (unitCount == 0) {
        return;
    }

    const Uint64 renderPassStart = SDL_GetTicksNS();

    SDL_BindGPUGraphicsPipeline(pass, m_unitPipeline);

    SDL_GPUBuffer* storageBuffers[3] = {m_gpuPositions, m_gpuVelocities, m_gpuUnitStates};
    SDL_BindGPUVertexStorageBuffers(pass, 0, storageBuffers, 3);

    UnitRenderUniforms uniforms{};
    uniforms.viewProjection = viewProjection;
    uniforms.unitSize = glm::vec2(enemy_size, enemy_size);
    uniforms.padding0 = glm::vec2(0.0f, 0.0f);
    uniforms.unitUv = gfx::atlasCell(1, 0, 4, 4);
    SDL_PushGPUVertexUniformData(commands, 0, &uniforms, sizeof(uniforms));

    SDL_GPUTextureSamplerBinding binding{};
    binding.texture = m_services->atlas->handle();
    binding.sampler = m_unitSampler;
    SDL_BindGPUFragmentSamplers(pass, 0, &binding, 1);

    SDL_DrawGPUPrimitives(pass, static_cast<Uint32>(unitCount * 6), 1, 0, 0);

    const Uint64 renderPassEnd = SDL_GetTicksNS();
    const float renderMs = static_cast<float>(renderPassEnd - renderPassStart) / 1.0e6f;
    m_lastRenderPrepMs.store(renderMs, std::memory_order_relaxed);
}

void LevelScene::debugUi() {
    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Level");

    if (ImGui::Button("Back to menu", ImVec2(160.0f, 0.0f))) {
        m_simRunning = false;
        m_services->scenes->pop();
    }

    ImGui::Checkbox("Show Hex Grid", &m_showBackgroundGrid);

    ImGui::End();

    // Logic Frametime Profiler & Sleeping Benchmark Window
    ImGui::SetNextWindowPos(ImVec2(40.0f, 120.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(440.0f, 650.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Real-Time Performance Profiler");

    // Helper: Pinned left label, right-anchored number (expands cleanly to the left)
    auto drawMetricRow = [](const char* label, const char* value, const ImVec4& color = ImVec4(1.0f, 1.0f, 1.0f, 1.0f)) {
        ImGui::TextUnformatted(label);
        ImGui::SameLine();
        const float textWidth = ImGui::CalcTextSize(value).x;
        const float rightEdge = ImGui::GetWindowContentRegionMax().x;
        ImGui::SetCursorPosX(std::max(ImGui::GetCursorPosX(), rightEdge - textWidth));
        ImGui::TextColored(color, "%s", value);
    };

    char bufFps[32], bufFrame[32], bufStats[64], bufCompute[32], bufRender[32];
    snprintf(bufFps, sizeof(bufFps), "%6.1f FPS", m_displayRealFps);
    snprintf(bufFrame, sizeof(bufFrame), "%6.2f ms", m_displayFrameMs);
    snprintf(bufStats, sizeof(bufStats), "%4.1f / %4.1f / %4.1f ms", m_displayMinMs, m_displayMaxMs, m_displayAvgMs);
    snprintf(bufCompute, sizeof(bufCompute), "%6.2f ms", m_displayGpuComputeMs);
    snprintf(bufRender, sizeof(bufRender), "%6.2f ms", m_displayRenderPrepMs);

    const ImVec4 fpsColor = (m_displayRealFps >= 55.0f) ? ImVec4(0.2f, 1.0f, 0.4f, 1.0f) :
                            (m_displayRealFps >= 30.0f) ? ImVec4(1.0f, 0.85f, 0.2f, 1.0f) :
                            ImVec4(1.0f, 0.3f, 0.2f, 1.0f);

    drawMetricRow("Application Real FPS", bufFps, fpsColor);
    drawMetricRow("Frame Duration", bufFrame, ImVec4(0.9f, 0.9f, 0.9f, 1.0f));
    drawMetricRow("Min / Max / Avg Frame", bufStats, ImVec4(0.7f, 0.7f, 0.7f, 1.0f));
    drawMetricRow("GPU Compute Dispatch", bufCompute, ImVec4(0.3f, 0.8f, 1.0f, 1.0f));
    drawMetricRow("Render Prep Time", bufRender, ImVec4(0.3f, 1.0f, 0.8f, 1.0f));

    ImGui::Separator();
    ImGui::TextUnformatted("Frame Time History (Last 120 Frames, 0-60ms):");
    ImGui::PlotLines("##frametime_plot", m_frameTimeHistory.data(), static_cast<int>(m_frameTimeHistory.size()),
                     m_frameHistoryOffset, nullptr, 0.0f, 60.0f, ImVec2(ImGui::GetContentRegionAvail().x, 60.0f));

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Simulation Backend:");
    if (ImGui::RadioButton("GPU Compute Shaders (Zero-Copy VRAM)", m_useGpuSimulation)) {
        m_useGpuSimulation = true;
    }
    if (ImGui::RadioButton("CPU AVX2 Worker Thread", !m_useGpuSimulation)) {
        m_useGpuSimulation = false;
    }

    if (m_useGpuSimulation) {
        ImGui::SliderInt("GPU Physics Substeps", &m_gpuSubsteps, 1, 10, "%d substeps");
        ImGui::TextUnformatted("Substep Presets:");
        ImGui::SameLine();
        if (ImGui::Button("1x (Default)")) m_gpuSubsteps = 1;
        ImGui::SameLine();
        if (ImGui::Button("2x")) m_gpuSubsteps = 2;
        ImGui::SameLine();
        if (ImGui::Button("4x")) m_gpuSubsteps = 4;
        ImGui::SameLine();
        if (ImGui::Button("8x")) m_gpuSubsteps = 8;

        ImGui::SliderInt("GPU Solver Iterations", &m_gpuSolverIterations, 1, 100);
        ImGui::TextUnformatted("Solver Presets:");
        ImGui::SameLine();
        if (ImGui::Button("4x")) m_gpuSolverIterations = 4;
        ImGui::SameLine();
        if (ImGui::Button("12x")) m_gpuSolverIterations = 12;
        ImGui::SameLine();
        if (ImGui::Button("25x")) m_gpuSolverIterations = 25;
        ImGui::SameLine();
        if (ImGui::Button("50x")) m_gpuSolverIterations = 50;
        ImGui::SameLine();
        if (ImGui::Button("100x")) m_gpuSolverIterations = 100;



        ImGui::Separator();
        ImGui::TextColored(ImVec4(0.2f, 0.8f, 1.0f, 1.0f), "GPU Sleeping & Physics Parameters:");
        ImGui::Checkbox("Enable GPU Sleep Optimization", &m_enableGpuSleeping);
        if (m_enableGpuSleeping) {
            ImGui::SliderInt("Deep Sleep Min Contacts", &m_deepSleepMinContacts, 1, 6, "%d contacts");
            ImGui::SliderFloat("Sleep Rel-Speed Threshold", &m_sleepMaxRelSpeed, 0.1f, 5.0f, "%.2f");
        }
        ImGui::SliderFloat("Over-Relaxation (Stiffness)", &m_overRelaxation, 1.0f, 2.0f, "%.2fx (Anti-Overlap)");
        ImGui::SliderFloat("Contact Friction", &m_contactFriction, 0.0f, 1.0f, "%.2f (Stack Stability)");
        ImGui::SliderFloat("Collision Normal Damping", &m_collisionDamping, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Gravity Acceleration", &m_gravity, 0.0f, 50.0f, "%.1f px/s^2");
        ImGui::SliderFloat("Floor Friction", &m_floorFriction, 0.0f, 1.0f, "%.2f");
        ImGui::SliderFloat("Wall Restitution", &m_restitution, 0.0f, 1.0f, "%.2f");

        ImGui::TextUnformatted("Sleep Presets:");
        ImGui::SameLine();
        if (ImGui::Button("High Perf")) {
            m_enableGpuSleeping = true;
            m_deepSleepMinContacts = 4;
            m_sleepMaxRelSpeed = 2.0f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Default")) {
            m_enableGpuSleeping = true;
            m_deepSleepMinContacts = 5;
            m_sleepMaxRelSpeed = 1.5f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Sensitive")) {
            m_enableGpuSleeping = true;
            m_deepSleepMinContacts = 6;
            m_sleepMaxRelSpeed = 0.8f;
        }
        ImGui::SameLine();
        if (ImGui::Button("Sleep Off")) {
            m_enableGpuSleeping = false;
        }
    } else {
        ImGui::SliderFloat("Target Sim Hz", &m_targetSimHz, 30.0f, 240.0f, "%.0f Hz");
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "World Size & Boundary Controls:");
    bool worldChanged = false;
    float worldSize[2] = {m_level.size.x, m_level.size.y};
    if (ImGui::DragFloat2("World Dimensions", worldSize, 50.0f, 500.0f, 25000.0f, "%.0f px")) {
        m_level.size.x = std::max(500.0f, worldSize[0]);
        m_level.size.y = std::max(500.0f, worldSize[1]);
        worldChanged = true;
    }
    if (ImGui::InputFloat2("Exact Dimensions", worldSize, "%.0f")) {
        m_level.size.x = std::max(500.0f, worldSize[0]);
        m_level.size.y = std::max(500.0f, worldSize[1]);
        worldChanged = true;
    }

    ImGui::TextUnformatted("World Presets:");
    ImGui::SameLine();
    if (ImGui::Button("2k x 2k")) {
        m_level.size = {2000.0f, 2000.0f};
        worldChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("4k x 4k")) {
        m_level.size = {4000.0f, 4000.0f};
        worldChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("6k x 6k")) {
        m_level.size = {6000.0f, 6000.0f};
        worldChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("10k x 10k")) {
        m_level.size = {10000.0f, 10000.0f};
        worldChanged = true;
    }
    ImGui::SameLine();
    if (ImGui::Button("Focus Horde")) {
        m_camera.setCenter(glm::vec2(m_level.size.x * 0.5f, enemy_size + 250.0f));
    }

    if (worldChanged) {
        unit_manager.Init(MaxUnitsCapacity, m_level.size, enemy_size);
        rebuildGridBuffers();
        generateBackgroundTexture();
        spawnAllUnits();
        m_needsGpuUpload.store(true, std::memory_order_release);
        m_camera.setCenter(glm::vec2(m_level.size.x * 0.5f, enemy_size + 250.0f));
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.2f, 1.0f), "Unit Spawner Configuration:");
    if (ImGui::SliderInt("Spawn Unit Count", &m_spawnUnitCount, 500, static_cast<int>(MaxUnitsCapacity), "%d units")) {
        m_needsRespawn.store(true, std::memory_order_release);
    }
    if (ImGui::InputInt("Exact Unit Count", &m_spawnUnitCount, 5000, 50000)) {
        m_spawnUnitCount = std::clamp(m_spawnUnitCount, 100, static_cast<int>(MaxUnitsCapacity));
        m_needsRespawn.store(true, std::memory_order_release);
    }

    ImGui::TextUnformatted("Unit Presets:");
    ImGui::SameLine();
    if (ImGui::Button("30k")) {
        m_spawnUnitCount = 30000;
        m_needsRespawn.store(true, std::memory_order_release);
    }
    ImGui::SameLine();
    if (ImGui::Button("100k")) {
        m_spawnUnitCount = 100000;
        m_needsRespawn.store(true, std::memory_order_release);
    }
    ImGui::SameLine();
    if (ImGui::Button("500k")) {
        m_spawnUnitCount = 500000;
        m_needsRespawn.store(true, std::memory_order_release);
    }
    ImGui::SameLine();
    if (ImGui::Button("1M")) {
        m_spawnUnitCount = 1000000;
        m_needsRespawn.store(true, std::memory_order_release);
    }
    ImGui::SameLine();
    if (ImGui::Button("2M")) {
        m_spawnUnitCount = 2000000;
        m_needsRespawn.store(true, std::memory_order_release);
    }
    ImGui::SameLine();
    if (ImGui::Button("4M")) {
        m_spawnUnitCount = 4000000;
        m_needsRespawn.store(true, std::memory_order_release);
    }

    ImGui::Separator();
    const size_t total = unit_manager.GetCurrentUnits() > 0 ? unit_manager.GetCurrentUnits() : 1;
    ImGui::TextColored(ImVec4(0.4f, 1.0f, 0.4f, 1.0f), "Sleeping Distribution (%zu Units):", total);
    const float interiorPct = (static_cast<float>(m_displayInteriorUnits) / total) * 100.0f;
    const float edgePct = (static_cast<float>(m_displayEdgeUnits) / total) * 100.0f;
    const float activePct = (static_cast<float>(m_displayActiveUnits) / total) * 100.0f;

    char bufInterior[48], bufEdge[48], bufActive[48];
    snprintf(bufInterior, sizeof(bufInterior), "%5zu (%5.1f%%)", m_displayInteriorUnits, interiorPct);
    snprintf(bufEdge, sizeof(bufEdge), "%5zu (%5.1f%%)", m_displayEdgeUnits, edgePct);
    snprintf(bufActive, sizeof(bufActive), "%5zu (%5.1f%%)", m_displayActiveUnits, activePct);

    drawMetricRow("Royal Blue (Interior):", bufInterior, ImVec4(0.3f, 0.6f, 1.0f, 1.0f));
    drawMetricRow("Sky Blue (Edge):", bufEdge, ImVec4(0.4f, 0.85f, 1.0f, 1.0f));
    drawMetricRow("Active (Green/Red):", bufActive, ImVec4(0.3f, 1.0f, 0.4f, 1.0f));

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Sleeping Aggressiveness Presets:");
    if (ImGui::Button("Very Aggressive")) {
        unit_manager.groupVelVarianceSq = 36.0f;
        unit_manager.groupNeighborDiffSq = 36.0f;
        unit_manager.stabilityFramesToSleep = 1;
    }
    ImGui::SameLine();
    if (ImGui::Button("Aggressive")) {
        unit_manager.groupVelVarianceSq = 24.0f;
        unit_manager.groupNeighborDiffSq = 24.0f;
        unit_manager.stabilityFramesToSleep = 2;
    }
    ImGui::SameLine();
    if (ImGui::Button("Moderate")) {
        unit_manager.groupVelVarianceSq = 16.0f;
        unit_manager.groupNeighborDiffSq = 16.0f;
        unit_manager.stabilityFramesToSleep = 3;
    }
    if (ImGui::Button("Conservative")) {
        unit_manager.groupVelVarianceSq = 6.0f;
        unit_manager.groupNeighborDiffSq = 6.0f;
        unit_manager.stabilityFramesToSleep = 6;
    }
    ImGui::SameLine();
    if (ImGui::Button("100% Awake (No Sleep)")) {
        unit_manager.groupVelVarianceSq = 0.0f;
        unit_manager.groupNeighborDiffSq = 0.0f;
        unit_manager.stabilityFramesToSleep = 999;
    }

    ImGui::Separator();
    ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Live Tuning Sliders:");
    ImGui::SliderFloat("Packing Quality (Min %)", &unit_manager.minPackingQuality, 0.50f, 0.99f, "%.2f (Strictness)");
    ImGui::SliderFloat("Variance Thresh (Vel Sq)", &unit_manager.groupVelVarianceSq, 0.0f, 60.0f, "%.1f");
    ImGui::SliderFloat("Neighbor Merge Thresh", &unit_manager.groupNeighborDiffSq, 0.0f, 60.0f, "%.1f");
    ImGui::SliderInt("Steps to Sleep", &unit_manager.stabilityFramesToSleep, 1, 10);
    ImGui::SliderInt("Solver Iterations", &unit_manager.solverIterations, 1, 8);
    ImGui::SliderInt("Physics Sub-Steps", &unit_manager.physicsSubSteps, 1, 6);

    ImGui::Separator();
    ImGui::PushStyleColor(ImGuiCol_Button, ImVec4(0.18f, 0.52f, 0.92f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonHovered, ImVec4(0.28f, 0.65f, 1.0f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_ButtonActive, ImVec4(0.12f, 0.40f, 0.80f, 1.0f));
    if (ImGui::Button("Respawn Units  [R]", ImVec2(-1.0f, 32.0f))) {
        m_needsRespawn.store(true, std::memory_order_release);
    }
    ImGui::PopStyleColor(3);

    ImGui::End();
}

void LevelScene::onResize(float width, float height) {
    m_camera.setViewport(width, height);
}

} // namespace horde::scene