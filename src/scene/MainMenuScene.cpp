#include "scene/MainMenuScene.hpp"

#include <imgui.h>

#include <memory>

#include "scene/LevelScene.hpp"
#include "scene/SceneStack.hpp"
#include "scene/TechTreeScene.hpp"

namespace horde::scene {

bool MainMenuScene::onEnter(Services& services) {
    m_services = &services;

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(services.window, &width, &height);
    m_camera.setViewport(static_cast<float>(width), static_cast<float>(height));

    return true;
}

void MainMenuScene::debugUi() {
    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("horde");

    if (ImGui::Button("Play", ImVec2(160.0f, 0.0f))) {
        m_services->scenes->push(std::make_unique<LevelScene>());
    }

    if (ImGui::Button("Upgrades", ImVec2(160.0f, 0.0f))) {
        m_services->scenes->push(std::make_unique<TechTreeScene>());
    }

    ImGui::End();
}

void MainMenuScene::onResize(float width, float height) {
    m_camera.setViewport(width, height);
}

} // namespace horde::scene
