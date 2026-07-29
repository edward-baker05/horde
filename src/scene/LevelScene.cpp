#include "scene/LevelScene.hpp"

#include <imgui.h>

#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
#include "scene/SceneStack.hpp"

namespace horde::scene {

bool LevelScene::onEnter(Services& services) {
    m_services = &services;

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(services.window, &width, &height);
    m_camera.setViewport(static_cast<float>(width), static_cast<float>(height));

    return true;
}

bool LevelScene::handleEvent(const SDL_Event& event) {
    return m_cameraController.handleEvent(event, m_camera);
}

void LevelScene::render(gfx::SpriteBatch& batch) {
    gfx::Sprite rectangle;
    rectangle.position = {-160.0f, -100.0f, 0.0f};
    rectangle.size = {320.0f, 200.0f};
    rectangle.uv = gfx::atlasCell(1, 1, 2, 2);
    rectangle.color = {0.35f, 0.65f, 0.9f, 1.0f};

    batch.draw(rectangle, m_services->atlas->handle());
}

void LevelScene::debugUi() {
    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Level");

    if (ImGui::Button("Back to menu", ImVec2(160.0f, 0.0f))) {
        m_services->scenes->pop();
    }

    ImGui::End();
}

void LevelScene::onResize(float width, float height) {
    m_camera.setViewport(width, height);
}

} // namespace horde::scene
