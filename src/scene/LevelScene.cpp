#include "LevelScene.hpp"

#include <SDL3/SDL_log.h>

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <vector>

#include "core/Paths.hpp"
#include "gfx/GpuContext.hpp"
#include "gfx/LevelRenderer.hpp"
#include "gfx/ShaderLoader.hpp"
#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
#include "logic/LevelIO.hpp"
#include "scene/SceneStack.hpp"

namespace horde::scene {

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

// Matches HLSL cbuffer UnitUniforms in unit.vert.hlsl
struct alignas(16) UnitDrawUniforms {
    glm::mat4 viewProjection;
    glm::vec4 uvRect;   // xy = top-left, zw = bottom-right
    glm::vec4 color;    // tint color
    glm::vec2 unitSize; // width, height
    glm::vec2 padding;
};

LevelScene::~LevelScene() {
    onExit();
}

bool LevelScene::onEnter(Services& services) {
    m_services = &services;
    SDL_GPUDevice* device = services.gpu->device();

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(services.window, &width, &height);
    m_camera.setViewport(static_cast<float>(width), static_cast<float>(height));

    m_level = logic::makeDefaultLevel();
    unit_manager = UnitManager(MaxUnits, m_level.size, enemy_size);

    // Compute grid dimensions and power-of-two allocated unit count for bitonic sort
    m_allocatedUnits = nextPowerOfTwo(static_cast<Uint32>(MaxUnits));
    const Uint32 gridCols = static_cast<Uint32>(std::ceil(m_level.size.x / enemy_size));
    const Uint32 gridRows = static_cast<Uint32>(std::ceil(m_level.size.y / enemy_size));
    m_gridDim = glm::uvec2(gridCols, gridRows);
    m_totalCells = gridCols * gridRows;

    // 1. Load GPU Simulation Compute Pipelines
    // 1a. grid_clear.comp: 0 ro buf, 1 rw buf (CellRanges at u0, space1), 1 ubuf (ClearParams at b0, space2)
    m_gridClearPipeline = services.shaders->loadCompute(
        "grid_clear.comp",
        0, 0, 0, 0,
        1, 1,
        256, 1, 1
    );

    // 1b. grid_build.comp: 1 ro buf (InUnits at t0, space0), 1 rw buf (GridKeys at u0, space1), 1 ubuf (GridBuildParams at b0, space2)
    m_gridBuildPipeline = services.shaders->loadCompute(
        "grid_build.comp",
        0, 0, 1, 0,
        1, 1,
        256, 1, 1
    );

    // 1c. bitonic_sort_step.comp: 0 ro buf, 1 rw buf (GridKeys at u0, space1), 1 ubuf (SortParams at b0, space2)
    m_bitonicSortPipeline = services.shaders->loadCompute(
        "bitonic_sort_step.comp",
        0, 0, 0, 0,
        1, 1,
        256, 1, 1
    );

    // 1d. grid_find_ranges.comp: 1 ro buf (SortedKeys at t0, space0), 1 rw buf (CellRanges at u0, space1), 1 ubuf (RangeParams at b0, space2)
    m_gridFindRangesPipeline = services.shaders->loadCompute(
        "grid_find_ranges.comp",
        0, 0, 1, 0,
        1, 1,
        256, 1, 1
    );

    // 1e. update_positions.comp: 3 ro buf (InUnits at t0, SortedKeys at t1, CellRanges at t2), 1 rw buf (OutUnits at u0), 1 ubuf (SimParams at b0)
    m_computePipeline = services.shaders->loadCompute(
        "update_positions.comp",
        0, 0, 3, 0,
        1, 1,
        256, 1, 1
    );

    if (!m_gridClearPipeline || !m_gridBuildPipeline || !m_bitonicSortPipeline ||
        !m_gridFindRangesPipeline || !m_computePipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to load GPU grid simulation compute pipelines");
        return false;
    }

    // 2. Load Unit Graphics Pipeline for Direct GPU Instancing (unit.vert + sprite.frag)
    // unit.vert: 1 storage buffer (t0, space0), 1 uniform buffer (b0, space1)
    SDL_GPUShader* vertexShader = services.shaders->loadGraphics(
        "unit.vert", SDL_GPU_SHADERSTAGE_VERTEX,
        gfx::ShaderResources{.samplers = 0, .storageTextures = 0, .storageBuffers = 1, .uniformBuffers = 1}
    );

    // sprite.frag: 1 sampler (s0, space2), 1 sampled texture (t0, space2)
    SDL_GPUShader* fragmentShader = services.shaders->loadGraphics(
        "sprite.frag", SDL_GPU_SHADERSTAGE_FRAGMENT,
        gfx::ShaderResources{.samplers = 1, .storageTextures = 0, .storageBuffers = 0, .uniformBuffers = 0}
    );

    if (!vertexShader || !fragmentShader) {
        if (vertexShader) SDL_ReleaseGPUShader(device, vertexShader);
        if (fragmentShader) SDL_ReleaseGPUShader(device, fragmentShader);
        return false;
    }

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
    pipelineInfo.vertex_shader = vertexShader;
    pipelineInfo.fragment_shader = fragmentShader;
    pipelineInfo.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
    pipelineInfo.target_info.num_color_targets = 1;
    pipelineInfo.target_info.color_target_descriptions = &colorTarget;

    m_unitGraphicsPipeline = SDL_CreateGPUGraphicsPipeline(device, &pipelineInfo);
    SDL_ReleaseGPUShader(device, vertexShader);
    SDL_ReleaseGPUShader(device, fragmentShader);

    if (!m_unitGraphicsPipeline) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create unit graphics pipeline: %s", SDL_GetError());
        return false;
    }

