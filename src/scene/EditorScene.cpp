#include "scene/EditorScene.hpp"

#include <imgui.h>

#include <algorithm>
#include <cmath>
#include <optional>
#include <variant>

#include "editor/EditorUi.hpp"
#include "editor/HitTest.hpp"
#include "editor/Snap.hpp"
#include "gfx/LevelRenderer.hpp"
#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
#include "logic/LevelGeometry.hpp"
#include "logic/LevelValidation.hpp"
#include "scene/LevelScene.hpp"
#include "scene/SceneStack.hpp"

namespace horde::scene {

bool EditorScene::onEnter(Services& services) {
    m_services = &services;

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(services.window, &width, &height);
    m_camera.setViewport(static_cast<float>(width), static_cast<float>(height));

    // Frame the whole level with a margin, so a new editor opens on something
    // sensible rather than on the origin.
    m_camera.setCenter(m_state.level.size * 0.5f);
    const float fit = std::min(static_cast<float>(width) / (m_state.level.size.x * 1.2f),
                               static_cast<float>(height) / (m_state.level.size.y * 1.2f));
    m_camera.setZoom(fit);

    return true;
}

glm::vec2 EditorScene::mouseWorld(float screenX, float screenY) const {
    return m_camera.screenToWorld({screenX, screenY});
}

// Handles are drawn and picked at a constant SCREEN size, so convert once per
// use: at zoom z, one screen pixel is 1/z world units.
float EditorScene::handleWorldSize() const {
    return 10.0f / m_camera.zoom();
}

float EditorScene::handlePickRadius() const {
    return handleWorldSize();
}

void EditorScene::commitDraft() {
    logic::Wall wall;
    if (editor::finishPolyline(m_draft, m_state.newWallColor, m_state.newPolylineThickness, wall)) {
        m_state.beginMutation();
        m_state.level.walls.push_back(std::move(wall));
    }
    m_draft.clear();
    // Tool stays armed, ready for the next polyline.
}

bool EditorScene::handleEvent(const SDL_Event& event) {
    // ImGui gets first refusal on the mouse, so dragging a panel never also
    // drags the world behind it.
    const ImGuiIO& io = ImGui::GetIO();

    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        m_cursorWorld = mouseWorld(event.motion.x, event.motion.y);
    }

    if (event.type == SDL_EVENT_KEY_DOWN && !io.WantCaptureKeyboard && m_state.tool == editor::Tool::Paint) {
        // Shift and a number picks a palette colour; without shift, numbers
        // still switch tools.
        if ((event.key.mod & SDL_KMOD_SHIFT) != 0 && event.key.key >= SDLK_1 && event.key.key <= SDLK_8) {
            m_state.paint.colorIndex = static_cast<std::uint8_t>(event.key.key - SDLK_1);
            return true;
        }
        if (event.key.key == SDLK_LEFTBRACKET || event.key.key == SDLK_RIGHTBRACKET) {
            const int step = event.key.key == SDLK_LEFTBRACKET ? -1 : 1;
            m_state.paint.size = std::clamp(m_state.paint.size + step, 1, editor::kMaxBrushSize);
            return true;
        }
    }

