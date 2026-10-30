#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>

#include "gfx/Camera2D.hpp"
#include "logic/Level.hpp"
#include "scene/Scene.hpp"
#include "sim/PhysicsRegistry.hpp"
#include "sim/PhysicsSystem.hpp"
#include "sim/WallColliders.hpp"

namespace horde::scene {

// How a level is set up: which level, which physics system, and how many units
// start in it. The benchmark builds one of these from the command line.
struct LevelOptions {
    // Empty means the built-in default level.
    std::filesystem::path levelPath;
    // Play this level instead of loading one: the editor's test play.
    std::optional<logic::Level> level;
    std::string physics = sim::kDefaultPhysics;
    std::uint32_t unitCount = 10000;
    // Markers: units stream in at the green spawns, spawnRate per second.
    sim::SpawnPattern spawn = sim::SpawnPattern::Markers;
    float spawnRate = 1000.0f;
    // A unit reaching a red exit re-enters at a spawn; otherwise it stops there.
    bool respawnAtExit = true;
    sim::HealthSettings health;
    std::uint32_t seed = 1;
    // Zoom the camera so the whole level is on screen.
    bool fitCamera = false;
    sim::CollisionSettings collision;
    // Physics steps per second, independent of the frame rate; drawing blends
    // between steps. 0 steps once per frame, with the frame's own dt.
    float physicsRate = 60.0f;
    // Unit speed; units head for the nearest point of the nearest exit.
    float maxSpeed = sim::MovementSettings{}.maxSpeed;
    // The preset these options came from, if any, for display.
    std::string presetName;
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

    sim::PhysicsSystem& physics() {
        return *m_physics;
    }

    const logic::Level& level() const {
        return m_level;
    }

    float unitSize() const {
        return enemy_size;
    }

    const sim::WallColliders& walls() const {
        return *m_walls;
    }

    gfx::Camera2D& camera() override {
        return m_camera;
    }

    const char* name() const override {
        return "Level";
    }

private:
    void fitCameraToLevel(float width, float height);
    // The frame cost breakdown at the top of the debug panel.
    void frameCostUi();
    // Live health summary, sampled from the GPU about once a second.
    void healthUi();

    Services* m_services = nullptr;
    gfx::Camera2D m_camera;
    gfx::CameraController m_cameraController;

    LevelOptions m_options;
    // What the debug panel's restart button will apply.
    LevelOptions m_pendingOptions;
    logic::Level m_level;

    float enemy_size = 5.0f;
    bool m_showFieldRegions = true;

    // Fixed-rate stepping: time not yet simulated, and the steps this frame owes compute().
    float m_stepAccumulator = 0.0f;
    std::uint32_t m_stepsThisFrame = 0;
    float m_stepsPerFrame = 0.0f; // smoothed, for the panel

    // Last health sample: when, and mean / lowest hit points and units at 0.
    Uint64 m_healthSampledAt = 0;
    bool m_healthSampled = false;
    float m_healthMean = 0.0f;
    float m_healthLowest = 0.0f;
    std::uint32_t m_healthAtZero = 0;
    // Most steps one frame may run before the simulation is allowed to fall
    // behind real time, so a slow frame can't snowball into slower ones.
    static constexpr std::uint32_t kMaxStepsPerFrame = 4;
    std::shared_ptr<sim::WallColliders> m_walls = std::make_shared<sim::WallColliders>();
    std::unique_ptr<sim::PhysicsSystem> m_physics;
};

} // namespace horde::scene
