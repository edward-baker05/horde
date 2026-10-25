#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <string>

#include "gfx/Camera2D.hpp"
#include "logic/Level.hpp"
#include "scene/Scene.hpp"
#include "sim/PhysicsRegistry.hpp"
#include "sim/PhysicsSystem.hpp"

namespace horde::scene {

// How a level is set up: which level, which physics system, and how many units
// start in it. The benchmark builds one of these from the command line.
struct LevelOptions {
    // Empty means the built-in default level.
    std::filesystem::path levelPath;
    std::string physics = sim::kDefaultPhysics;
    std::uint32_t unitCount = 10000;
    sim::SpawnPattern spawn = sim::SpawnPattern::Column;
    std::uint32_t seed = 1;
    // Zoom the camera so the whole level is on screen.
    bool fitCamera = false;
};

// The game world. Currently a level full of simulated units and a button back
// to the menu — this is where the actual game goes.
class LevelScene : public Scene {
public:
    LevelScene() = default;
    ~LevelScene() override;

    explicit LevelScene(std::filesystem::path levelPath) {
        m_options.levelPath = std::move(levelPath);
    }

    explicit LevelScene(LevelOptions options) : m_options(std::move(options)) {}

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
    void fitCameraToLevel(float width, float height);

    Services* m_services = nullptr;
    gfx::Camera2D m_camera;
    gfx::CameraController m_cameraController;

    LevelOptions m_options;
    // What the debug panel's restart button will apply.
    LevelOptions m_pendingOptions;
    logic::Level m_level;

    float enemy_size = 5.0f;
    std::unique_ptr<sim::PhysicsSystem> m_physics;
};

} // namespace horde::scene
