#pragma once

#include "editor/EditorState.hpp"
#include "editor/Handles.hpp"
#include "editor/Tools.hpp"
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

    // Turns the in-progress polyline draft into a wall, if it has enough
    // points, and clears it either way.
    void commitDraft();

    // Handles are drawn and picked at a constant SCREEN size, so their world
    // size falls out of the current zoom.
    float handleWorldSize() const;
    float handlePickRadius() const;

    Services* m_services = nullptr;
    gfx::Camera2D m_camera;
    gfx::CameraController m_cameraController;

    editor::EditorState m_state;
    glm::vec2 m_cursorWorld{0.0f, 0.0f};

    // The box-sweep drag in progress, for the rectangle, triangle and circle
    // tools.
    editor::Placement m_placement;

    // The polyline being clicked out, point by point.
    editor::PolylineDraft m_draft;

    // Set while the left button is held after grabbing a selected element.
    bool m_draggingBody = false;
    glm::vec2 m_dragGrabOffset{0.0f, 0.0f};

    // The handle being dragged, if any, and the angle between the wall's
    // rotation and the cursor when it was grabbed.
    editor::Handle m_activeHandle;
    float m_rotationGrabOffset = 0.0f;
};

} // namespace horde::scene