    if (event.type == SDL_EVENT_KEY_DOWN && !io.WantCaptureKeyboard) {
        switch (event.key.key) {
            case SDLK_1:
                m_state.tool = editor::Tool::Select;
                return true;
            case SDLK_2:
                m_state.tool = editor::Tool::Rectangle;
                return true;
            case SDLK_3:
                m_state.tool = editor::Tool::Triangle;
                return true;
            case SDLK_4:
                m_state.tool = editor::Tool::Circle;
                return true;
            case SDLK_5:
                m_state.tool = editor::Tool::Polyline;
                return true;
            case SDLK_6:
                m_state.tool = editor::Tool::Spawn;
                return true;
            case SDLK_7:
                m_state.tool = editor::Tool::Exit;
                return true;
            case SDLK_8:
                m_state.tool = editor::Tool::FieldRegion;
                return true;
            case SDLK_9:
                m_state.tool = editor::Tool::Paint;
                return true;

            case SDLK_DELETE:
            case SDLK_BACKSPACE:
                editor::deleteSelected(m_state);
                return true;

            case SDLK_RETURN:
            case SDLK_KP_ENTER:
                if (m_draft.active) {
                    commitDraft();
                    return true;
                }
                break;

            case SDLK_ESCAPE:
                // Escape abandons an in-progress polyline entirely, rather than
                // committing a partial one.
                m_draft.clear();
                m_placement.active = false;
                m_painting = false;
                m_state.tool = editor::Tool::Select;
                return true;

            case SDLK_Z:
                if ((event.key.mod & SDL_KMOD_CTRL) != 0) {
                    m_state.undo.undo(m_state.level);
                    m_state.selection.clear();
                    m_state.dirty = true;
                    return true;
                }
                break;

            case SDLK_Y:
                if ((event.key.mod & SDL_KMOD_CTRL) != 0) {
                    m_state.undo.redo(m_state.level);
                    m_state.selection.clear();
                    m_state.dirty = true;
                    return true;
                }
                break;

            default:
                break;
        }
    }

    // A stroke already under way keeps going even over a panel, so releasing
    // the button there still ends it.
    if (m_state.tool == editor::Tool::Paint && (m_painting || !io.WantCaptureMouse)) {
        if (handlePaintEvent(event)) {
            return true;
        }
    }

    if (!io.WantCaptureMouse && m_state.tool == editor::Tool::Polyline) {
        switch (event.type) {
            case SDL_EVENT_MOUSE_MOTION:
                m_draft.cursor = editor::snapPosition(m_cursorWorld, m_state.snap);
                return m_draft.active;

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button != SDL_BUTTON_LEFT) {
                    break;
                }
                // A double click finishes rather than adding a duplicate point.
                if (event.button.clicks >= 2) {
                    commitDraft();
                    return true;
                }
                m_draft.active = true;
                m_draft.points.push_back(editor::snapPosition(m_cursorWorld, m_state.snap));
                return true;
            }

