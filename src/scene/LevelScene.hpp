#pragma once

#include <glm/glm.hpp>

#include "gfx/Camera2D.hpp"
#include "logic/UnitData.hpp"
#include "scene/Scene.hpp"

namespace horde::scene {

// The game world. Currently a single rectangle and a button back to the menu —
// this is where the actual game goes.
class LevelScene : public Scene {
public:
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

    size_t MaxUnits = 1000000;
    glm::vec2 level_size = {600, 400};
    int enemy_size = 5;
    UnitManager unit_manager{MaxUnits, level_size, enemy_size};
};

} // namespace horde::scene
