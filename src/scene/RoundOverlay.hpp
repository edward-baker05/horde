#pragma once

#include <glm/vec2.hpp>

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Types.h>

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

#include "logic/Turret.hpp"

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

namespace horde::scene {

struct Services;

// The round's tablet app, drawn crisp over the feed: an OSD (date, clock, CAM,
// REC, elapsed, money, horde and base), the tray of turrets along the bottom
// with a line about the one under the pointer, the START button while the
// round is in prep, a placed turret's panel and the results card. An RmlUi
// document (assets/ui/round.rml) bound to a "round" data model on the
// tablet's context. It shows what LevelScene tells it and reports taps back;
// the rules live in logic::Round and the turrets in sim::TurretEffects.
class RoundOverlay {
public:
    // The tray's turret kinds, left to right: the weapons grouped by
    // category, then the kinds that predate categories.
    static const std::vector<logic::TurretKind>& trayKinds();

    struct Item {
        Rml::String name;
        int cost = 0;
        Rml::String icon;      // a glyph in the "lucide" font, see ui/Icons.hpp
        Rml::String color;     // the icon's tint: its category's, or its placement class's, lightened
        bool group = false;    // first of a category group, set apart from the one before
        bool disabled = false; // can't be picked: unaffordable
    };

    struct Hud {
        int money = 0;
        std::uint32_t dealt = 0; // killed + leaked
        std::uint32_t total = 0;
        std::uint32_t leaked = 0;
        std::uint32_t leakLimit = 1;
        float seconds = 0.0f; // the round's clock, which stops when it ends
    };

    // A placed turret, described: what it is and how worn it is.
    struct TurretPanel {
        Rml::String name;
        Rml::String kind;       // category and placement, e.g. "MERCH · BLOCKER"
        Rml::String detail;     // hit points for a blocker, else what it does
        glm::vec2 screen{0.0f}; // where the card goes, in tablet screen pixels
    };

    struct Results {
        bool won = false;
        float seconds = 0.0f;
        std::uint32_t killed = 0;
        std::uint32_t leaked = 0;
        int earned = 0;
        std::int64_t total = 0;
    };

    enum class ResultAction { None, Retry, Quit };

    ~RoundOverlay();

    bool show(Services& services);
    void hide();
    void update(float deltaTime);
    // Which feed camera is live, for the CAM and CH labels.
    void setCamera(std::size_t active, std::size_t count);

    // The tray's selected item (an index into trayKinds()), or -1.
    int selected() const {
        return m_selected;
    }

    // Puts the tray's selection back.
    void clearSelection();

    void setHud(const Hud& hud);
    // Tray item `index`: whether it can be paid for.
    void setTrayState(int index, bool affordable);
    // Why the selected turret can't go where the pointer is, or null.
    void setProblem(const char* problem);
    // The prep phase: the START button shows, and the hint says what to do.
    void setPrep(bool prep);
    // Whether START was tapped since the last call.
    bool takeStartPressed();
    void showTurretPanel(const TurretPanel& panel);
    void hideTurretPanel();
    // The round is over: the results card, which nothing closes but its buttons.
    void showResults(const Results& results);
    ResultAction takeResultAction();

private:
    using Args = Rml::VariantList;
    void onPick(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onHover(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onStart(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onRetry(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onQuit(Rml::DataModelHandle, Rml::Event&, const Args& args);
    template<typename T>
    void setValue(const char* name, T& field, T value);
    void refreshClock();
    // The line over the tray: the hovered item, else the selected one.
    void refreshHint();
    void set(const char* name, std::string& field, std::string value);

    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_document = nullptr;
    Rml::DataModelHandle m_model;

    std::string m_date;
    std::string m_time;
    std::string m_elapsed;
    std::string m_camera;
    std::string m_channel;
    std::string m_money;
    std::string m_horde;

    bool m_prep = false;
    bool m_startPressed = false;
    int m_hovered = -1;
    std::string m_hintName;
    std::string m_hintText;
    std::string m_problem;

    bool m_panelOpen = false;
    std::string m_panelName;
    std::string m_panelKind;
    std::string m_panelDetail;
    float m_panelX = 0.0f;
    float m_panelY = 0.0f;

    bool m_ended = false;
    bool m_won = false;
    std::string m_resultTitle;
    std::string m_resultTime;
    std::string m_resultKills;
    std::string m_resultLeaked;
    std::string m_resultEarned;
    std::string m_resultTotal;
    ResultAction m_resultAction = ResultAction::None;
    bool m_rec = true;
    int m_selected = -1;
    Rml::Vector<int> m_basePips;
    Rml::Vector<Item> m_items;

    float m_roundSeconds = 0.0f;
    float m_tick = 0.0f;
};

// Upper-case category and placement, as the panel and hint line show them:
// "MERCH · BLOCKER", or just "MOUNT" for a kind with no category.
std::string turretKindLine(const logic::TurretDef& def);

} // namespace horde::scene
