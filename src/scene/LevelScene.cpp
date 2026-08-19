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

    for (size_t i = 0; i < MaxUnits; ++i) {
        unit_manager.SpawnUnit(
            // silly wrapping
            glm::vec2(enemy_size * (i * enemy_size) / int(level_size.y), (i * enemy_size) % int(level_size.y)),
            glm::vec2(80, (i * enemy_size) % int(level_size.y)), 10);
    }

    return true;
}

bool LevelScene::handleEvent(const SDL_Event& event) {
    return m_cameraController.handleEvent(event, m_camera);
}

void LevelScene::update(float dt) {
    unit_manager.UpdatePhysics(dt);
}

void LevelScene::render(gfx::SpriteBatch& batch) {
    gfx::Sprite rectangle;
    rectangle.position = {0.0f, 0.0f, 0.0f};
    rectangle.size = level_size;
    rectangle.uv = gfx::atlasCell(1, 1, 2, 2);
    rectangle.color = {0.35f, 0.65f, 0.9f, 1.0f};

    batch.draw(rectangle, m_services->atlas->handle());

    gfx::Sprite unit;
    unit.size = {enemy_size, enemy_size};
    unit.uv = gfx::atlasCell(1, 0, 2, 2);
    // TODO: color could be determined from hp
    unit.color = {0.0f, 1.0f, 0.2f, 1.0f};

    const size_t unitCount = unit_manager.GetCurrentUnits();
    const glm::vec2* positions = unit_manager.GetPositionsPtr();

    for (size_t i = 0; i < unitCount; ++i) {
        unit.position = {positions[i], 0.0f};
        batch.draw(unit, m_services->atlas->handle());
    }
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