            default:
                break;
        }
    }

    if (!io.WantCaptureMouse && (m_state.tool == editor::Tool::Spawn || m_state.tool == editor::Tool::Exit)) {
        const logic::MarkerKind kind =
            m_state.tool == editor::Tool::Spawn ? logic::MarkerKind::Spawn : logic::MarkerKind::Exit;

        switch (event.type) {
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    m_placement.active = true;
                    m_placement.start = m_cursorWorld;
                    m_placement.current = m_cursorWorld;
                    return true;
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                m_placement.current = m_cursorWorld;
                return m_placement.active;

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (event.button.button != SDL_BUTTON_LEFT || !m_placement.active) {
                    break;
                }
                m_placement.active = false;

                logic::Marker marker;
                if (editor::makeMarkerFromDrag(m_state.level, kind, m_placement.start, m_cursorWorld, marker)) {
                    m_state.beginMutation();
                    m_state.level.markers.push_back(marker);
                }
                return true;
            }

            default:
                break;
        }
    }

    if (!io.WantCaptureMouse && editor::isBoxTool(m_state.tool)) {
        switch (event.type) {
            case SDL_EVENT_MOUSE_BUTTON_DOWN:
                if (event.button.button == SDL_BUTTON_LEFT) {
                    m_placement.active = true;
                    m_placement.start = editor::snapPosition(m_cursorWorld, m_state.snap);
                    m_placement.current = m_placement.start;
                    return true;
                }
                break;

            case SDL_EVENT_MOUSE_MOTION:
                // Snap here too, so the preview shows exactly what release
                // would produce.
                m_placement.current = editor::snapPosition(m_cursorWorld, m_state.snap);
                return m_placement.active;

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (event.button.button != SDL_BUTTON_LEFT || !m_placement.active) {
                    break;
                }
                m_placement.active = false;
                const glm::vec2 end = editor::snapPosition(m_cursorWorld, m_state.snap);

                if (m_state.tool == editor::Tool::FieldRegion) {
                    logic::FieldRegion region;
                    if (editor::makeFieldRegionFromDrag(m_placement.start, end, m_state.newFieldStrength, region)) {
                        m_state.beginMutation();
                        m_state.level.fieldRegions.push_back(region);
                    }
                    return true;
                }

                logic::Wall wall;
                if (editor::makeWallFromDrag(m_state.tool, m_placement.start, end, m_state.newWallColor, wall)) {
                    m_state.beginMutation();
                    m_state.level.walls.push_back(std::move(wall));
                }
                // The tool stays armed: walls come in runs.
                return true;
            }

            default:
                break;
        }
    }

    if (!io.WantCaptureMouse && m_state.tool == editor::Tool::Select) {
        switch (event.type) {
            case SDL_EVENT_MOUSE_MOTION: {
                if (m_activeHandle.kind != editor::HandleKind::None && m_state.selection.isWall()) {
                    logic::Wall& wall = m_state.level.walls[m_state.selection.index];
                    const glm::vec2 target = m_activeHandle.kind == editor::HandleKind::ResizeCorner
                                                 ? editor::snapPosition(m_cursorWorld, m_state.snap)
                                                 : m_cursorWorld;
                    editor::applyHandleDrag(wall, m_activeHandle, target, m_rotationGrabOffset);
                    if (m_activeHandle.kind == editor::HandleKind::Rotate) {
                        wall.rotation = editor::snapRotation(wall.rotation, m_state.snap);
                    }
                    return true;
                }
                if (m_activeHandle.kind != editor::HandleKind::None && m_state.selection.isFieldRegion()) {
                    // Drag a rectangle with the region's transform, then copy
                    // the result back: the same grips and maths as a wall.
                    logic::FieldRegion& region = m_state.level.fieldRegions[m_state.selection.index];
                    logic::Wall frame = logic::fieldRegionFrame(region);
                    const glm::vec2 target = m_activeHandle.kind == editor::HandleKind::ResizeCorner
                                                 ? editor::snapPosition(m_cursorWorld, m_state.snap)
                                                 : m_cursorWorld;
                    editor::applyHandleDrag(frame, m_activeHandle, target, m_rotationGrabOffset);
                    region.rotation = m_activeHandle.kind == editor::HandleKind::Rotate
                                          ? editor::snapRotation(frame.rotation, m_state.snap)
                                          : frame.rotation;
                    region.halfExtents = std::get<logic::RectangleShape>(frame.shape).halfExtents;
                    return true;
                }
                if (m_markerDrag != 0 && m_state.selection.isMarker()) {
                    logic::Marker& marker = m_state.level.markers[m_state.selection.index];
                    const float along = editor::projectOntoEdge(m_state.level, marker.edge, m_cursorWorld);

                    if (m_markerDrag == 1) {
                        const float end = marker.offset + marker.length;
                        marker.offset = std::min(along, end - editor::kMinimumMarkerLength);
                        marker.length = end - marker.offset;
                    } else if (m_markerDrag == 2) {
                        marker.length = std::max(along - marker.offset, editor::kMinimumMarkerLength);
                    } else {
                        marker.offset = along - m_markerGrabOffset;
                    }

                    // Clamping here is what makes overlap impossible, rather
                    // than something validation reports after the fact.
                    editor::clampMarker(m_state.level, m_state.selection.index);
                    return true;
                }

                if (m_draggingBody && m_state.selection.isWall()) {
                    m_state.level.walls[m_state.selection.index].center =
                        editor::snapPosition(m_cursorWorld - m_dragGrabOffset, m_state.snap);
                    return true;
                }
                if (m_draggingBody && m_state.selection.isFieldRegion()) {
                    m_state.level.fieldRegions[m_state.selection.index].center =
                        editor::snapPosition(m_cursorWorld - m_dragGrabOffset, m_state.snap);
                    return true;
                }
                m_state.hovered = editor::pick(m_state.level, m_cursorWorld);
                return false;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button != SDL_BUTTON_LEFT) {
                    break;
                }

                // Handles beat the body: a corner handle sits ON the shape's
                // outline, so testing the body first would make it unreachable.
                if (m_state.selection.isWall()) {
                    const logic::Wall& wall = m_state.level.walls[m_state.selection.index];
                    const editor::Handle handle = editor::pickHandle(wall, m_cursorWorld, handlePickRadius());
                    if (handle.kind != editor::HandleKind::None) {
                        m_state.beginMutation();
                        m_activeHandle = handle;
                        if (handle.kind == editor::HandleKind::Rotate) {
                            const glm::vec2 d = m_cursorWorld - wall.center;
                            m_rotationGrabOffset = std::atan2(d.y, d.x) - wall.rotation;
                        }
                        return true;
                    }
                }

                if (m_state.selection.isFieldRegion()) {
                    const logic::Wall frame =
                        logic::fieldRegionFrame(m_state.level.fieldRegions[m_state.selection.index]);
                    const editor::Handle handle = editor::pickHandle(frame, m_cursorWorld, handlePickRadius());
                    if (handle.kind != editor::HandleKind::None) {
                        m_state.beginMutation();
                        m_activeHandle = handle;
                        if (handle.kind == editor::HandleKind::Rotate) {
                            const glm::vec2 d = m_cursorWorld - frame.center;
                            m_rotationGrabOffset = std::atan2(d.y, d.x) - frame.rotation;
                        }
                        return true;
                    }
                }

                // A selected marker's end grips are reachable before the
                // selection changes, the same way a wall's handles are.
                if (m_state.selection.isMarker()) {
                    const logic::Marker& marker = m_state.level.markers[m_state.selection.index];
                    glm::vec2 low{};
                    glm::vec2 high{};
                    editor::markerEndHandles(m_state.level, marker, low, high);

                    const float reach = handlePickRadius();
                    const auto isNear = [&](glm::vec2 p) {
                        const glm::vec2 d = m_cursorWorld - p;
                        return std::sqrt(d.x * d.x + d.y * d.y) <= reach;
                    };

                    if (isNear(low) || isNear(high)) {
                        m_state.beginMutation();
                        m_markerDrag = isNear(low) ? 1 : 2;
                        return true;
                    }
                }

                m_state.selection = editor::pick(m_state.level, m_cursorWorld);
                if (m_state.selection.isWall()) {
                    // Snapshot once, here, at the start of the drag — not on
                    // every motion event.
                    m_state.beginMutation();
                    m_draggingBody = true;
                    m_dragGrabOffset = m_cursorWorld - m_state.level.walls[m_state.selection.index].center;
                }
                if (m_state.selection.isFieldRegion()) {
                    m_state.beginMutation();
                    m_draggingBody = true;
                    m_dragGrabOffset = m_cursorWorld - m_state.level.fieldRegions[m_state.selection.index].center;
                }
                if (m_state.selection.isMarker()) {
                    const logic::Marker& marker = m_state.level.markers[m_state.selection.index];
                    m_state.beginMutation();
                    m_markerDrag = 3;
                    m_markerGrabOffset =
                        editor::projectOntoEdge(m_state.level, marker.edge, m_cursorWorld) - marker.offset;
                    return true;
                }
                return true;
            }

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    m_draggingBody = false;
                    m_activeHandle = editor::Handle{};
                    m_markerDrag = 0;
                }
                break;
            }

            default:
                break;
        }
    }

    // CameraController pans on left OR middle drag, but in the editor the left
    // button belongs to the tools. Forward only wheel and middle-button events.
    const bool cameraEvent = event.type == SDL_EVENT_MOUSE_WHEEL ||
                             ((event.type == SDL_EVENT_MOUSE_BUTTON_DOWN || event.type == SDL_EVENT_MOUSE_BUTTON_UP) &&
                              event.button.button == SDL_BUTTON_MIDDLE) ||
                             (event.type == SDL_EVENT_MOUSE_MOTION && (event.motion.state & SDL_BUTTON_MMASK) != 0);

    if (cameraEvent && !io.WantCaptureMouse) {
        return m_cameraController.handleEvent(event, m_camera);
    }

    return false;
}

