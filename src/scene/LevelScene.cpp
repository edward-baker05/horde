#include "LevelScene.hpp"

#include <SDL3/SDL_log.h>

#include <imgui.h>

#include <optional>
#include <string>

#include "core/Paths.hpp"
#include "gfx/LevelRenderer.hpp"
#include "gfx/SpriteBatch.hpp"
#include "gfx/Texture.hpp"
#include "logic/LevelIO.hpp"
#include "scene/SceneStack.hpp"

namespace horde::scene {

bool LevelScene::onEnter(Services& services) {
    m_services = &services;

    int width = 0;
    int height = 0;
    SDL_GetWindowSizeInPixels(services.window, &width, &height);
    m_camera.setViewport(static_cast<float>(width), static_cast<float>(height));

    // A missing or malformed level must never stop the game booting: fall back
    // to a bare default rather than failing to enter the scene.
    const std::filesystem::path path = m_levelPath.empty() ? paths::asset("levels/default.level.json") : m_levelPath;

    // std::string error;
    // if (std::optional<logic::Level> loaded = logic::loadLevel(path, &error)) {
    //     m_level = std::move(*loaded);
    // } else {
    //     SDL_Log("LevelScene: falling back to an empty level (%s)", error.c_str());
    //     m_level = logic::makeDefaultLevel();
    // }
    m_level = logic::makeDefaultLevel();
    unit_manager = UnitManager(MaxUnits, m_level.size, enemy_size);

    for (int i = 0; i < MaxUnits; ++i) {
        unit_manager.SpawnUnit(
            // silly wrapping
            glm::vec2(i * 25 / int(m_level.size.y) + m_level.size.x * 0.5, i * 5 % int(m_level.size.y)),
            glm::vec2(0, 0),
            // glm::vec2(i * 5 / int(m_level.size.y), i * 5 / int(m_level.size.y)),
            // glm::vec2(2, std::min(i/int(m_level.size.y),10)),
            10);
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
    gfx::renderLevel(m_level, batch, m_services->atlas->handle());

    gfx::Sprite unit;
    unit.size = {enemy_size, enemy_size};
    unit.uv = gfx::atlasCell(1, 0, 4, 4);
    // TODO: color could be determined from hp
    unit.color = {0.0f, 1.0f, 0.2f, 1.0f};

    const size_t unitCount = unit_manager.GetCurrentUnits();
    const glm::vec2* positions = unit_manager.GetPositionsPtr();
    const int* healths = unit_manager.GetHealthsPtr();

    for (size_t i = 0; i < unitCount; ++i) {
        if (i == 0) {
            unit.color = {1.0f, 0.0f, 0.0f, 1.0f};
        } else {
            unit.color = {1.0f - healths[i] / 10, 1.0f * healths[i] / 10, 0.2f, 1.0f};
        }
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