    // 3. Create Unit Texture Sampler
    SDL_GPUSamplerCreateInfo samplerInfo{};
    samplerInfo.min_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mag_filter = SDL_GPU_FILTER_NEAREST;
    samplerInfo.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
    samplerInfo.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
    samplerInfo.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;

    m_unitSampler = SDL_CreateGPUSampler(device, &samplerInfo);
    if (!m_unitSampler) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create unit sampler: %s", SDL_GetError());
        return false;
    }

    // 4. Create Double-Buffered GPU Unit Storage Buffers
    SDL_GPUBufferCreateInfo bufferInfo{};
    bufferInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
                       SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ  |
                       SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
    bufferInfo.size = static_cast<Uint32>(sizeof(BaseUnitData) * MaxUnits);

    m_unitBuffers[0] = SDL_CreateGPUBuffer(device, &bufferInfo);
    m_unitBuffers[1] = SDL_CreateGPUBuffer(device, &bufferInfo);

    if (!m_unitBuffers[0] || !m_unitBuffers[1]) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create double-buffered unit GPU buffers: %s", SDL_GetError());
        return false;
    }
    m_currentBufferIndex = 0;

    // 5. Create GPU Sort-Based Uniform Grid Buffers
    SDL_GPUBufferCreateInfo keysBufferInfo{};
    keysBufferInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    keysBufferInfo.size = static_cast<Uint32>(sizeof(glm::uvec2) * m_allocatedUnits);
    m_gridKeysBuffer = SDL_CreateGPUBuffer(device, &keysBufferInfo);

    SDL_GPUBufferCreateInfo rangesBufferInfo{};
    rangesBufferInfo.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE | SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
    rangesBufferInfo.size = static_cast<Uint32>(sizeof(glm::uvec2) * m_totalCells);
    m_cellRangesBuffer = SDL_CreateGPUBuffer(device, &rangesBufferInfo);

    if (!m_gridKeysBuffer || !m_cellRangesBuffer) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "Failed to create grid keys/ranges GPU buffers: %s", SDL_GetError());
        return false;
    }

    // Spawn units (seeded once on CPU)
    for (int i = 0; i < static_cast<int>(MaxUnits); ++i) {
        unit_manager.SpawnUnit(
            glm::vec2(i * 25 / int(m_level.size.y) + m_level.size.x * 0.5f, i * 5 % int(m_level.size.y)),
            glm::vec2(0.0f, 0.0f),
            10
        );
    }
    m_unitCount = static_cast<Uint32>(unit_manager.GetCurrentUnits());

    // 6. Upload initial unit data to GPU buffers via transfer buffer
    if (m_unitCount > 0) {
        const Uint32 uploadSize = static_cast<Uint32>(sizeof(BaseUnitData) * m_unitCount);
        SDL_GPUTransferBufferCreateInfo transferInfo{};
        transferInfo.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
        transferInfo.size = uploadSize;

        SDL_GPUTransferBuffer* uploadBuffer = SDL_CreateGPUTransferBuffer(device, &transferInfo);
        if (uploadBuffer != nullptr) {
            void* mapped = SDL_MapGPUTransferBuffer(device, uploadBuffer, false);
            SDL_memcpy(mapped, unit_manager.GetBaseUnitsPtr(), uploadSize);
            SDL_UnmapGPUTransferBuffer(device, uploadBuffer);

            SDL_GPUCommandBuffer* uploadCmd = SDL_AcquireGPUCommandBuffer(device);
            SDL_GPUCopyPass* copyPass = SDL_BeginGPUCopyPass(uploadCmd);

            SDL_GPUTransferBufferLocation src{};
            src.transfer_buffer = uploadBuffer;
            src.offset = 0;

            SDL_GPUBufferRegion dst0{};
            dst0.buffer = m_unitBuffers[0];
            dst0.offset = 0;
            dst0.size = uploadSize;

            SDL_UploadToGPUBuffer(copyPass, &src, &dst0, false);

            SDL_GPUBufferRegion dst1{};
            dst1.buffer = m_unitBuffers[1];
            dst1.offset = 0;
            dst1.size = uploadSize;

            SDL_UploadToGPUBuffer(copyPass, &src, &dst1, false);

            SDL_EndGPUCopyPass(copyPass);
            SDL_SubmitGPUCommandBuffer(uploadCmd);

            SDL_ReleaseGPUTransferBuffer(device, uploadBuffer);
        }
    }

    return true;
}

