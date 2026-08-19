#include "scene/EditorScene.hpp"

#include <imgui.h>

#include <algorithm>

#include "editor/EditorUi.hpp"
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
    gfx::renderLevel(m_state.level, batch, m_services->atlas->handle());
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
