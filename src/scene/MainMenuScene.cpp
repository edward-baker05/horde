#include "scene/MainMenuScene.hpp"

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <memory>

#include "audio/MusicPlayer.hpp"
#include "core/Paths.hpp"
#include "core/PolishMode.hpp"
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

    refreshLists();
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
    constructor.Bind("date", &m_date);
    constructor.Bind("featured", &m_featured);
    constructor.Bind("level_count", &m_levelCount);
    constructor.Bind("polish", &m_polish);
    constructor.BindEventCallback("open", &MainMenuScene::onOpen, this);
    constructor.BindEventCallback("back", &MainMenuScene::onBack, this);
    constructor.BindEventCallback("play", &MainMenuScene::onPlay, this);
    constructor.BindEventCallback("dev_level", &MainMenuScene::onDevLevel, this);
    constructor.BindEventCallback("dev_preset", &MainMenuScene::onDevPreset, this);
    constructor.BindEventCallback("upgrades", &MainMenuScene::onUpgrades, this);
    constructor.BindEventCallback("editor", &MainMenuScene::onEditor, this);
    constructor.BindEventCallback("quit", &MainMenuScene::onQuit, this);
    constructor.BindEventCallback("toggle_polish", &MainMenuScene::onTogglePolish, this);
    m_model = constructor.GetModelHandle();

    m_polish = polish::enabled();
    if (!loadDocument()) {
        return false;
    }
    playMusic();
    return true;
}

bool MainMenuScene::loadDocument() {
    // Each mode has its own document and stylesheet; both bind this model.
    const std::string documentPath =
        paths::asset(polish::enabled() ? "ui/main_menu.rml" : "ui/main_menu_normal.rml").string();
    Rml::ElementDocument* document = m_context->LoadDocument(documentPath);
    if (document == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "main menu: could not load %s", documentPath.c_str());
        return false;
    }
    if (m_document != nullptr) {
        m_document->Close();
    }
    m_document = document;
    m_document->Show();
    SDL_Log("Main menu document loaded: %s", documentPath.c_str());
    return true;
}

void MainMenuScene::onTogglePolish(Rml::DataModelHandle, Rml::Event&, const Args&) {
    // Only flagged here: swapping documents closes the one this click came
    // from, which frees the handler RmlUi is still running. update() swaps.
    m_togglePending = true;
}

void MainMenuScene::applyToggle() {
    m_togglePending = false;
    polish::setEnabled(!polish::enabled());
    m_polish = polish::enabled();
    m_model.DirtyVariable("polish");
    // The other mode's menu, still on the settings page, and its music.
    loadDocument();
    playMusic();
}

void MainMenuScene::playMusic() {
    // Polish mode: Chopin, Polonaise in A major Op. 40 No. 1 "Military", Aya
    // Higuchi / Musopen (public domain). About 12 dB hotter than the pause
    // mazurka, hence the gain. Normal mode is silent.
    if (m_services->music == nullptr) {
        return;
    }
    if (polish::enabled()) {
        m_services->music->play(paths::asset("music/polonaise_op40_no1.ogg"), 0.3f);
    } else {
        m_services->music->stop();
    }
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

void MainMenuScene::refreshLists() {
    m_levelPaths = logic::listLevels();
    m_levels.clear();
    for (const std::filesystem::path& path : m_levelPaths) {
        m_levels.push_back(logic::levelDisplayName(path));
    }
    m_presetNames.clear();
    m_presets.clear();
    for (const std::filesystem::path& path : listPresets()) {
        m_presetNames.push_back(presetName(path));
        m_presets.push_back(m_presetNames.back());
    }
    // As onOpen("play") picks: the built-in level, the only level, or a list.
    if (m_levels.empty()) {
        m_featured = "Built-in level";
    } else if (m_levels.size() == 1) {
        m_featured = m_levels.front();
    } else {
        m_featured = "Choose a level";
    }
    const std::size_t count = std::max<std::size_t>(m_levels.size(), 1);
    m_levelCount = std::to_string(count) + (count == 1 ? " LEVEL" : " LEVELS");
    if (m_model) {
        m_model.DirtyVariable("levels");
        m_model.DirtyVariable("presets");
        m_model.DirtyVariable("featured");
        m_model.DirtyVariable("level_count");
    }
}

void MainMenuScene::onRevealed() {
    // The editor may have saved levels while this scene was covered.
    refreshLists();
    // Back on the top-level list rather than wherever the player left from.
    setPage("root");
    if (m_document != nullptr) {
        m_document->Show();
    }
    playMusic();
}

void MainMenuScene::update(float deltaTime) {
    if (m_togglePending) {
        applyToggle();
    }
    m_clockTimer += deltaTime;
    if (m_clockTimer >= 1.0f) {
        m_clockTimer = 0.0f;
        refreshClock();
    }
}

namespace {

// Polish weekday (from Sunday) and month names, the months in the genitive as
// a date takes them: "piątek, 20 listopada".
constexpr const char* kDays[] = {"niedziela", "poniedziałek", "wtorek", "środa", "czwartek", "piątek", "sobota"};
constexpr const char* kMonths[] = {"stycznia", "lutego",   "marca",    "kwietnia",     "maja",      "czerwca",
                                   "lipca",    "sierpnia", "września", "października", "listopada", "grudnia"};

} // namespace

void MainMenuScene::refreshClock() {
    SDL_Time now = 0;
    SDL_DateTime time{};
    std::string text;
    std::string date;
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &time, true)) {
        text = std::to_string(time.hour / 10) + std::to_string(time.hour % 10) + ":" +
               std::to_string(time.minute / 10) + std::to_string(time.minute % 10);
        if (time.day_of_week >= 0 && time.day_of_week < 7 && time.month >= 1 && time.month <= 12) {
            date =
                std::string(kDays[time.day_of_week]) + ", " + std::to_string(time.day) + " " + kMonths[time.month - 1];
        }
    }
    if (date != m_date) {
        m_date = std::move(date);
        if (m_model) {
            m_model.DirtyVariable("date");
        }
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
        refreshLists();
        // One level means there is nothing to choose between: go straight into
        // it. With none saved, play the built-in level.
        if (m_levelPaths.size() > 1) {
            setPage("play");
        } else {
            playLevel(m_levelPaths.empty() ? std::filesystem::path{} : m_levelPaths.front());
        }
    } else if (target == "dev") {
        setPage("dev");
    } else if (target == "settings") {
        setPage("settings");
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