void LevelScene::onExit() {
    if (m_services && m_services->gpu) {
        SDL_GPUDevice* device = m_services->gpu->device();
        if (m_unitSampler) {
            SDL_ReleaseGPUSampler(device, m_unitSampler);
            m_unitSampler = nullptr;
        }
        if (m_unitGraphicsPipeline) {
            SDL_ReleaseGPUGraphicsPipeline(device, m_unitGraphicsPipeline);
            m_unitGraphicsPipeline = nullptr;
        }
        if (m_gridClearPipeline) {
            SDL_ReleaseGPUComputePipeline(device, m_gridClearPipeline);
            m_gridClearPipeline = nullptr;
        }
        if (m_gridBuildPipeline) {
            SDL_ReleaseGPUComputePipeline(device, m_gridBuildPipeline);
            m_gridBuildPipeline = nullptr;
        }
        if (m_bitonicSortPipeline) {
            SDL_ReleaseGPUComputePipeline(device, m_bitonicSortPipeline);
            m_bitonicSortPipeline = nullptr;
        }
        if (m_gridFindRangesPipeline) {
            SDL_ReleaseGPUComputePipeline(device, m_gridFindRangesPipeline);
            m_gridFindRangesPipeline = nullptr;
        }
        if (m_computePipeline) {
            SDL_ReleaseGPUComputePipeline(device, m_computePipeline);
            m_computePipeline = nullptr;
        }
        if (m_gridKeysBuffer) {
            SDL_ReleaseGPUBuffer(device, m_gridKeysBuffer);
            m_gridKeysBuffer = nullptr;
        }
        if (m_cellRangesBuffer) {
            SDL_ReleaseGPUBuffer(device, m_cellRangesBuffer);
            m_cellRangesBuffer = nullptr;
        }
        for (int i = 0; i < 2; ++i) {
            if (m_unitBuffers[i]) {
                SDL_ReleaseGPUBuffer(device, m_unitBuffers[i]);
                m_unitBuffers[i] = nullptr;
            }
        }
    }
}

bool LevelScene::handleEvent(const SDL_Event& event) {
    return m_cameraController.handleEvent(event, m_camera);
}

void LevelScene::update(float dt) {
    m_dt = dt;
    unit_manager.UpdatePhysics(dt);
}

