#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Types.h>

#include <filesystem>
#include <string>
#include <vector>

#include "gfx/Camera2D.hpp"
#include "scene/LevelMode.hpp"
#include "scene/Scene.hpp"

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

namespace horde::scene {

// Entry screen: an RmlUi document (assets/ui/main_menu.rml) bound to a "menu"
// data model. The model holds the lists and the current page; the document is
// pure markup and the scene only reacts to its event callbacks.
class MainMenuScene : public Scene {
public:
    bool onEnter(Services& services) override;
    void onExit() override;
    void onRevealed() override;
    void update(float deltaTime) override;
    void onResize(float width, float height) override;
    ui::ScenePresentation presentation() const override {
        return {ui::DeviceKind::Phone, ui::Orientation::Portrait};
    }

    gfx::Camera2D& camera() override {
        return m_camera;
    }

    const char* name() const override {
        return "MainMenu";
    }

private:
    // Pushes a LevelScene for the level file at `path` (empty: the built-in level).
    void refreshLists();
    void playLevel(const std::filesystem::path& path, LevelMode mode = LevelMode::Player);
    void playPreset(const std::string& name);
    void setPage(const char* page);
    void refreshClock();

    using Args = Rml::VariantList;
    void onOpen(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onBack(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onPlay(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onDevLevel(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onDevPreset(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onUpgrades(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onEditor(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onQuit(Rml::DataModelHandle, Rml::Event&, const Args& args);

    Services* m_services = nullptr;
    gfx::Camera2D m_camera;

    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_document = nullptr;
    Rml::DataModelHandle m_model;

    // Model state. Level files and preset names behind the lists, index for index.
    std::string m_page = "root";
    std::string m_clock;
    float m_clockTimer = 0.0f;
    std::vector<std::filesystem::path> m_levelPaths;
    std::vector<std::string> m_presetNames;
    Rml::Vector<Rml::String> m_levels;
    Rml::Vector<Rml::String> m_presets;
};

} // namespace horde::scene
