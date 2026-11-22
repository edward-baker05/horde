#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Types.h>

#include <algorithm>
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
    // Esc closes the level picker or a page, back to the home screen.
    bool handleEvent(const SDL_Event& event) override;
    void update(float deltaTime) override;
    void onResize(float width, float height) override;
    ui::ScenePresentation presentation() const override {
        return {ui::DeviceKind::Phone, ui::Orientation::Portrait, false, m_phase != Phase::Idle && m_phase != Phase::SlidingIn};
    }

    float backdropBrightness() const override {
        const float t = std::min(m_phaseTimer / kFadeSeconds, 1.0f);
        switch (m_phase) {
            case Phase::SlidingOut:
                return 1.0f - t; // fading to black as the phone leaves
            case Phase::HoldBeforeSwap:
                return 0.0f;
            case Phase::FadeBackground:
                return t;
            default:
                return 1.0f;
        }
    }

    bool blocksInput() const override {
        return m_phase != Phase::Idle;
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
    void playMusic();
    // Loads the current mode's menu document, replacing the shown one.
    bool loadDocument();
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
    void onTogglePolish(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void startToggle();
    void updateToggle(float deltaTime);

    Services* m_services = nullptr;
    gfx::Camera2D m_camera;

    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_document = nullptr;
    Rml::DataModelHandle m_model;

    // Model state. Level files and preset names behind the lists, index for index.
    std::string m_page = "root";
    std::string m_clock;
    std::string m_date;
    // Under the home screen's title: how many levels there are to play.
    std::string m_levelCount;
    bool m_polish = false; // mirrors polish::enabled() for the settings switch
    bool m_togglePending = false;
    // Switching mode: the phone slides away while the background fades to black,
    // a beat, the mode and document swap, the new background fades up from black,
    // then the new phone slides in and the music fades in.
    enum class Phase { Idle, SlidingOut, HoldBeforeSwap, FadeBackground, SlidingIn };
    static constexpr float kFadeSeconds = 0.8f;
    Phase m_phase = Phase::Idle;
    float m_phaseTimer = 0.0f;
    float m_clockTimer = 0.0f;
    std::vector<std::filesystem::path> m_levelPaths;
    std::vector<std::string> m_presetNames;
    Rml::Vector<Rml::String> m_levels;
    Rml::Vector<Rml::String> m_presets;
};

} // namespace horde::scene
