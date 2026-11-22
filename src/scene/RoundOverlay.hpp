#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Types.h>

#include <glm/vec2.hpp>

#include <cstddef>
#include <cstdint>
#include <string>

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

namespace horde::scene {

struct Services;

// The round's tablet app, drawn crisp over the feed: an OSD (date, clock, CAM,
// REC, elapsed, money, horde and base), the tray of towers and abilities
// along the bottom, a placed tower's panel and the results card. An RmlUi
// document (assets/ui/round.rml) bound to a "round" data model on the
// tablet's context. It shows what LevelScene tells it and reports taps back;
// the rules live in logic::Round.
class RoundOverlay {
public:
    struct Tower {
        Rml::String name;
        int cost = 0;
        Rml::String shape; // circle, bar, tri, diamond, ring or square: a class in tablet.rcss
        Rml::String glyph; // text drawn in the shape, for those radii can't make
        Rml::String color;
        bool disabled = false;  // can't be picked: unaffordable or cooling down
        Rml::String cooldown;   // whole seconds left, or empty when ready
    };

    struct Hud {
        int money = 0;
        std::uint32_t dealt = 0; // killed + leaked
        std::uint32_t total = 0;
        std::uint32_t leaked = 0;
        std::uint32_t leakLimit = 1;
        float seconds = 0.0f; // the round's clock, which stops when it ends
    };

    struct TowerPanel {
        Rml::String name;
        int level = 1;
        int hp = 0;
        int maxHp = 0;
        Rml::String action; // UPGRADE or REPAIR
        int cost = 0;
        bool affordable = false;
        glm::vec2 screen{0.0f}; // the tower, in tablet screen pixels
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

    // The tray's selected tower, or -1.
    int selected() const {
        return m_selected;
    }

    // Puts the tray's selection back, once it has been used.
    void clearSelection();

    void setHud(const Hud& hud);
    // Tray item `index` (0-3 towers, 4-5 abilities): whether it can be paid
    // for, and seconds of cooldown left (0: ready).
    void setTrayState(int index, bool affordable, float cooldown);
    void showTowerPanel(const TowerPanel& panel);
    void hideTowerPanel();
    // Whether the panel's Upgrade/Repair was tapped since the last call.
    bool takeOfferPressed();
    // The round is over: the results card, which nothing closes but its buttons.
    void showResults(const Results& results);
    ResultAction takeResultAction();

private:
    using Args = Rml::VariantList;
    void onPick(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onOffer(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onRetry(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onQuit(Rml::DataModelHandle, Rml::Event&, const Args& args);
    template <typename T>
    void setValue(const char* name, T& field, T value);
    void refreshClock();
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

    bool m_panelOpen = false;
    std::string m_panelName;
    std::string m_panelLevel;
    std::string m_panelHp;
    std::string m_panelAction;
    bool m_panelAffordable = false;
    float m_panelX = 0.0f;
    float m_panelY = 0.0f;
    bool m_offerPressed = false;

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
    Rml::Vector<Tower> m_towers;

    float m_roundSeconds = 0.0f;
    float m_tick = 0.0f;
};

} // namespace horde::scene
