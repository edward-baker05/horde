#pragma once

#include <glm/glm.hpp>

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "gfx/Camera2D.hpp"
#include "logic/Level.hpp"
#include "logic/WallGridShapes.hpp"
#include "scene/LevelMode.hpp"
#include "scene/DancingCow.hpp"
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
    // Player plays on the tablet; Dev draws straight to the window with the
    // debug panel (CLI levels, presets, benchmarks).
    LevelMode mode = LevelMode::Player;
    sim::CollisionSettings collision;
    // Physics steps per second, independent of the frame rate; drawing blends
    // between steps. 0 steps once per frame, with the frame's own dt.
    float physicsRate = 60.0f;
    // Unit speed; units head for the nearest point of the nearest exit.
    float maxSpeed = sim::MovementSettings{}.maxSpeed;
    // The preset these options came from, if any, for display.
    std::string presetName;
    // The debug explosion tool's settings. Restarting the level carries over
    // whatever they were last set to, rather than resetting them.
    bool explosionArmed = false;
    sim::Explosion explosion;
    float flingDrag = 3.0f;
};

// The views a level can be watched through. The player sees one fixed view
// fitted to the level; more views and gestures to switch between them (pan,
// zoom, multiple cameras) plug in here later.
struct FeedCamera {
    std::vector<gfx::Camera2D> views;
    std::size_t active = 0;
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
    void onCovered() override;
    void onRevealed() override;
    bool handleEvent(const SDL_Event& event) override;
    void update(float dt) override;
    void compute(SDL_GPUCommandBuffer* commands) override;
    void render(gfx::SpriteBatch& batch) override;
    void renderPass(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass) override;
    void debugUi() override;
    void onResize(float width, float height) override;
    ui::ScenePresentation presentation() const override {
        return levelPresentation(m_options.mode);
    }

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
    // Points m_camera at the active feed view.
    void applyFeedCamera();
    // The seam for camera input in Player mode; no gestures yet.
    bool handleCameraGesture(const SDL_Event& event);
    // Live health summary, sampled from the GPU about once a second.
    void healthUi();
    // The debug explosion tool's settings.
    void explosionUi();

    Services* m_services = nullptr;
    DancingCow m_cow;
    // A pause has been pushed and not yet popped.
    bool m_pausePending = false;
    gfx::Camera2D m_camera;
    gfx::CameraController m_cameraController;
    FeedCamera m_feed;

    LevelOptions m_options;
    // What the debug panel's restart button will apply.
    LevelOptions m_pendingOptions;
    logic::Level m_level;
    // The wall grid merged for drawing, once, since the level doesn't change.
    logic::GridPieces m_gridPieces;

    float enemy_size = 5.0f;
    bool m_showFieldRegions = true;

    // Debug explosion tool: while armed, a right-click sets one off at the
    // cursor with these settings (its centre is filled in on the click).
    bool m_explosionArmed = false;
    sim::Explosion m_explosion;
    float m_flingDrag = 3.0f;

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
