#pragma once

#include <glm/vec2.hpp>

#include "gfx/Camera2D.hpp"
#include "scene/Scene.hpp"

namespace horde::scene {

// The upgrade tree. Currently two connected nodes and a button back to the
// menu.
//
// Nodes are sprites in world space drawn through the same SpriteBatch and
// Camera2D as the level, rather than through a UI toolkit, so they pan and zoom
// with the camera.
class TechTreeScene : public Scene {
public:
    bool onEnter(Services& services) override;
    bool handleEvent(const SDL_Event& event) override;
    void render(gfx::SpriteBatch& batch) override;
    void debugUi() override;
    void onResize(float width, float height) override;

    gfx::Camera2D& camera() override {
        return m_camera;
    }

    const char* name() const override {
        return "TechTree";
    }

private:
    static constexpr float kNodeSize = 64.0f;
    static constexpr float kEdgeThickness = 6.0f;

    Services* m_services = nullptr;
    gfx::Camera2D m_camera;
    gfx::CameraController m_cameraController;

    glm::vec2 m_nodeA{-120.0f, -60.0f};
    glm::vec2 m_nodeB{120.0f, 60.0f};
};

} // namespace horde::scene
