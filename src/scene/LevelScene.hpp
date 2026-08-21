#pragma once
#include <glm/glm.hpp>

#include <array>
#include <atomic>
#include <filesystem>
#include <thread>

#include "gfx/Camera2D.hpp"
#include "gfx/Texture.hpp"
#include "logic/Level.hpp"
#include "logic/UnitData.hpp"
#include "scene/Scene.hpp"

namespace horde::scene {

// The game world. Currently a single rectangle and a button back to the menu —
// this is where the actual game goes.
class LevelScene : public Scene {
public:
    LevelScene() = default;
    ~LevelScene() override;

    explicit LevelScene(std::filesystem::path levelPath) : m_levelPath(std::move(levelPath)) {}

    bool onEnter(Services& services) override;
    bool handleEvent(const SDL_Event& event) override;
    void update(float dt) override;
    void compute(SDL_GPUCommandBuffer* commands) override;
    void render(gfx::SpriteBatch& batch) override;
    void renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, const glm::mat4& viewProjection) override;
    void debugUi() override;
    void onResize(float width, float height) override;

    gfx::Camera2D& camera() override {
        return m_camera;
    }

    const char* name() const override {
        return "Level";
    }

private:
    void simLoop(std::stop_token stopToken);
    void spawnAllUnits();
    void generateBackgroundTexture();
    void rebuildGridBuffers();
    void releaseGpuResources();

    Services* m_services = nullptr;
    gfx::Camera2D m_camera;
    gfx::CameraController m_cameraController;

    std::filesystem::path m_levelPath;
    logic::Level m_level;

    static constexpr size_t MaxUnitsCapacity = 4194304;
    int m_spawnUnitCount = 30000;
    float enemy_size = 5.0f;
    UnitManager unit_manager;

    // GPU Compute & Render Pipeline State
    SDL_GPUGraphicsPipeline* m_unitPipeline = nullptr;
    SDL_GPUSampler* m_unitSampler = nullptr;
    SDL_GPUComputePipeline* m_clearGridPipeline = nullptr;
    SDL_GPUComputePipeline* m_updatePositionsPipeline = nullptr;
    SDL_GPUComputePipeline* m_prefixSumBlocksPipeline = nullptr;
    SDL_GPUComputePipeline* m_prefixSumTopPipeline = nullptr;
    SDL_GPUComputePipeline* m_prefixSumAddPipeline = nullptr;
    SDL_GPUComputePipeline* m_reorderUnitsPipeline = nullptr;
    SDL_GPUComputePipeline* m_resolveCollisionsPipeline = nullptr;

    // Double-buffered Unit Attribute Storage
    SDL_GPUBuffer* m_gpuPositions = nullptr;
    SDL_GPUBuffer* m_gpuSortedPositions = nullptr;
    SDL_GPUBuffer* m_gpuVelocities = nullptr;
    SDL_GPUBuffer* m_gpuSortedVelocities = nullptr;
    SDL_GPUBuffer* m_gpuUnitStates = nullptr;
    SDL_GPUBuffer* m_gpuSortedUnitStates = nullptr;

    // Unit Spatial Indexing
    SDL_GPUBuffer* m_gpuCellKeys = nullptr;
    SDL_GPUBuffer* m_gpuUnitLocalIndices = nullptr;

    // Spatial Grid Buckets & Prefix Sum Tables
    SDL_GPUBuffer* m_gpuCellCounts = nullptr;
    SDL_GPUBuffer* m_gpuCellStarts = nullptr;
    SDL_GPUBuffer* m_gpuCellEnds = nullptr;
    SDL_GPUBuffer* m_gpuBlockSums = nullptr;
    size_t m_gpuGridCapacityCells = 0;

    SDL_GPUTransferBuffer* m_gpuPosUpload = nullptr;
    SDL_GPUTransferBuffer* m_gpuVelUpload = nullptr;
    SDL_GPUTransferBuffer* m_gpuStatesUpload = nullptr;
    SDL_GPUTransferBuffer* m_gpuPosDownload = nullptr;

    bool m_useGpuSimulation = true;
    std::atomic<bool> m_needsGpuUpload{true};
    int m_gpuSolverIterations = 4;
    int m_gpuSubsteps = 1;
    std::vector<glm::vec2> m_gpuRenderPositions;

    // Tunable GPU Sleep & Physics Parameters
    bool m_enableGpuSleeping = true;
    int m_deepSleepMinContacts = 5;
    float m_sleepMaxRelSpeed = 1.5f;
    float m_gravity = 10.0f;
    float m_collisionDamping = 0.5f;
    float m_overRelaxation = 1.35f;
    float m_contactFriction = 0.25f;
    float m_floorFriction = 0.85f;
    float m_restitution = 0.1f;

    // Dedicated background texture (stored in VRAM for lifetime of level)
    gfx::Texture m_bgTexture;

    // Asynchronous Simulation Thread & Lock-Free Snapshot State
    std::jthread m_simThread;
    std::atomic<bool> m_simRunning{false};
    std::atomic<bool> m_needsRespawn{false};
    float m_targetSimHz = 60.0f;

    // Real Frame & Performance Profiler
    std::atomic<float> m_lastGpuComputeMs{0.0f};
    std::atomic<float> m_lastRenderPrepMs{0.0f};
    std::atomic<float> m_lastLogicTimeMs{0.0f};
    std::array<float, 240> m_frameTimeHistory{};
    size_t m_frameHistoryOffset = 0;
    float m_minFrameMs = 0.0f;
    float m_maxFrameMs = 0.0f;
    float m_avgFrameMs = 0.0f;

    // Display smoothing for stable, jitter-free UI text
    float m_displayRealFps = 0.0f;
    float m_displayFrameMs = 0.0f;
    float m_displayGpuComputeMs = 0.0f;
    float m_displayRenderPrepMs = 0.0f;
    float m_displayLogicMs = 0.0f;
    float m_displayMinMs = 0.0f;
    float m_displayMaxMs = 0.0f;
    float m_displayAvgMs = 0.0f;
    size_t m_displayInteriorUnits = 0;
    size_t m_displayEdgeUnits = 0;
    size_t m_displayActiveUnits = 0;
    float m_displayTimer = 0.0f;

    // Visual background grid toggle
    bool m_showBackgroundGrid = true;
};

} // namespace horde::scene