void LevelScene::compute(SDL_GPUCommandBuffer* commands) {
    if (!m_computePipeline || !m_unitBuffers[0] || !m_unitBuffers[1] || m_unitCount == 0 ||
        !m_gridKeysBuffer || !m_cellRangesBuffer) {
        return;
    }

    const uint32_t srcIndex = m_currentBufferIndex;
    const uint32_t dstIndex = 1 - m_currentBufferIndex;

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
        buildUniforms.invCellSize = glm::vec2{1.0f / enemy_size, 1.0f / enemy_size};
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
        simParams.worldBounds = m_level.size - glm::vec2(enemy_size, enemy_size);
        simParams.dt = m_dt;
        simParams.unitCount = m_unitCount;
        simParams.restitution = 0.8f;
        simParams.gravity = 98.0f;
        simParams.unitSize = enemy_size;
        simParams.cellSize = enemy_size;
        simParams.gridDim = m_gridDim;
        simParams.invCellSize = glm::vec2{1.0f / enemy_size, 1.0f / enemy_size};
        simParams.padding = glm::vec2{0.0f, 0.0f};
        SDL_PushGPUComputeUniformData(commands, 0, &simParams, sizeof(simParams));

        SDL_GPUStorageBufferReadWriteBinding rwBinding{};
        rwBinding.buffer = m_unitBuffers[dstIndex];
        rwBinding.cycle = false;

        SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(commands, nullptr, 0, &rwBinding, 1);
        if (pass != nullptr) {
            SDL_BindGPUComputePipeline(pass, m_computePipeline);

            SDL_GPUBuffer* roBuffers[3] = {
                m_unitBuffers[srcIndex],
                m_gridKeysBuffer,
                m_cellRangesBuffer
            };
            SDL_BindGPUComputeStorageBuffers(pass, 0, roBuffers, 3);

            const Uint32 groupCountX = (m_unitCount + 255) / 256;
            SDL_DispatchGPUCompute(pass, groupCountX, 1, 1);
            SDL_EndGPUComputePass(pass);
        }
    }

    m_currentBufferIndex = dstIndex;
}

void LevelScene::render(gfx::SpriteBatch& batch) {
    // Render world geometry through SpriteBatch
    gfx::renderLevel(m_level, batch, m_services->atlas->handle());
}

void LevelScene::renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) {
    if (!m_unitGraphicsPipeline || !m_unitBuffers[m_currentBufferIndex] || m_unitCount == 0) {
        return;
    }

    // Direct GPU Instancing:
    // 1. Bind pipeline
    SDL_BindGPUGraphicsPipeline(pass, m_unitGraphicsPipeline);

    // 2. Bind vertex storage buffer (Units at t0, space0) from latest simulation buffer
    SDL_BindGPUVertexStorageBuffers(pass, 0, &m_unitBuffers[m_currentBufferIndex], 1);

    // 3. Push vertex uniform data (slot 0, b0, space1)
    const glm::vec4 uv = gfx::atlasCell(1, 0, 4, 4);
    UnitDrawUniforms uniforms{};
    uniforms.viewProjection = m_camera.viewProjection();
    uniforms.uvRect = uv;
    uniforms.color = glm::vec4(0.0f, 1.0f, 0.2f, 1.0f);
    uniforms.unitSize = glm::vec2(enemy_size, enemy_size);
    uniforms.padding = glm::vec2(0.0f, 0.0f);

    SDL_PushGPUVertexUniformData(commands, 0, &uniforms, sizeof(uniforms));

    // 4. Bind atlas texture and sampler to fragment stage (t0, s0, space2)
    SDL_GPUTextureSamplerBinding atlasBinding{};
    atlasBinding.texture = m_services->atlas->handle();
    atlasBinding.sampler = m_unitSampler;
    SDL_BindGPUFragmentSamplers(pass, 0, &atlasBinding, 1);

    // 5. Draw 6 vertices per unit quad
    SDL_DrawGPUPrimitives(pass, m_unitCount * 6, 1, 0, 0);
}

void LevelScene::debugUi() {
    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Level");

    ImGui::Text("GPU Units: %u", m_unitCount);
    ImGui::Text("Sim: GPU Sort-Based Uniform Grid Collision Solver");
    ImGui::Text("Grid Dim: %u x %u (%u cells)", m_gridDim.x, m_gridDim.y, m_totalCells);
    ImGui::Text("Allocated Keys: %u (padded for bitonic sort)", m_allocatedUnits);

    if (ImGui::Button("Back to menu", ImVec2(160.0f, 0.0f))) {
        m_services->scenes->pop();
    }

    ImGui::End();
}

void LevelScene::onResize(float width, float height) {
    m_camera.setViewport(width, height);
}

} // namespace horde::scene
