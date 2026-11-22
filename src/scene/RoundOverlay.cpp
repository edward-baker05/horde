#include "scene/RoundOverlay.hpp"

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <utility>

#include "core/Paths.hpp"
#include "logic/TowerStats.hpp"
#include "scene/Scene.hpp"
#include "ui/RmlLayer.hpp"

namespace horde::scene {

namespace {

constexpr int kBasePips = 10;

std::string twoDigits(int value) {
    char text[8];
    std::snprintf(text, sizeof(text), "%02d", value);
    return text;
}

} // namespace

RoundOverlay::~RoundOverlay() {
    hide();
}

bool RoundOverlay::show(Services& services) {
    if (m_document != nullptr) {
        return true;
    }
    if (services.ui == nullptr) {
        return false;
    }
    m_context = services.ui->context(ui::DeviceKind::Tablet);
    if (m_context == nullptr) {
        return false;
    }

    // In tray order: the four towers, then the two abilities. Colours match
    // the towers in the feed (towerColor in LevelScene.cpp).
    using logic::AbilityKind;
    using logic::TowerKind;
    m_towers = {
        {"Sniper", logic::towerStats(TowerKind::Sniper, 1).cost, "diamond", "", "#f4ca84", false, ""},
        {"Gunner", logic::towerStats(TowerKind::Gunner, 1).cost, "bar", "", "#90d7ff", false, ""},
        {"Pulse", logic::towerStats(TowerKind::Pulse, 1).cost, "ring", "", "#76e2e2", false, ""},
        {"Frost", logic::towerStats(TowerKind::Frost, 1).cost, "tri", "\xE2\x96\xB2", "#a1e4ae", false, ""},
        {"Bomb", logic::abilityStats(AbilityKind::Bomb).cost, "circle", "", "#ffa8a0", false, ""},
        {"Gravity", logic::abilityStats(AbilityKind::Gravity).cost, "square", "", "#d4bcff", false, ""},
    };
    m_basePips.assign(kBasePips, 1);
    m_horde.clear();
    m_money.clear();
    m_selected = -1;
    m_panelOpen = false;
    m_offerPressed = false;
    m_ended = false;
    m_resultAction = ResultAction::None;

    Rml::DataModelConstructor constructor = m_context->CreateDataModel("round");
    if (!constructor) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "round: could not create the data model");
        return false;
    }
    if (auto tower = constructor.RegisterStruct<Tower>()) {
        tower.RegisterMember("name", &Tower::name);
        tower.RegisterMember("cost", &Tower::cost);
        tower.RegisterMember("shape", &Tower::shape);
        tower.RegisterMember("glyph", &Tower::glyph);
        tower.RegisterMember("color", &Tower::color);
        tower.RegisterMember("disabled", &Tower::disabled);
        tower.RegisterMember("cooldown", &Tower::cooldown);
    }
    constructor.RegisterArray<Rml::Vector<Tower>>();
    constructor.RegisterArray<Rml::Vector<int>>();
    constructor.Bind("towers", &m_towers);
    constructor.Bind("base_pips", &m_basePips);
    constructor.Bind("selected", &m_selected);
    constructor.Bind("date", &m_date);
    constructor.Bind("time", &m_time);
    constructor.Bind("elapsed", &m_elapsed);
    constructor.Bind("camera", &m_camera);
    constructor.Bind("channel", &m_channel);
    constructor.Bind("money", &m_money);
    constructor.Bind("horde", &m_horde);
    constructor.Bind("rec", &m_rec);
    constructor.Bind("panel_open", &m_panelOpen);
    constructor.Bind("panel_name", &m_panelName);
    constructor.Bind("panel_level", &m_panelLevel);
    constructor.Bind("panel_hp", &m_panelHp);
    constructor.Bind("panel_action", &m_panelAction);
    constructor.Bind("panel_affordable", &m_panelAffordable);
    constructor.Bind("panel_x", &m_panelX);
    constructor.Bind("panel_y", &m_panelY);
    constructor.Bind("ended", &m_ended);
    constructor.Bind("won", &m_won);
    constructor.Bind("result_title", &m_resultTitle);
    constructor.Bind("result_time", &m_resultTime);
    constructor.Bind("result_kills", &m_resultKills);
    constructor.Bind("result_leaked", &m_resultLeaked);
    constructor.Bind("result_earned", &m_resultEarned);
    constructor.Bind("result_total", &m_resultTotal);
    constructor.BindEventCallback("pick", &RoundOverlay::onPick, this);
    constructor.BindEventCallback("offer", &RoundOverlay::onOffer, this);
    constructor.BindEventCallback("retry", &RoundOverlay::onRetry, this);
    constructor.BindEventCallback("quit", &RoundOverlay::onQuit, this);
    m_model = constructor.GetModelHandle();

    m_roundSeconds = 0.0f;
    m_tick = 0.0f;
    refreshClock();
    setCamera(0, 1);

    const std::string path = paths::asset("ui/round.rml").string();
    m_document = m_context->LoadDocument(path);
    if (m_document == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "round: could not load %s", path.c_str());
        m_context->RemoveDataModel("round");
        m_context = nullptr;
        m_model = {};
        return false;
    }
    // Shown without focus so keys (Esc to pause) still reach the scene.
    m_document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    return true;
}

