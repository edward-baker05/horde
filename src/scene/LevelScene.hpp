#pragma once

#include "gfx/Camera2D.hpp"
#include "scene/Scene.hpp"
#include <glm/glm.hpp>
#include "Logic/UnitData.hpp"

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

    size_t MaxUnits = 100;
    UnitManager unit_manager{MaxUnits};


    glm::vec2 level_size = {6000, 4000};
    int enemy_size = 5;
};

} // namespace horde::scene