bool EditorScene::handlePaintEvent(const SDL_Event& event) {
    logic::WallGrid& grid = m_state.level.wallGrid;
    const editor::PaintSettings& paint = m_state.paint;

    switch (event.type) {
        case SDL_EVENT_MOUSE_BUTTON_DOWN: {
            const std::uint8_t button = event.button.button;
            if (m_painting || (button != SDL_BUTTON_LEFT && button != SDL_BUTTON_RIGHT)) {
                break;
            }
            const SDL_Keymod mod = SDL_GetModState();

            // Alt-click is an eyedropper: it picks up the colour under the
            // cursor and paints nothing.
            if (button == SDL_BUTTON_LEFT && (mod & SDL_KMOD_ALT) != 0) {
                const logic::Cell cell = grid.at(editor::cellAt(m_cursorWorld));
                if (!cell.empty()) {
                    m_state.paint.colorIndex = cell.colorIndex();
                }
                return true;
            }

            // Slope mode's left click turns the one cell under the cursor to
            // the next slope, rather than stamping the brush.
            if (button == SDL_BUTTON_LEFT && paint.mode == editor::PaintMode::Slope) {
                const glm::ivec2 cell = editor::cellAt(m_cursorWorld);
                if (grid.contains(cell)) {
                    m_state.beginMutation();
                    grid.set(cell, logic::Cell::make(editor::nextSlope(grid.at(cell).shape()), paint.colorIndex));
                }
                return true;
            }

            const bool erase = (paint.mode == editor::PaintMode::Erase) == (button == SDL_BUTTON_LEFT);
            m_paintValue = erase ? logic::Cell{} : logic::Cell::make(logic::CellShape::Full, paint.colorIndex);
            m_paintButton = button;
            m_painting = true;
            m_strokeBox = editor::CellBox{};

            // Snapshot once, at the start of the stroke, so it undoes whole.
            m_state.beginMutation();
            if ((mod & SDL_KMOD_SHIFT) != 0 && m_hasLastStamp) {
                editor::stampLine(grid, m_lastStamp, m_cursorWorld, paint, m_paintValue, m_strokeBox);
            } else {
                editor::stampBrush(grid, m_cursorWorld, paint, m_paintValue, m_strokeBox);
            }
            m_lastStamp = m_cursorWorld;
            m_hasLastStamp = true;
            return true;
        }

        case SDL_EVENT_MOUSE_MOTION:
            if (!m_painting) {
                return false;
            }
            editor::stampLine(grid, m_lastStamp, m_cursorWorld, paint, m_paintValue, m_strokeBox);
            m_lastStamp = m_cursorWorld;
            return true;

        case SDL_EVENT_MOUSE_BUTTON_UP:
            if (!m_painting || event.button.button != m_paintButton) {
                break;
            }
            m_painting = false;
            // Smoothing fills notches, so it follows painting only: after an
            // erase it would put back some of what was just removed.
            if (!m_paintValue.empty() && paint.autoSmooth) {
                editor::autoSmooth(grid, m_strokeBox, paint.colorIndex);
            }
            return true;

        default:
            break;
    }
    return false;
}

