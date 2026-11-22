#pragma once

#include <RmlUi/Core/DataModelHandle.h>
#include <RmlUi/Core/Event.h>
#include <RmlUi/Core/Types.h>

#include <glm/vec2.hpp>

#include <array>
#include <filesystem>
#include <functional>
#include <string>
#include <vector>

struct SDL_Window;

namespace Rml {
class Context;
class ElementDocument;
} // namespace Rml

namespace horde::editor {
struct EditorState;
}

namespace horde::scene {

struct Services;

// The level editor's tablet app: the round's readouts, dock and glass panels
// turned to authoring. The dock holds the tools, a card above it the armed
// tool's settings, undo and redo sit by the level's name at the top left, a
// card below them what's selected, and a tabbed sheet on the right the level
// (with Test play), its round, its contents and files. Saving and leaving are
// the phone's: EditorMenuScene, on Esc.
// An RmlUi document (assets/ui/editor.rml) bound to an "editor" data model on
// the tablet's context. Edits land straight in the EditorState it is given,
// one undo step each; what needs the scene comes back through takeAction().
class EditorOverlay {
public:
    enum class Action { None, Play };

    ~EditorOverlay();

    // `state` must outlive the overlay, or at least its hide().
    bool show(Services& services, editor::EditorState& state);
    void hide();
    // Covered by another scene on the tablet: hidden, but kept loaded.
    void setVisible(bool visible);

    // Mirrors the state into the document. `zoom` and `cursor` are for the OSD.
    void update(float deltaTime, float zoom, glm::vec2 cursor);

    Action takeAction();
    // Whether the level was swapped (opened or new) since the last call, so
    // the scene can frame it.
    bool takeLevelSwapped();

    // A text field has the keyboard: shortcuts must leave keys alone.
    bool typing() const {
        return m_focusGroup >= 0;
    }

    // Takes the keyboard back from whichever field has it, keeping its value.
    void blur();

    // The level was saved from elsewhere (the editor menu): refreshes the
    // saved levels and the Files tab's name.
    void levelSaved();

private:
    using Args = Rml::VariantList;

    struct Field {
        Rml::String label;
        Rml::String text;
        Rml::String unit;
    };

    struct Row {
        Rml::String label;
        bool on = false;
    };

    struct Tool {
        Rml::String name;
        Rml::String icon;  // a glyph in the "lucide" font, see ui/Icons.hpp
        Rml::String color; // the icon's tint
    };

    // One number: how to read it from the state and write it back.
    struct FieldSpec {
        std::string label;
        std::string unit;
        double step = 1.0;
        double min = 0.0;
        double max = 0.0;
        int decimals = 0;
        bool undoable = true; // a level edit, rather than a setting for the next one
        std::function<double(const editor::EditorState&)> get;
        std::function<void(editor::EditorState&, double)> set;
    };

    enum GroupId { Inspect, LevelBounds, Snap, Round, ToolSettings, kGroupCount };

    struct Group {
        const char* name;     // as the document names it in events
        const char* variable; // the bound array
        std::vector<FieldSpec> specs;
        Rml::Vector<Field> fields;
        std::string layout; // what the specs were built for; rebuilt when it changes
    };

    // The colours typed as hex: what's selected, the background, the paint
    // palette's current entry and the next wall's.
    enum HexId { HexInspect, HexBackground, HexPalette, HexNewWall, kHexCount };
    static constexpr int kHexGroup = 100;
    static constexpr int kNameGroup = 101;

    void bindModel(Rml::DataModelConstructor& constructor);

    // Each frame: the specs for what's selected and armed, then every value.
    void syncGroups();
    void syncHex();
    void syncLists();
    void syncLabels(float zoom, glm::vec2 cursor);
    void buildInspect(Group& group);
    void buildToolSettings(Group& group);
    void refreshLevels();

    int groupIndex(const Rml::String& name) const;
    // Writes `value`, clamped, if it differs: one undo step when it's a level edit.
    void apply(int group, int index, double value);
    void commitText(int group, int index);
    // Puts the value as it stands into its field, even the one being typed in.
    void showValue(int group, int index);
    std::string format(const FieldSpec& spec, double value) const;

    bool hexColor(int id, std::string& out) const;
    void setHexColor(int id, const std::string& hex);

    void onStep(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onFocus(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onBlur(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onChange(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onSwatch(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onToggle(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onTab(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onPickTool(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onPaintMode(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onBrush(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onPalette(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onPickItem(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onPickProblem(Rml::DataModelHandle, Rml::Event&, const Args& args);
    void onOpenLevel(Rml::DataModelHandle, Rml::Event&, const Args& args);

    template <typename T>
    void set(const char* name, T& field, T value);

    Rml::Context* m_context = nullptr;
    Rml::ElementDocument* m_document = nullptr;
    Rml::DataModelHandle m_model;
    SDL_Window* m_window = nullptr;
    editor::EditorState* m_state = nullptr;

    std::array<Group, kGroupCount> m_groups{{
        {"inspect", "inspect_fields", {}, {}, {}},
        {"level", "level_fields", {}, {}, {}},
        {"snap", "snap_fields", {}, {}, {}},
        {"round", "round_fields", {}, {}, {}},
        {"tool", "tool_fields", {}, {}, {}},
    }};
    // Never empty: they're bound to inline colours, even while hidden.
    std::array<std::string, kHexCount> m_hex{"#000000", "#000000", "#000000", "#000000"};
    // The field with the keyboard, whose text is the user's until it lands.
    int m_focusGroup = -1;
    int m_focusIndex = -1;

    Rml::Vector<Tool> m_tools;
    int m_tool = 0;
    Rml::Vector<Rml::String> m_swatches;
    Rml::Vector<Rml::String> m_palette;
    int m_paletteIndex = 0;
    bool m_paletteEdit = false;
    int m_paintMode = 0;
    bool m_brushSquare = false;
    bool m_autoSmooth = true;
    bool m_snapPosition = false;
    bool m_snapRotation = false;

    std::string m_tab = "level";
    std::string m_toolCard;
    std::string m_toolHint;

    bool m_inspectOpen = false;
    bool m_inspectHasColor = false;
    std::string m_inspectTitle;
    std::string m_inspectNote;

    Rml::Vector<Row> m_walls;
    Rml::Vector<Row> m_markers;
    Rml::Vector<Row> m_regions;
    Rml::Vector<Row> m_levels;
    Rml::Vector<Row> m_problems;
    Rml::Vector<Rml::String> m_warnings;
    std::vector<std::filesystem::path> m_levelPaths;
    std::vector<std::size_t> m_problemWalls;
    std::string m_saveName = "untitled";

    std::string m_levelName;
    bool m_dirty = false;
    std::string m_savedLabel;
    std::string m_status;
    std::string m_lastStatus;
    float m_statusSeconds = 0.0f;
    int m_problemCount = 0;
    std::string m_problemsLabel;
    std::string m_toolLabel;
    std::string m_zoomLabel;
    std::string m_cursorLabel;
    bool m_canUndo = false;
    bool m_canRedo = false;

    Action m_action = Action::None;
    bool m_levelSwapped = false;
};

} // namespace horde::scene
