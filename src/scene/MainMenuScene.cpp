#include "scene/MainMenuScene.hpp"

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <memory>

#include "core/Paths.hpp"
#include "logic/LevelFiles.hpp"
#include "scene/EditorScene.hpp"
#include "scene/LevelScene.hpp"
#include "scene/Presets.hpp"
#include "scene/SceneStack.hpp"
#include "scene/TechTreeScene.hpp"
#include "ui/RmlLayer.hpp"

namespace horde::scene {

bool MainMenuScene::onEnter(Services& services) {
    m_services = &services;

    if (services.ui == nullptr) {
        return false;
    }
    m_context = services.ui->context(ui::DeviceKind::Phone);
    if (m_context == nullptr) {
        return false;
    }

    m_levelPaths = logic::listLevels();
    for (const std::filesystem::path& path : m_levelPaths) {
        m_levels.push_back(logic::levelDisplayName(path));
    }
    for (const std::filesystem::path& path : listPresets()) {
        m_presetNames.push_back(presetName(path));
        m_presets.push_back(m_presetNames.back());
    }
    refreshClock();

    Rml::DataModelConstructor constructor = m_context->CreateDataModel("menu");
    if (!constructor) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "main menu: could not create the data model");
        return false;
    }
    constructor.RegisterArray<Rml::Vector<Rml::String>>();
    constructor.Bind("levels", &m_levels);
    constructor.Bind("presets", &m_presets);
    constructor.Bind("page", &m_page);
    constructor.Bind("clock", &m_clock);
    constructor.BindEventCallback("open", &MainMenuScene::onOpen, this);
    constructor.BindEventCallback("back", &MainMenuScene::onBack, this);
    constructor.BindEventCallback("play", &MainMenuScene::onPlay, this);
    constructor.BindEventCallback("dev_level", &MainMenuScene::onDevLevel, this);
    constructor.BindEventCallback("dev_preset", &MainMenuScene::onDevPreset, this);
    constructor.BindEventCallback("upgrades", &MainMenuScene::onUpgrades, this);
    constructor.BindEventCallback("editor", &MainMenuScene::onEditor, this);
    constructor.BindEventCallback("quit", &MainMenuScene::onQuit, this);
    m_model = constructor.GetModelHandle();

    const std::string documentPath = paths::asset("ui/main_menu.rml").string();
    m_document = m_context->LoadDocument(documentPath);
    if (m_document == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "main menu: could not load %s", documentPath.c_str());
        return false;
    }
    m_document->Show();
    SDL_Log("Main menu document loaded: %s", documentPath.c_str());
    return true;
}

void MainMenuScene::onExit() {
    if (m_document != nullptr) {
        m_document->Close();
        m_document = nullptr;
    }
    if (m_context != nullptr) {
        m_context->RemoveDataModel("menu");
        m_context = nullptr;
    }
}

void MainMenuScene::onRevealed() {
    // Back on the top-level list rather than wherever the player left from.
    setPage("root");
    if (m_document != nullptr) {
        m_document->Show();
    }
}

void MainMenuScene::update(float deltaTime) {
    m_clockTimer += deltaTime;
    if (m_clockTimer >= 1.0f) {
        m_clockTimer = 0.0f;
        refreshClock();
    }
}

void MainMenuScene::refreshClock() {
    SDL_Time now = 0;
    SDL_DateTime time{};
    std::string text;
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &time, true)) {
        text = std::to_string(time.hour / 10) + std::to_string(time.hour % 10) + ":" +
               std::to_string(time.minute / 10) + std::to_string(time.minute % 10);
    }
    if (text != m_clock) {
        m_clock = std::move(text);
        if (m_model) {
            m_model.DirtyVariable("clock");
        }
    }
}

void MainMenuScene::setPage(const char* page) {
    if (m_page != page) {
        m_page = page;
        if (m_model) {
            m_model.DirtyVariable("page");
        }
    }
}

void MainMenuScene::playLevel(const std::filesystem::path& path, LevelMode mode) {
    // Empty path: the built-in level. Zoomed to fit, so the whole level is in view.
    LevelOptions options;
    options.levelPath = path;
    options.fitCamera = true;
    options.mode = mode;
    m_services->scenes->push(std::make_unique<LevelScene>(std::move(options)));
}

void MainMenuScene::playPreset(const std::string& name) {
    Preset preset;
    std::string error;
    if (loadPreset(name, LevelOptions{}, preset, error)) {
        preset.options.fitCamera = true;
        preset.options.mode = LevelMode::Dev;
        m_services->scenes->push(std::make_unique<LevelScene>(std::move(preset.options)));
    } else {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "preset '%s': %s", name.c_str(), error.c_str());
    }
}

namespace {

// Event arguments arrive as Variants; a missing or wrong-typed one is -1.
int indexArg(const Rml::VariantList& args) {
    return args.empty() ? -1 : args[0].Get<int>(-1);
}

} // namespace

void MainMenuScene::onOpen(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    const Rml::String target = args.empty() ? Rml::String() : args[0].Get<Rml::String>();
    if (target == "play") {
        // One level means there is nothing to choose between: go straight into
        // it. With none saved, play the built-in level.
        if (m_levelPaths.size() > 1) {
            setPage("play");
        } else {
            playLevel(m_levelPaths.empty() ? std::filesystem::path{} : m_levelPaths.front());
        }
    } else if (target == "dev") {
        setPage("dev");
    }
}

void MainMenuScene::onBack(Rml::DataModelHandle, Rml::Event&, const Args&) {
    setPage("root");
}

void MainMenuScene::onPlay(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    const int index = indexArg(args);
    if (index >= 0 && index < static_cast<int>(m_levelPaths.size())) {
        setPage("root");
        playLevel(m_levelPaths[index]);
    }
}

void MainMenuScene::onDevLevel(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    // -1 is the built-in level, listed first on the Dev Ground page.
    const int index = indexArg(args);
    if (index == -1) {
        playLevel({}, LevelMode::Dev);
    } else if (index >= 0 && index < static_cast<int>(m_levelPaths.size())) {
        playLevel(m_levelPaths[index], LevelMode::Dev);
    }
}

void MainMenuScene::onDevPreset(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    const int index = indexArg(args);
    if (index >= 0 && index < static_cast<int>(m_presetNames.size())) {
        playPreset(m_presetNames[index]);
    }
}

void MainMenuScene::onUpgrades(Rml::DataModelHandle, Rml::Event&, const Args&) {
    m_services->scenes->push(std::make_unique<TechTreeScene>());
}

void MainMenuScene::onEditor(Rml::DataModelHandle, Rml::Event&, const Args&) {
    m_services->scenes->push(std::make_unique<EditorScene>());
}

void MainMenuScene::onQuit(Rml::DataModelHandle, Rml::Event&, const Args&) {
    SDL_Event quit{};
    quit.type = SDL_EVENT_QUIT;
    SDL_PushEvent(&quit);
}

void MainMenuScene::onResize(float width, float height) {
    m_camera.setViewport(width, height);
}

} // namespace horde::scene