void EditorScene::renderPaintOverlay(gfx::SpriteBatch& batch) {
    SDL_GPUTexture* atlas = m_services->atlas->handle();
    const logic::WallGrid& grid = m_state.level.wallGrid;
    const float zoom = m_camera.zoom();

    // Cell lines, only once they are far enough apart on screen to help.
    if (logic::kCellSize * zoom >= 6.0f) {
        const glm::vec2 a = m_camera.screenToWorld({0.0f, 0.0f});
        const glm::vec2 b = m_camera.screenToWorld(m_camera.viewport());
        const glm::vec2 levelMax = glm::vec2(grid.dim) * logic::kCellSize;
        const glm::vec2 lo = glm::max(glm::min(a, b), glm::vec2(0.0f));
        const glm::vec2 hi = glm::min(glm::max(a, b), levelMax);
        const glm::ivec2 first = glm::ivec2(glm::ceil(lo / logic::kCellSize));
        const glm::ivec2 last = glm::ivec2(glm::floor(hi / logic::kCellSize));
        const float width = 1.0f / zoom;
        const glm::vec4 lineColor{1.0f, 1.0f, 1.0f, 0.06f};

        for (int x = first.x; x <= last.x; ++x) {
            const float wx = static_cast<float>(x) * logic::kCellSize;
            gfx::drawCentered(batch, atlas, {wx, (lo.y + hi.y) * 0.5f}, {width, hi.y - lo.y}, 0.0f, gfx::cellSolid(),
                              lineColor);
        }
        for (int y = first.y; y <= last.y; ++y) {
            const float wy = static_cast<float>(y) * logic::kCellSize;
            gfx::drawCentered(batch, atlas, {(lo.x + hi.x) * 0.5f, wy}, {hi.x - lo.x, width}, 0.0f, gfx::cellSolid(),
                              lineColor);
        }
    }

    // What the brush covers here: the next slope for slope mode, otherwise
    // every cell under the brush, in the paint colour or red for erasing.
    glm::vec4 paintColor = gfx::toFloatColor(grid.palette[m_state.paint.colorIndex]);
    paintColor.a = 0.45f;

    if (m_state.paint.mode == editor::PaintMode::Slope && !m_painting) {
        const glm::ivec2 cell = editor::cellAt(m_cursorWorld);
        if (grid.contains(cell)) {
            const glm::vec2 centre = (glm::vec2(cell) + 0.5f) * logic::kCellSize;
            gfx::drawCentered(batch, atlas, centre, glm::vec2(logic::kCellSize), 0.0f, gfx::cellSquare(),
                              {1.0f, 1.0f, 1.0f, 0.5f});
            const logic::CellShape next = editor::nextSlope(grid.at(cell).shape());
            if (next != logic::CellShape::Empty) {
                gfx::renderSlope(cell, next, batch, atlas, paintColor);
            }
        }
        return;
    }

    const bool erase = m_painting ? m_paintValue.empty() : m_state.paint.mode == editor::PaintMode::Erase;
    const glm::vec4 color = erase ? glm::vec4{1.0f, 0.3f, 0.25f, 0.35f} : paintColor;
    for (const glm::ivec2& cell : editor::brushCells(grid, m_cursorWorld, m_state.paint)) {
        gfx::drawCentered(batch, atlas, (glm::vec2(cell) + 0.5f) * logic::kCellSize, glm::vec2(logic::kCellSize), 0.0f,
                          gfx::cellSolid(), color);
    }
}

