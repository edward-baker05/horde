#include "scene/EditorScene.hpp"

#include <imgui.h>

#include <algorithm>

#include "editor/EditorUi.hpp"
#include "editor/HitTest.hpp"
#include "gfx/LevelRenderer.hpp"
#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
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

bool EditorScene::handleEvent(const SDL_Event& event) {
    // ImGui gets first refusal on the mouse, so dragging a panel never also
    // drags the world behind it.
    const ImGuiIO& io = ImGui::GetIO();

    if (event.type == SDL_EVENT_MOUSE_MOTION) {
        m_cursorWorld = mouseWorld(event.motion.x, event.motion.y);
    }

    if (!io.WantCaptureMouse && m_state.tool == editor::Tool::Select) {
        switch (event.type) {
            case SDL_EVENT_MOUSE_MOTION: {
                if (m_draggingBody && m_state.selection.isWall()) {
                    m_state.level.walls[m_state.selection.index].center = m_cursorWorld - m_dragGrabOffset;
                    return true;
                }
                m_state.hovered = editor::pick(m_state.level, m_cursorWorld);
                return false;
            }

            case SDL_EVENT_MOUSE_BUTTON_DOWN: {
                if (event.button.button != SDL_BUTTON_LEFT) {
                    break;
                }
                m_state.selection = editor::pick(m_state.level, m_cursorWorld);
                if (m_state.selection.isWall()) {
                    // Snapshot once, here, at the start of the drag — not on
                    // every motion event.
                    m_state.beginMutation();
                    m_draggingBody = true;
                    m_dragGrabOffset = m_cursorWorld - m_state.level.walls[m_state.selection.index].center;
                }
                return true;
            }

            case SDL_EVENT_MOUSE_BUTTON_UP: {
                if (event.button.button == SDL_BUTTON_LEFT) {
                    m_draggingBody = false;
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

void EditorScene::render(gfx::SpriteBatch& batch) {
    SDL_GPUTexture* atlas = m_services->atlas->handle();
    gfx::renderLevel(m_state.level, batch, atlas);

    // Redraw the hovered and selected walls tinted, on top of the level. There
    // is no depth test, so drawing after is what puts them on top.
    const auto highlight = [&](const editor::Selection& selection, glm::vec4 tint) {
        if (selection.isWall() && selection.index < m_state.level.walls.size()) {
            gfx::renderWall(m_state.level.walls[selection.index], batch, atlas, tint);
        }
    };

    if (!(m_state.hovered == m_state.selection)) {
        highlight(m_state.hovered, {1.0f, 1.0f, 1.0f, 0.25f});
    }
    highlight(m_state.selection, {1.0f, 0.85f, 0.2f, 0.45f});
}

void EditorScene::debugUi() {
    bool wantsExit = false;
    editor::drawEditorUi(m_state, wantsExit);

    if (wantsExit) {
        m_services->scenes->pop();
    }
}

void EditorScene::onResize(float width, float height) {
    m_camera.setViewport(width, height);
}

} // namespace horde::scene
