#pragma once

#include <cstdint>
#include <vector>

#include "editor/EditorState.hpp"
#include "editor/Handles.hpp"
#include "editor/Markers.hpp"
#include "editor/Paint.hpp"
#include "editor/Tools.hpp"
#include "gfx/Camera2D.hpp"
#include "logic/WallGridShapes.hpp"
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

    // 0 = not dragging an end, 1 = the low end, 2 = the high end, 3 = the body.
    int m_markerDrag = 0;
    float m_markerGrabOffset = 0.0f;

    // Handles the paint tool's mouse events. Returns whether it used the event.
    bool handlePaintEvent(const SDL_Event& event);

    // Draws the cell lines (when zoomed in far enough to see them) and what the
    // brush would cover at the cursor.
    void renderPaintOverlay(gfx::SpriteBatch& batch);

    // A paint stroke in progress: which button started it, what it writes, and
    // where it last stamped. The last stamp outlives the stroke, so a
    // shift-click can draw a straight line from it.
    bool m_painting = false;
    std::uint8_t m_paintButton = 0;
    logic::Cell m_paintValue;
    editor::CellBox m_strokeBox;
    bool m_hasLastStamp = false;
    glm::vec2 m_lastStamp{0.0f, 0.0f};

    // The wall grid merged for drawing, and the cells it was merged from.
    // Rebuilt only when the cells differ, which comparing 160k bytes a frame
    // finds far more cheaply than merging them would.
    std::vector<logic::Cell> m_gridPiecesCells;
    glm::ivec2 m_gridPiecesDim{-1, -1};
    logic::GridPieces m_gridPieces;
};

} // namespace horde::scene
