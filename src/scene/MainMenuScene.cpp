#include "scene/MainMenuScene.hpp"

#include <imgui.h>

#include <filesystem>
#include <memory>
#include <string>
#include <vector>

#include "logic/LevelFiles.hpp"
#include "scene/EditorScene.hpp"
#include "scene/LevelScene.hpp"
#include "scene/Presets.hpp"
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

void MainMenuScene::playLevel(const std::filesystem::path& path) {
    // Empty path: the built-in level. Zoomed to fit, so the whole level is in view.
    LevelOptions options;
    options.levelPath = path;
    options.fitCamera = true;
    m_services->scenes->push(std::make_unique<LevelScene>(std::move(options)));
}

void MainMenuScene::debugUi() {
    ImGui::SetNextWindowPos(ImVec2(40.0f, 40.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("horde");

    if (ImGui::Button("Play", ImVec2(160.0f, 0.0f))) {
        // One level means there is nothing to choose between: go straight into
        // it. With none saved, play the built-in level.
        const std::vector<std::filesystem::path> levels = logic::listLevels();
        if (levels.size() > 1) {
            m_choosingLevel = true;
        } else {
            playLevel(levels.empty() ? std::filesystem::path{} : levels.front());
        }
    }

    if (ImGui::Button("Upgrades", ImVec2(160.0f, 0.0f))) {
        m_services->scenes->push(std::make_unique<TechTreeScene>());
    }

    if (ImGui::Button("Level Editor", ImVec2(160.0f, 0.0f))) {
        m_services->scenes->push(std::make_unique<EditorScene>());
    }

    // Shared test setups from assets/presets.
    const std::vector<std::filesystem::path> presets = listPresets();
    if (!presets.empty()) {
        ImGui::SeparatorText("Presets");
        for (const std::filesystem::path& path : presets) {
            const std::string name = presetName(path);
            if (ImGui::Button(name.c_str(), ImVec2(220.0f, 0.0f))) {
                Preset preset;
                std::string error;
                if (loadPreset(name, LevelOptions{}, preset, error)) {
                    preset.options.fitCamera = true;
                    m_services->scenes->push(std::make_unique<LevelScene>(std::move(preset.options)));
                    m_presetError.clear();
                } else {
                    m_presetError = error;
                }
            }
            if (ImGui::IsItemHovered()) {
                Preset preset;
                std::string error;
                if (loadPreset(name, LevelOptions{}, preset, error) && !preset.description.empty()) {
                    ImGui::SetTooltip("%s", preset.description.c_str());
                }
            }
        }
        if (!m_presetError.empty()) {
            ImGui::TextWrapped("%s", m_presetError.c_str());
        }
    }

    if (m_choosingLevel) {
        ImGui::SeparatorText("Choose a level");

        for (const std::filesystem::path& path : logic::listLevels()) {
            const std::string name = logic::levelDisplayName(path);
            if (ImGui::Button(name.c_str(), ImVec2(160.0f, 0.0f))) {
                m_choosingLevel = false;
                playLevel(path);
            }
        }

        if (ImGui::Button("Cancel", ImVec2(160.0f, 0.0f))) {
            m_choosingLevel = false;
        }
    }

    ImGui::End();
}

void MainMenuScene::onResize(float width, float height) {
    m_camera.setViewport(width, height);
}

} // namespace horde::scene
