#pragma once

#include "editor/EditorState.hpp"
#include "gfx/Camera2D.hpp"
#include "scene/Scene.hpp"

namespace horde::scene {

// The level editor.
//
// Dear ImGui drives its panels, which is consistent with the project's
// "ImGui is for tooling" rule: a level editor is tooling. Direct manipulation
// happens in world space through the shared Camera2D.
class EditorScene : public Scene {
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
        return "Editor";
    }

private:
    // World-space position of the mouse, updated every motion event.
    glm::vec2 mouseWorld(float screenX, float screenY) const;

    Services* m_services = nullptr;
    gfx::Camera2D m_camera;
    gfx::CameraController m_cameraController;

    editor::EditorState m_state;
    glm::vec2 m_cursorWorld{0.0f, 0.0f};

    // Set while the left button is held after grabbing a selected element.
    bool m_draggingBody = false;
    glm::vec2 m_dragGrabOffset{0.0f, 0.0f};
};

} // namespace horde::scene
