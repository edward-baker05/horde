#pragma once

#include <glm/glm.hpp>

#include <filesystem>

#include "gfx/Camera2D.hpp"
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
    void onExit() override;
    bool handleEvent(const SDL_Event& event) override;
    void update(float dt) override;
    void compute(SDL_GPUCommandBuffer* commands) override;
    void render(gfx::SpriteBatch& batch) override;
    void renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) override;
    void debugUi() override;
    void onResize(float width, float height) override;

    gfx::Camera2D& camera() override {
        return m_camera;
    }

    const char* name() const override {
        return "Level";
    }

private:
    Services* m_services = nullptr;
    gfx::Camera2D m_camera;
    gfx::CameraController m_cameraController;

    std::filesystem::path m_levelPath;
    logic::Level m_level;

    static constexpr size_t MaxUnits = 10000;
    float enemy_size = 5.0f;
    UnitManager unit_manager;

    // GPU Simulation Pipelines
    SDL_GPUComputePipeline* m_gridClearPipeline = nullptr;
    SDL_GPUComputePipeline* m_gridBuildPipeline = nullptr;
    SDL_GPUComputePipeline* m_bitonicSortPipeline = nullptr;
    SDL_GPUComputePipeline* m_gridFindRangesPipeline = nullptr;
    SDL_GPUComputePipeline* m_computePipeline = nullptr;

    // Direct GPU Instancing Graphics Pipeline
    SDL_GPUGraphicsPipeline* m_unitGraphicsPipeline = nullptr;
    SDL_GPUSampler* m_unitSampler = nullptr;

    // Double buffers for deterministic collision simulation
    SDL_GPUBuffer* m_unitBuffers[2] = {nullptr, nullptr};
    uint32_t m_currentBufferIndex = 0;

    // GPU Sort-Based Uniform Grid Buffers
    SDL_GPUBuffer* m_gridKeysBuffer = nullptr;
    SDL_GPUBuffer* m_cellRangesBuffer = nullptr;
    Uint32 m_allocatedUnits = 0;
    glm::uvec2 m_gridDim{0, 0};
    Uint32 m_totalCells = 0;

    float m_dt = 0.0f;
    Uint32 m_unitCount = 0;
};

} // namespace horde::scene
