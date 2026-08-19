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

    explicit LevelScene(std::filesystem::path levelPath) : m_levelPath(std::move(levelPath)) {}

    bool onEnter(Services& services) override;
    bool handleEvent(const SDL_Event& event) override;
    void update(float dt) override;
    void render(gfx::SpriteBatch& batch) override;
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

    static constexpr size_t MaxUnits = 1000000;
    int enemy_size = 5;
    UnitManager unit_manager{MaxUnits, glm::vec2{600.0f, 400.0f}, enemy_size};
};

} // namespace horde::scene