void EditorScene::render(gfx::SpriteBatch& batch) {
    SDL_GPUTexture* atlas = m_services->atlas->handle();

    const logic::WallGrid& grid = m_state.level.wallGrid;
    if (grid.dim != m_gridPiecesDim || grid.cells != m_gridPiecesCells) {
        m_gridPieces = logic::mergeWallGrid(grid, true);
        m_gridPiecesCells = grid.cells;
        m_gridPiecesDim = grid.dim;
    }
    gfx::renderLevel(m_state.level, batch, atlas, true, &m_gridPieces);

    // Redraw the hovered and selected walls tinted, on top of the level. There
    // is no depth test, so drawing after is what puts them on top.
    const auto highlight = [&](const editor::Selection& selection, glm::vec4 tint) {
        if (selection.isWall() && selection.index < m_state.level.walls.size()) {
            gfx::renderWall(m_state.level.walls[selection.index], batch, atlas, tint);
        }
        if (selection.isFieldRegion() && selection.index < m_state.level.fieldRegions.size()) {
            gfx::renderFieldRegion(m_state.level.fieldRegions[selection.index], batch, atlas, tint);
        }
    };

    if (!(m_state.hovered == m_state.selection)) {
        highlight(m_state.hovered, {1.0f, 1.0f, 1.0f, 0.25f});
    }
    highlight(m_state.selection, {1.0f, 0.85f, 0.2f, 0.45f});

    // A translucent preview of what the current drag would produce, so the
    // result is visible before the button is released.
    if (m_placement.active && m_state.tool == editor::Tool::FieldRegion) {
        logic::FieldRegion preview;
        if (editor::makeFieldRegionFromDrag(m_placement.start, m_placement.current, m_state.newFieldStrength,
                                            preview)) {
            glm::vec4 tint = gfx::fieldRegionColor();
            tint.a = 0.35f;
            gfx::renderFieldRegion(preview, batch, atlas, tint);
        }
    } else if (m_placement.active) {
        logic::Wall preview;
        if (editor::makeWallFromDrag(m_state.tool, m_placement.start, m_placement.current, m_state.newWallColor,
                                     preview)) {
            glm::vec4 tint = gfx::toFloatColor(preview.color);
            tint.a = 0.45f;
            gfx::renderWall(preview, batch, atlas, tint);
        }
    }

    // The in-progress polyline, plus a rubber-band segment to the cursor so you
    // can see where the next click would land.
    if (m_draft.active && !m_draft.points.empty()) {
        editor::PolylineDraft preview = m_draft;
        preview.points.push_back(m_draft.cursor);

        logic::Wall wall;
        if (editor::finishPolyline(preview, m_state.newWallColor, m_state.newPolylineThickness, wall)) {
            glm::vec4 tint = gfx::toFloatColor(wall.color);
            tint.a = 0.55f;
            gfx::renderWall(wall, batch, atlas, tint);
        }
    }

    // Tint every wall validation rejects, so the reason saving is blocked is
    // visible in the world and not only in a panel.
    for (const logic::Problem& problem : logic::validate(m_state.level)) {
        if (problem.wallIndex < m_state.level.walls.size()) {
            gfx::renderWall(m_state.level.walls[problem.wallIndex], batch, atlas, {1.0f, 0.25f, 0.2f, 0.55f});
        }
    }

    if (m_state.tool == editor::Tool::Paint) {
        renderPaintOverlay(batch);
    }

    // The grips on the selected wall or field region, drawn last so nothing
    // covers them.
    std::optional<logic::Wall> gripped;
    if (m_state.selection.isWall() && m_state.selection.index < m_state.level.walls.size()) {
        gripped = m_state.level.walls[m_state.selection.index];
    } else if (m_state.selection.isFieldRegion() && m_state.selection.index < m_state.level.fieldRegions.size()) {
        gripped = logic::fieldRegionFrame(m_state.level.fieldRegions[m_state.selection.index]);
    }
    if (gripped) {
        const float size = handleWorldSize();
        for (const editor::Handle& handle : editor::wallHandles(*gripped)) {
            const glm::vec4 uv = handle.kind == editor::HandleKind::Rotate ? gfx::cellDisc() : gfx::cellSolid();
            const glm::vec4 color = handle.kind == editor::HandleKind::Rotate ? glm::vec4{0.4f, 0.8f, 1.0f, 1.0f}
                                                                              : glm::vec4{1.0f, 0.9f, 0.3f, 1.0f};
            gfx::drawCentered(batch, atlas, handle.world, {size, size}, 0.0f, uv, color);
        }
    }

    // A marker stretches along its edge only, so it gets two end grips and
    // nothing that would make it thicker.
    if (m_state.selection.isMarker() && m_state.selection.index < m_state.level.markers.size()) {
        const float size = handleWorldSize();
        glm::vec2 low{};
        glm::vec2 high{};
        editor::markerEndHandles(m_state.level, m_state.level.markers[m_state.selection.index], low, high);

        for (const glm::vec2& position : {low, high}) {
            gfx::drawCentered(batch, atlas, position, {size, size}, 0.0f, gfx::cellSolid(), {1.0f, 1.0f, 1.0f, 1.0f});
        }
    }
}

void EditorScene::debugUi() {
    bool wantsExit = false;
    bool wantsPlay = false;
    editor::drawEditorUi(m_state, wantsExit, wantsPlay);

    if (wantsExit) {
        m_services->scenes->pop();
    } else if (wantsPlay) {
        // Over the editor, so leaving the level comes straight back here.
        LevelOptions options;
        options.level = m_state.level;
        options.physics = m_state.testPhysics;
        options.unitCount = static_cast<std::uint32_t>(m_state.testUnits);
        options.spawn = sim::SpawnPattern::Markers;
        options.fitCamera = true;
        m_services->scenes->push(std::make_unique<LevelScene>(std::move(options)));
    }
}

void EditorScene::onResize(float width, float height) {
    m_camera.setViewport(width, height);
}

} // namespace horde::scene