void RoundOverlay::hide() {
    if (m_document != nullptr) {
        m_document->Close();
        m_document = nullptr;
    }
    if (m_context != nullptr) {
        m_context->RemoveDataModel("round");
        m_context = nullptr;
    }
    m_model = {};
}

void RoundOverlay::update(float deltaTime) {
    if (!m_model) {
        return;
    }
    m_roundSeconds += deltaTime;
    // REC blinks on the half second; the clocks tick with it.
    m_tick += deltaTime;
    if (m_tick >= 0.5f) {
        m_tick -= 0.5f;
        m_rec = !m_rec;
        m_model.DirtyVariable("rec");
        refreshClock();
    }
}

void RoundOverlay::setCamera(std::size_t active, std::size_t count) {
    const int index = static_cast<int>(active) + 1;
    set("camera", m_camera, "CAM-" + twoDigits(index));
    set("channel", m_channel, "CH " + twoDigits(index) + "/" + twoDigits(static_cast<int>(count)));
}

void RoundOverlay::refreshClock() {
    const int total = static_cast<int>(m_roundSeconds);
    set("elapsed", m_elapsed, twoDigits(total / 3600) + ":" + twoDigits(total / 60 % 60) + ":" + twoDigits(total % 60));

    SDL_Time now = 0;
    SDL_DateTime time{};
    if (SDL_GetCurrentTime(&now) && SDL_TimeToDateTime(now, &time, true)) {
        set("date", m_date, std::to_string(time.year) + "-" + twoDigits(time.month) + "-" + twoDigits(time.day));
        set("time", m_time, twoDigits(time.hour) + ":" + twoDigits(time.minute) + ":" + twoDigits(time.second));
    }
}

template <typename T>
void RoundOverlay::setValue(const char* name, T& field, T value) {
    if (field != value) {
        field = std::move(value);
        if (m_model) {
            m_model.DirtyVariable(name);
        }
    }
}

void RoundOverlay::setHud(const Hud& hud) {
    if (!m_model) {
        return;
    }
    // The elapsed clock is the round's own, so it stops with the round.
    m_roundSeconds = hud.seconds;
    set("money", m_money, "$ " + std::to_string(hud.money));
    // Padded to the total's width, like the clock, so the line doesn't jitter.
    const std::string total = std::to_string(hud.total);
    std::string dealt = std::to_string(std::min(hud.dealt, hud.total));
    dealt.insert(0, total.size() > dealt.size() ? total.size() - dealt.size() : 0, '0');
    set("horde", m_horde, "HORDE " + dealt + "/" + total);

    const std::uint32_t limit = std::max<std::uint32_t>(hud.leakLimit, 1);
    const std::uint32_t left = limit - std::min(hud.leaked, limit);
    const int lit = static_cast<int>(std::ceil(static_cast<double>(kBasePips) * left / limit));
    Rml::Vector<int> pips(kBasePips, 0);
    for (int i = 0; i < lit; ++i) {
        pips[static_cast<std::size_t>(i)] = 1;
    }
    if (pips != m_basePips) {
        m_basePips = std::move(pips);
        m_model.DirtyVariable("base_pips");
    }
}

