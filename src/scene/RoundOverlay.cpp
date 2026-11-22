#include "scene/RoundOverlay.hpp"

#include <SDL3/SDL.h>

#include <RmlUi/Core.h>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <optional>
#include <utility>

#include "core/Paths.hpp"
#include "scene/Scene.hpp"
#include "ui/Icons.hpp"
#include "ui/RmlLayer.hpp"

namespace horde::scene {

namespace {

constexpr int kBasePips = 10;

std::string twoDigits(int value) {
    char text[12];
    std::snprintf(text, sizeof(text), "%02d", value);
    return text;
}

std::string upper(std::string text) {
    for (char& c : text) {
        c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
    }
    return text;
}

// "#rrggbb" for an RCSS colour.
std::string hexColor(glm::vec4 color) {
    char text[8];
    const auto channel = [](float value) {
        return static_cast<int>(std::lround(std::clamp(value, 0.0f, 1.0f) * 255.0f));
    };
    std::snprintf(text, sizeof(text), "#%02x%02x%02x", channel(color.r), channel(color.g), channel(color.b));
    return text;
}

// The dock's glyph for each kind: what it is, or failing that what it does.
const char* turretIcon(logic::TurretKind kind) {
    using logic::TurretKind;
    switch (kind) {
        case TurretKind::Flamer:
            return ui::icon::kFlame;
        case TurretKind::Zapper:
            return ui::icon::kZap;
        case TurretKind::Mortar:
            return ui::icon::kBomb;
        case TurretKind::Beam:
            return ui::icon::kRadar;
        case TurretKind::SpikeBlock:
            return ui::icon::kConstruction;
        case TurretKind::Barricade:
            return ui::icon::kBrickWall;
        case TurretKind::Mine:
            return ui::icon::kCircleDot;
        case TurretKind::Gunner:
            return ui::icon::kCrosshair;
        case TurretKind::TShirtCannon:
            return ui::icon::kShirt;
        case TurretKind::MerchSlinger:
            return ui::icon::kDisc3;
        case TurretKind::MerchStand:
            return ui::icon::kStore;
        case TurretKind::JazzSpeaker:
            return ui::icon::kMusic;
        case TurretKind::BassStack:
            return ui::icon::kSpeaker;
        case TurretKind::FeedbackAmp:
            return ui::icon::kAudioWaveform;
        case TurretKind::PepperSpray:
            return ui::icon::kSprayCan;
        case TurretKind::CrowdBarrier:
            return ui::icon::kFence;
    }
    return ui::icon::kCircle;
}

} // namespace

const std::vector<logic::TurretKind>& RoundOverlay::trayKinds() {
    static const std::vector<logic::TurretKind> kinds = [] {
        std::vector<logic::TurretKind> out;
        // A group per weapon category, then the kinds that have none.
        for (std::size_t group = 0; group <= logic::kWeaponCategoryCount; ++group) {
            const std::optional<logic::WeaponCategory> category =
                group == logic::kWeaponCategoryCount ? std::nullopt
                                                     : std::optional(static_cast<logic::WeaponCategory>(group));
            for (std::size_t i = 0; i < logic::kTurretKindCount; ++i) {
                const auto kind = static_cast<logic::TurretKind>(i);
                if (logic::turretDef(kind).category == category) {
                    out.push_back(kind);
                }
            }
        }
        return out;
    }();
    return kinds;
}

std::string turretKindLine(const logic::TurretDef& def) {
    std::string line;
    if (def.category) {
        line = logic::weaponCategoryName(*def.category);
        for (std::size_t c = 0; c < logic::kWeaponCategoryCount; ++c) {
            const auto other = static_cast<logic::WeaponCategory>(c);
            if ((def.secondaryCategories & logic::categoryBit(other)) != 0) {
                line += std::string(" + ") + logic::weaponCategoryName(other);
            }
        }
        line += " \xC2\xB7 ";
    }
    line += logic::turretClassName(def.placement);
    return upper(line);
}

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

    // In tray order, each glyph tinted as its kind is in the feed (by
    // category or placement class), lightened a third so the darker tints
    // still read on the dark tile.
    m_items.clear();
    std::optional<logic::WeaponCategory> lastCategory;
    for (const logic::TurretKind kind : trayKinds()) {
        const logic::TurretDef& def = logic::turretDef(kind);
        Item item;
        item.name = def.name;
        item.cost = static_cast<int>(def.cost);
        item.icon = turretIcon(kind);
        item.color = hexColor(glm::mix(logic::turretColor(def), glm::vec4(1.0f), 0.33f));
        item.group = !m_items.empty() && def.category != lastCategory;
        lastCategory = def.category;
        m_items.push_back(std::move(item));
    }
    m_basePips.assign(kBasePips, 1);
    m_horde.clear();
    m_money.clear();
    m_selected = -1;
    m_hovered = -1;
    m_prep = false;
    m_startPressed = false;
    m_problem.clear();
    m_hintName.clear();
    m_hintText.clear();
    m_panelOpen = false;
    m_ended = false;
    m_resultAction = ResultAction::None;