void RoundOverlay::setTrayState(int index, bool affordable, float cooldown) {
    if (!m_model || index < 0 || index >= static_cast<int>(m_towers.size())) {
        return;
    }
    Tower& tower = m_towers[static_cast<std::size_t>(index)];
    const bool disabled = !affordable || cooldown > 0.0f;
    const Rml::String text = cooldown > 0.0f ? std::to_string(static_cast<int>(std::ceil(cooldown))) + "s" : "";
    if (tower.disabled != disabled || tower.cooldown != text) {
        tower.disabled = disabled;
        tower.cooldown = text;
        m_model.DirtyVariable("towers");
    }
    // Can't hold a selection it could no longer use.
    if (disabled && m_selected == index) {
        clearSelection();
    }
}

void RoundOverlay::showTowerPanel(const TowerPanel& panel) {
    setValue("panel_open", m_panelOpen, true);
    set("panel_name", m_panelName, std::string(panel.name));
    set("panel_level", m_panelLevel, "LV " + std::to_string(panel.level));
    set("panel_hp", m_panelHp, "HP " + std::to_string(panel.hp) + "/" + std::to_string(panel.maxHp));
    set("panel_action", m_panelAction, std::string(panel.action) + " $" + std::to_string(panel.cost));
    setValue("panel_affordable", m_panelAffordable, panel.affordable);
    setValue("panel_x", m_panelX, panel.screen.x);
    setValue("panel_y", m_panelY, panel.screen.y);
}

void RoundOverlay::hideTowerPanel() {
    setValue("panel_open", m_panelOpen, false);
    // A press on a panel that has gone (its tower destroyed that frame)
    // mustn't carry over and buy for the next tower opened.
    m_offerPressed = false;
}

bool RoundOverlay::takeOfferPressed() {
    return std::exchange(m_offerPressed, false);
}

void RoundOverlay::showResults(const Results& results) {
    const int seconds = static_cast<int>(results.seconds);
    setValue("ended", m_ended, true);
    setValue("won", m_won, results.won);
    set("result_title", m_resultTitle, results.won ? "WIN" : "LOSS");
    set("result_time", m_resultTime, "TIME " + twoDigits(seconds / 60) + ":" + twoDigits(seconds % 60));
    set("result_kills", m_resultKills, "KILLS " + std::to_string(results.killed));
    set("result_leaked", m_resultLeaked, "LEAKED " + std::to_string(results.leaked));
    set("result_earned", m_resultEarned, "+$" + std::to_string(results.earned));
    set("result_total", m_resultTotal, "TOTAL $" + std::to_string(results.total));
    hideTowerPanel();
    clearSelection();
}

RoundOverlay::ResultAction RoundOverlay::takeResultAction() {
    return std::exchange(m_resultAction, ResultAction::None);
}

void RoundOverlay::onOffer(Rml::DataModelHandle, Rml::Event&, const Args&) {
    if (m_panelOpen && m_panelAffordable) {
        m_offerPressed = true;
    }
}

void RoundOverlay::onRetry(Rml::DataModelHandle, Rml::Event&, const Args&) {
    m_resultAction = ResultAction::Retry;
}

void RoundOverlay::onQuit(Rml::DataModelHandle, Rml::Event&, const Args&) {
    m_resultAction = ResultAction::Quit;
}

void RoundOverlay::set(const char* name, std::string& field, std::string value) {
    if (field != value) {
        field = std::move(value);
        if (m_model) {
            m_model.DirtyVariable(name);
        }
    }
}

void RoundOverlay::clearSelection() {
    if (m_selected != -1) {
        m_selected = -1;
        if (m_model) {
            m_model.DirtyVariable("selected");
        }
    }
}

void RoundOverlay::onPick(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    // Picking the selected tower again puts it back.
    const int index = args.empty() ? -1 : args[0].Get<int>(-1);
    if (index < 0 || index >= static_cast<int>(m_towers.size()) || m_ended ||
        m_towers[static_cast<std::size_t>(index)].disabled) {
        return;
    }
    m_selected = m_selected == index ? -1 : index;
    m_model.DirtyVariable("selected");
}

} // namespace horde::scene