    Rml::DataModelConstructor constructor = m_context->CreateDataModel("round");
    if (!constructor) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "round: could not create the data model");
        return false;
    }
    if (auto item = constructor.RegisterStruct<Item>()) {
        item.RegisterMember("name", &Item::name);
        item.RegisterMember("cost", &Item::cost);
        item.RegisterMember("icon", &Item::icon);
        item.RegisterMember("color", &Item::color);
        item.RegisterMember("group", &Item::group);
        item.RegisterMember("disabled", &Item::disabled);
    }
    constructor.RegisterArray<Rml::Vector<Item>>();
    constructor.RegisterArray<Rml::Vector<int>>();
    constructor.Bind("items", &m_items);
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
    constructor.Bind("prep", &m_prep);
    constructor.Bind("hint_name", &m_hintName);
    constructor.Bind("hint_text", &m_hintText);
    constructor.Bind("problem", &m_problem);
    constructor.Bind("panel_open", &m_panelOpen);
    constructor.Bind("panel_name", &m_panelName);
    constructor.Bind("panel_kind", &m_panelKind);
    constructor.Bind("panel_detail", &m_panelDetail);
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
    constructor.BindEventCallback("hover", &RoundOverlay::onHover, this);
    constructor.BindEventCallback("start", &RoundOverlay::onStart, this);
    constructor.BindEventCallback("retry", &RoundOverlay::onRetry, this);
    constructor.BindEventCallback("quit", &RoundOverlay::onQuit, this);
    m_model = constructor.GetModelHandle();

    m_roundSeconds = 0.0f;
    m_tick = 0.0f;
    refreshClock();
    refreshHint();
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

template<typename T>
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

void RoundOverlay::setTrayState(int index, bool affordable) {
    if (!m_model || index < 0 || index >= static_cast<int>(m_items.size())) {
        return;
    }
    Item& item = m_items[static_cast<std::size_t>(index)];
    if (item.disabled != !affordable) {
        item.disabled = !affordable;
        m_model.DirtyVariable("items");
    }
    // Can't hold a selection it could no longer use.
    if (!affordable && m_selected == index) {
        clearSelection();
    }
}

void RoundOverlay::setProblem(const char* problem) {
    set("problem", m_problem, problem != nullptr ? std::string(problem) : std::string());
}

void RoundOverlay::setPrep(bool prep) {
    setValue("prep", m_prep, prep);
    refreshHint();
}

bool RoundOverlay::takeStartPressed() {
    return std::exchange(m_startPressed, false);
}

void RoundOverlay::refreshHint() {
    const int index = m_hovered >= 0 ? m_hovered : m_selected;
    if (index < 0 || index >= static_cast<int>(trayKinds().size())) {
        set("hint_name", m_hintName, "");
        set("hint_text", m_hintText,
            m_prep ? "Pick a turret, place it on the feed, then start."
                   : "Tap the feed to strike, or a turret to inspect.");
        return;
    }
    const logic::TurretDef& def = logic::turretDef(trayKinds()[static_cast<std::size_t>(index)]);
    set("hint_name", m_hintName, std::string(def.name) + "  \xC2\xB7  " + turretKindLine(def));
    std::string text = def.blurb;
    if (text.empty()) {
        text = def.placement == logic::TurretClass::Mount ? "Goes on top of a wall." : "Goes on open ground.";
    }
    if (def.directional) {
        text += " Q/E to turn.";
    }
    set("hint_text", m_hintText, text);
}

void RoundOverlay::showTurretPanel(const TurretPanel& panel) {
    setValue("panel_open", m_panelOpen, true);
    set("panel_name", m_panelName, std::string(panel.name));
    set("panel_kind", m_panelKind, std::string(panel.kind));
    set("panel_detail", m_panelDetail, std::string(panel.detail));
    setValue("panel_x", m_panelX, panel.screen.x);
    setValue("panel_y", m_panelY, panel.screen.y);
}

void RoundOverlay::hideTurretPanel() {
    setValue("panel_open", m_panelOpen, false);
}

void RoundOverlay::showResults(const Results& results) {
    const int seconds = static_cast<int>(results.seconds);
    setValue("ended", m_ended, true);
    setValue("won", m_won, results.won);
    set("result_title", m_resultTitle, results.won ? "Round won" : "Round lost");
    set("result_time", m_resultTime, "Time " + twoDigits(seconds / 60) + ":" + twoDigits(seconds % 60));
    set("result_kills", m_resultKills, "Kills " + std::to_string(results.killed));
    set("result_leaked", m_resultLeaked, "Leaked " + std::to_string(results.leaked));
    set("result_earned", m_resultEarned, "+$" + std::to_string(results.earned));
    set("result_total", m_resultTotal, "Total $" + std::to_string(results.total));
    setPrep(false);
    hideTurretPanel();
    clearSelection();
}

RoundOverlay::ResultAction RoundOverlay::takeResultAction() {
    return std::exchange(m_resultAction, ResultAction::None);
}

void RoundOverlay::onStart(Rml::DataModelHandle, Rml::Event&, const Args&) {
    if (m_prep) {
        m_startPressed = true;
    }
}

void RoundOverlay::onHover(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    const int index = args.empty() ? -1 : args[0].Get<int>(-1);
    if (index != m_hovered) {
        m_hovered = index;
        refreshHint();
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
        refreshHint();
    }
}

void RoundOverlay::onPick(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    // Picking the selected tower again puts it back.
    const int index = args.empty() ? -1 : args[0].Get<int>(-1);
    if (index < 0 || index >= static_cast<int>(m_items.size()) || m_ended ||
        m_items[static_cast<std::size_t>(index)].disabled) {
        return;
    }
    m_selected = m_selected == index ? -1 : index;
    m_model.DirtyVariable("selected");
    refreshHint();
}

} // namespace horde::scene
