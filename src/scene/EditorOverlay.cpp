#include "scene/EditorOverlay.hpp"

#include <RmlUi/Core.h>
#include <SDL3/SDL.h>

#include <glm/trigonometric.hpp>

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <iterator>
#include <utility>
#include <variant>

#include "core/Paths.hpp"
#include "core/PolishMode.hpp"
#include "editor/Actions.hpp"
#include "editor/EditorState.hpp"
#include "editor/Markers.hpp"
#include "editor/Tools.hpp"
#include "gfx/GpuContext.hpp"
#include "logic/LevelFiles.hpp"
#include "logic/LevelValidation.hpp"
#include "scene/Scene.hpp"
#include "ui/Icons.hpp"
#include "ui/RmlLayer.hpp"

namespace horde::scene {

namespace {

using editor::EditorState;

// How long a status message stays in the OSD.
constexpr float kStatusSeconds = 6.0f;

// Tray order, which is also the number keys' order.
constexpr editor::Tool kTools[] = {editor::Tool::Select,   editor::Tool::Rectangle, editor::Tool::Triangle,
                                   editor::Tool::Circle,   editor::Tool::Polyline,  editor::Tool::Spawn,
                                   editor::Tool::Exit,     editor::Tool::FieldRegion, editor::Tool::Paint};

// Quick colours for walls, the background and the palette: greys for walls
// and grounds, then the round's tints and a few deeper ones.
constexpr const char* kSwatches[] = {"#1b1e24", "#262a32", "#3a3f4a", "#5a6170", "#8a919c", "#b4b4be",
                                     "#e8e8ee", "#7a5c48", "#c8a070", "#f4ca84", "#ffa8a0", "#e0605a",
                                     "#a1e4ae", "#4fae6a", "#76e2e2", "#90d7ff", "#4a7bd0", "#d4bcff"};

std::string toHex(logic::Rgb color) {
    char text[8];
    std::snprintf(text, sizeof(text), "#%02x%02x%02x", color.r, color.g, color.b);
    return text;
}

// "#rrggbb", "rrggbb", "#rgb" or "rgb", in either case.
bool parseHex(std::string text, logic::Rgb& out) {
    text.erase(std::remove_if(text.begin(), text.end(), [](unsigned char c) { return std::isspace(c) != 0; }),
               text.end());
    if (!text.empty() && text.front() == '#') {
        text.erase(text.begin());
    }
    if (text.size() == 3) {
        text = {text[0], text[0], text[1], text[1], text[2], text[2]};
    }
    if (text.size() != 6 || !std::all_of(text.begin(), text.end(), [](unsigned char c) { return std::isxdigit(c); })) {
        return false;
    }
    const unsigned long value = std::strtoul(text.c_str(), nullptr, 16);
    out = {static_cast<std::uint8_t>(value >> 16 & 0xff), static_cast<std::uint8_t>(value >> 8 & 0xff),
           static_cast<std::uint8_t>(value & 0xff)};
    return true;
}

const char* wallKindName(const logic::Wall& wall) {
    return std::holds_alternative<logic::RectangleShape>(wall.shape)  ? tr("Rectangle")
           : std::holds_alternative<logic::TriangleShape>(wall.shape) ? tr("Triangle")
           : std::holds_alternative<logic::CircleShape>(wall.shape)   ? tr("Circle")
                                                                      : tr("Polyline");
}

const char* edgeName(logic::Edge edge) {
    return edge == logic::Edge::North   ? tr("north")
           : edge == logic::Edge::South ? tr("south")
           : edge == logic::Edge::East  ? tr("east")
                                        : tr("west");
}

std::string printf1(const char* format, std::size_t a) {
    char text[160];
    std::snprintf(text, sizeof(text), format, a);
    return text;
}

// Getters and setters for one element of the level, by index. They do nothing
// once it's gone, which it can be for a frame after an undo or a delete.
template <typename T>
const T* item(const std::vector<T>& items, std::size_t index) {
    return index < items.size() ? &items[index] : nullptr;
}

template <typename T>
T* item(std::vector<T>& items, std::size_t index) {
    return index < items.size() ? &items[index] : nullptr;
}

// Rectangles and triangles both size by half extents; null for other walls.
template <typename WallT>
auto halfExtentsOf(WallT* wall) -> decltype(&std::get_if<logic::RectangleShape>(&wall->shape)->halfExtents) {
    if (wall == nullptr) {
        return nullptr;
    }
    if (auto* rect = std::get_if<logic::RectangleShape>(&wall->shape)) {
        return &rect->halfExtents;
    }
    if (auto* tri = std::get_if<logic::TriangleShape>(&wall->shape)) {
        return &tri->halfExtents;
    }
    return nullptr;
}

} // namespace

EditorOverlay::~EditorOverlay() {
    hide();
}

bool EditorOverlay::show(Services& services, editor::EditorState& state) {
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
    m_window = services.gpu != nullptr ? services.gpu->window() : nullptr;
    m_state = &state;

    // Tints like the round's towers; spawns and exits in the level's green and red.
    m_tools = {
        {editor::toolName(editor::Tool::Select), ui::icon::kMousePointer2, "#f2f2f7"},
        {editor::toolName(editor::Tool::Rectangle), ui::icon::kSquare, "#90d7ff"},
        {editor::toolName(editor::Tool::Triangle), ui::icon::kTriangle, "#a1e4ae"},
        {editor::toolName(editor::Tool::Circle), ui::icon::kCircle, "#76e2e2"},
        {editor::toolName(editor::Tool::Polyline), ui::icon::kSpline, "#f4ca84"},
        {editor::toolName(editor::Tool::Spawn), ui::icon::kArrowDownToLine, "#5fe08a"},
        {editor::toolName(editor::Tool::Exit), ui::icon::kArrowUpFromLine, "#ff7a70"},
        {editor::toolName(editor::Tool::FieldRegion), ui::icon::kWind, "#d4bcff"},
        {editor::toolName(editor::Tool::Paint), ui::icon::kPaintbrush, "#ffd27a"},
    };
    m_swatches.assign(std::begin(kSwatches), std::end(kSwatches));
    for (Group& group : m_groups) {
        group.layout = "-";
    }
    m_focusGroup = -1;
    m_action = Action::None;
    m_saveName = state.path.empty() ? "untitled" : logic::levelDisplayName(state.path);
    refreshLevels();

    Rml::DataModelConstructor constructor = m_context->CreateDataModel("editor");
    if (!constructor) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "editor: could not create the data model");
        return false;
    }
    bindModel(constructor);
    m_model = constructor.GetModelHandle();
    update(0.0f, 1.0f, glm::vec2(0.0f));

    const std::string path = paths::asset("ui/editor.rml").string();
    m_document = m_context->LoadDocument(path);
    if (m_document == nullptr) {
        SDL_LogError(SDL_LOG_CATEGORY_APPLICATION, "editor: could not load %s", path.c_str());
        m_context->RemoveDataModel("editor");
        m_context = nullptr;
        m_model = {};
        return false;
    }
    // Shown without focus so keys (tool numbers, undo) still reach the scene.
    m_document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    return true;
}

void EditorOverlay::bindModel(Rml::DataModelConstructor& constructor) {
    constructor.RegisterTransformFunc("tr", [](const Rml::VariantList& args) -> Rml::Variant {
        if (args.empty()) {
            return {};
        }
        return Rml::Variant(Rml::String(tr(args[0].Get<Rml::String>().c_str())));
    });

    if (auto field = constructor.RegisterStruct<Field>()) {
        field.RegisterMember("label", &Field::label);
        field.RegisterMember("text", &Field::text);
        field.RegisterMember("unit", &Field::unit);
    }
    if (auto row = constructor.RegisterStruct<Row>()) {
        row.RegisterMember("label", &Row::label);
        row.RegisterMember("on", &Row::on);
    }
    if (auto tool = constructor.RegisterStruct<Tool>()) {
        tool.RegisterMember("name", &Tool::name);
        tool.RegisterMember("icon", &Tool::icon);
        tool.RegisterMember("color", &Tool::color);
    }
    constructor.RegisterArray<Rml::Vector<Field>>();
    constructor.RegisterArray<Rml::Vector<Row>>();
    constructor.RegisterArray<Rml::Vector<Tool>>();
    constructor.RegisterArray<Rml::Vector<Rml::String>>();

    for (Group& group : m_groups) {
        constructor.Bind(group.variable, &group.fields);
    }
    constructor.Bind("inspect_hex", &m_hex[HexInspect]);
    constructor.Bind("background_hex", &m_hex[HexBackground]);
    constructor.Bind("palette_hex", &m_hex[HexPalette]);
    constructor.Bind("new_hex", &m_hex[HexNewWall]);

    constructor.Bind("tools", &m_tools);
    constructor.Bind("tool", &m_tool);
    constructor.Bind("swatches", &m_swatches);
    constructor.Bind("palette", &m_palette);
    constructor.Bind("palette_index", &m_paletteIndex);
    constructor.Bind("palette_edit", &m_paletteEdit);
    constructor.Bind("paint_mode", &m_paintMode);
    constructor.Bind("brush_square", &m_brushSquare);
    constructor.Bind("auto_smooth", &m_autoSmooth);
    constructor.Bind("snap_position", &m_snapPosition);
    constructor.Bind("snap_rotation", &m_snapRotation);
    constructor.Bind("tab", &m_tab);
    constructor.Bind("tool_card", &m_toolCard);
    constructor.Bind("tool_hint", &m_toolHint);
    constructor.Bind("inspect_open", &m_inspectOpen);
    constructor.Bind("inspect_has_color", &m_inspectHasColor);
    constructor.Bind("inspect_title", &m_inspectTitle);
    constructor.Bind("inspect_note", &m_inspectNote);
    constructor.Bind("walls", &m_walls);
    constructor.Bind("markers", &m_markers);
    constructor.Bind("regions", &m_regions);
    constructor.Bind("levels", &m_levels);
    constructor.Bind("problems", &m_problems);
    constructor.Bind("warnings", &m_warnings);
    constructor.Bind("save_name", &m_saveName);
    constructor.Bind("level_name", &m_levelName);
    constructor.Bind("dirty", &m_dirty);
    constructor.Bind("saved_label", &m_savedLabel);
    constructor.Bind("status", &m_status);
    constructor.Bind("problem_count", &m_problemCount);
    constructor.Bind("problems_label", &m_problemsLabel);
    constructor.Bind("tool_label", &m_toolLabel);
    constructor.Bind("zoom_label", &m_zoomLabel);
    constructor.Bind("cursor_label", &m_cursorLabel);
    constructor.Bind("can_undo", &m_canUndo);
    constructor.Bind("can_redo", &m_canRedo);

    constructor.BindEventCallback("step", &EditorOverlay::onStep, this);
    constructor.BindEventCallback("focus", &EditorOverlay::onFocus, this);
    constructor.BindEventCallback("blur", &EditorOverlay::onBlur, this);
    constructor.BindEventCallback("change", &EditorOverlay::onChange, this);
    constructor.BindEventCallback("swatch", &EditorOverlay::onSwatch, this);
    constructor.BindEventCallback("toggle", &EditorOverlay::onToggle, this);
    constructor.BindEventCallback("set_tab", &EditorOverlay::onTab, this);
    constructor.BindEventCallback("pick_tool", &EditorOverlay::onPickTool, this);
    constructor.BindEventCallback("set_paint_mode", &EditorOverlay::onPaintMode, this);
    constructor.BindEventCallback("set_brush", &EditorOverlay::onBrush, this);
    constructor.BindEventCallback("pick_palette", &EditorOverlay::onPalette, this);
    constructor.BindEventCallback("pick_item", &EditorOverlay::onPickItem, this);
    constructor.BindEventCallback("pick_problem", &EditorOverlay::onPickProblem, this);
    constructor.BindEventCallback("open_level", &EditorOverlay::onOpenLevel, this);

    constructor.BindEventCallback("remove", [this](Rml::DataModelHandle, Rml::Event&, const Args&) {
        editor::deleteSelected(*m_state);
    });
    constructor.BindEventCallback("undo", [this](Rml::DataModelHandle, Rml::Event&, const Args&) {
        editor::undo(*m_state);
    });
    constructor.BindEventCallback("redo", [this](Rml::DataModelHandle, Rml::Event&, const Args&) {
        editor::redo(*m_state);
    });
    constructor.BindEventCallback("save", [this](Rml::DataModelHandle, Rml::Event&, const Args&) {
        if (editor::saveLevelAs(*m_state, m_saveName)) {
            refreshLevels();
        }
    });
    constructor.BindEventCallback("new_level", [this](Rml::DataModelHandle, Rml::Event&, const Args&) {
        editor::newLevel(*m_state);
        set("save_name", m_saveName, std::string("untitled"));
        m_levelSwapped = true;
    });
    constructor.BindEventCallback("play", [this](Rml::DataModelHandle, Rml::Event&, const Args&) {
        m_action = Action::Play;
    });
}

void EditorOverlay::hide() {
    if (typing() && m_window != nullptr) {
        SDL_StopTextInput(m_window);
    }
    m_focusGroup = -1;
    if (m_document != nullptr) {
        m_document->Close();
        m_document = nullptr;
    }
    if (m_context != nullptr) {
        m_context->RemoveDataModel("editor");
        m_context = nullptr;
    }
    m_model = {};
    m_state = nullptr;
}

void EditorOverlay::setVisible(bool visible) {
    if (m_document == nullptr) {
        return;
    }
    if (visible) {
        m_document->Show(Rml::ModalFlag::None, Rml::FocusFlag::None);
    } else {
        blur();
        m_document->Hide();
    }
}

void EditorOverlay::blur() {
    if (m_context == nullptr) {
        return;
    }
    Rml::Element* focus = m_context->GetFocusElement();
    if (focus != nullptr && focus->GetOwnerDocument() == m_document && focus != m_document) {
        // Fires the field's blur, which lands what was typed.
        focus->Blur();
    }
}

EditorOverlay::Action EditorOverlay::takeAction() {
    return std::exchange(m_action, Action::None);
}

bool EditorOverlay::takeLevelSwapped() {
    return std::exchange(m_levelSwapped, false);
}

template <typename T>
void EditorOverlay::set(const char* name, T& field, T value) {
    if (field != value) {
        field = std::move(value);
        if (m_model) {
            m_model.DirtyVariable(name);
        }
    }
}

// --- Syncing -------------------------------------------------------------

void EditorOverlay::update(float deltaTime, float zoom, glm::vec2 cursor) {
    if (!m_model || m_state == nullptr) {
        return;
    }
    syncGroups();
    syncHex();
    syncLists();
    syncLabels(zoom, cursor);

    // A status line shows for a while, then clears so the same message can
    // show again.
    if (m_state->status != m_lastStatus) {
        m_lastStatus = m_state->status;
        m_statusSeconds = m_state->status.empty() ? 0.0f : kStatusSeconds;
    }
    m_statusSeconds = std::max(m_statusSeconds - deltaTime, 0.0f);
    if (m_statusSeconds <= 0.0f && !m_state->status.empty()) {
        m_state->status.clear();
        m_lastStatus.clear();
    }
    set("status", m_status, m_state->status);
}

void EditorOverlay::syncGroups() {
    const EditorState& state = *m_state;

    // What the inspector and tool card show, rebuilt when that changes.
    std::string inspectLayout = "none";
    const editor::Selection& selection = state.selection;
    if (selection.isWall() && selection.index < state.level.walls.size()) {
        inspectLayout = "wall" + std::to_string(selection.index) + ":" +
                        std::to_string(state.level.walls[selection.index].shape.index());
    } else if (selection.isFieldRegion() && selection.index < state.level.fieldRegions.size()) {
        inspectLayout = "region" + std::to_string(selection.index);
    } else if (selection.isMarker() && selection.index < state.level.markers.size()) {
        inspectLayout = "marker" + std::to_string(selection.index);
    }
    const std::string toolLayout = std::to_string(static_cast<int>(state.tool));

    for (int g = 0; g < kGroupCount; ++g) {
        Group& group = m_groups[static_cast<std::size_t>(g)];
        const std::string layout = g == Inspect ? inspectLayout : g == ToolSettings ? toolLayout : "fixed";
        bool dirty = false;
        if (group.layout != layout) {
            group.layout = layout;
            group.specs.clear();
            if (g == Inspect) {
                buildInspect(group);
            } else if (g == ToolSettings) {
                buildToolSettings(group);
            } else if (g == LevelBounds) {
                group.specs = {
                    {tr("Width"), "", 10.0, 50.0, 10000.0, 0, true,
                     [](const EditorState& s) { return s.level.size.x; },
                     [](EditorState& s, double v) {
                         s.level.size.x = static_cast<float>(v);
                         editor::clampAllMarkers(s.level);
                         logic::resizeWallGrid(s.level.wallGrid, s.level.size);
                     }},
                    {tr("Height"), "", 10.0, 50.0, 10000.0, 0, true,
                     [](const EditorState& s) { return s.level.size.y; },
                     [](EditorState& s, double v) {
                         s.level.size.y = static_cast<float>(v);
                         editor::clampAllMarkers(s.level);
                         logic::resizeWallGrid(s.level.wallGrid, s.level.size);
                     }},
                };
            } else if (g == Snap) {
                group.specs = {
                    {tr("Grid"), "", 1.0, 1.0, 500.0, 1, false, [](const EditorState& s) { return s.snap.gridSize; },
                     [](EditorState& s, double v) { s.snap.gridSize = static_cast<float>(v); }},
                    {tr("Step"), tr("deg"), 1.0, 1.0, 180.0, 1, false,
                     [](const EditorState& s) { return s.snap.rotationDegrees; },
                     [](EditorState& s, double v) { s.snap.rotationDegrees = static_cast<float>(v); }},
                };
            } else {
                using logic::RoundSettings;
                const auto count = [](const char* label, std::uint32_t RoundSettings::* member, double step,
                                      double most) {
                    return FieldSpec{tr(label), "", step, 1.0, most, 0, true,
                                     [member](const EditorState& s) { return static_cast<double>(s.level.round.*member); },
                                     [member](EditorState& s, double v) {
                                         s.level.round.*member = static_cast<std::uint32_t>(std::lround(v));
                                     }};
                };
                const auto money = [](const char* label, int RoundSettings::* member, double step) {
                    return FieldSpec{tr(label), "$", step, 0.0, 1'000'000'000.0, 0, true,
                                     [member](const EditorState& s) { return static_cast<double>(s.level.round.*member); },
                                     [member](EditorState& s, double v) {
                                         s.level.round.*member = static_cast<int>(std::lround(v));
                                     }};
                };
                const auto amount = [](const char* label, const char* unit, float RoundSettings::* member,
                                       double step, double most, int decimals) {
                    return FieldSpec{tr(label), unit, step, 0.0, most, decimals, true,
                                     [member](const EditorState& s) { return static_cast<double>(s.level.round.*member); },
                                     [member](EditorState& s, double v) {
                                         s.level.round.*member = static_cast<float>(v);
                                     }};
                };
                group.specs = {
                    count("Total units", &RoundSettings::totalUnits, 100.0, 4'000'000.0),
                    count("Unit HP", &RoundSettings::unitHp, 1.0, 100000.0),
                    count("Leak limit", &RoundSettings::leakLimit, 10.0, 4'000'000.0),
                    money("Start money", &RoundSettings::startMoney, 50.0),
                    money("Kill reward", &RoundSettings::killReward, 1.0),
                    amount("Spawn rate", "/s", &RoundSettings::baseSpawnRate, 1.0, 100000.0, 0),
                    amount("Spawn boost", "x", &RoundSettings::spawnBoost, 0.1, 100.0, 2),
                    money("Win reward", &RoundSettings::winReward, 10.0),
                    amount("Par time", "s", &RoundSettings::parSeconds, 10.0, 36000.0, 0),
                    amount("Bonus per second", "$", &RoundSettings::bonusPerSecond, 0.1, 1000.0, 2),
                };
            }
            group.fields.assign(group.specs.size(), Field{});
            for (std::size_t i = 0; i < group.specs.size(); ++i) {
                group.fields[i].label = group.specs[i].label;
                group.fields[i].unit = group.specs[i].unit;
            }
            dirty = true;
        }

        for (std::size_t i = 0; i < group.specs.size(); ++i) {
            // The field being typed in keeps the user's text until it lands.
            if (m_focusGroup == g && m_focusIndex == static_cast<int>(i)) {
                continue;
            }
            std::string text = format(group.specs[i], group.specs[i].get(state));
            if (group.fields[i].text != text) {
                group.fields[i].text = std::move(text);
                dirty = true;
            }
        }
        if (dirty) {
            m_model.DirtyVariable(group.variable);
        }
    }
}

void EditorOverlay::buildInspect(Group& group) {
    const EditorState& state = *m_state;
    const editor::Selection selection = state.selection;
    const std::size_t index = selection.index;
    std::vector<FieldSpec>& specs = group.specs;

    bool open = true;
    bool hasColor = false;
    std::string title;
    std::string note;

    const auto position = [&](auto items) {
        specs.push_back({"X", "", 1.0, -100000.0, 100000.0, 1, true,
                         [=](const EditorState& s) {
                             const auto* it = item(s.level.*items, index);
                             return it != nullptr ? static_cast<double>(it->center.x) : 0.0;
                         },
                         [=](EditorState& s, double v) {
                             if (auto* it = item(s.level.*items, index)) {
                                 it->center.x = static_cast<float>(v);
                             }
                         }});
        specs.push_back({"Y", "", 1.0, -100000.0, 100000.0, 1, true,
                         [=](const EditorState& s) {
                             const auto* it = item(s.level.*items, index);
                             return it != nullptr ? static_cast<double>(it->center.y) : 0.0;
                         },
                         [=](EditorState& s, double v) {
                             if (auto* it = item(s.level.*items, index)) {
                                 it->center.y = static_cast<float>(v);
                             }
                         }});
    };
    const auto rotation = [&](auto items) {
        specs.push_back({tr("Rotation"), tr("deg"), 1.0, -360.0, 360.0, 1, true,
                         [=](const EditorState& s) {
                             const auto* it = item(s.level.*items, index);
                             return it != nullptr ? static_cast<double>(glm::degrees(it->rotation)) : 0.0;
                         },
                         [=](EditorState& s, double v) {
                             if (auto* it = item(s.level.*items, index)) {
                                 it->rotation = glm::radians(static_cast<float>(v));
                             }
                         }});
    };

    if (selection.isWall() && index < state.level.walls.size()) {
        const logic::Wall& wall = state.level.walls[index];
        title = printf1(tr("Wall %zu"), index) + " · " + wallKindName(wall);
        hasColor = true;
        position(&logic::Level::walls);
        // Circles ignore rotation, so don't offer it for them.
        if (!std::holds_alternative<logic::CircleShape>(wall.shape)) {
            rotation(&logic::Level::walls);
            note = tr("Positive rotation is clockwise on screen.");
        }

        if (halfExtentsOf(&wall) != nullptr) {
            for (int axis = 0; axis < 2; ++axis) {
                specs.push_back({axis == 0 ? tr("Width") : tr("Height"), "", 1.0, 1.0, 100000.0, 1, true,
                                 [=](const EditorState& s) {
                                     const glm::vec2* half = halfExtentsOf(item(s.level.walls, index));
                                     return half != nullptr ? static_cast<double>((*half)[axis] * 2.0f) : 0.0;
                                 },
                                 [=](EditorState& s, double v) {
                                     if (glm::vec2* half = halfExtentsOf(item(s.level.walls, index))) {
                                         (*half)[axis] = static_cast<float>(v) * 0.5f;
                                     }
                                 }});
            }
        } else if (std::holds_alternative<logic::CircleShape>(wall.shape)) {
            specs.push_back({tr("Radius"), "", 1.0, 1.0, 100000.0, 1, true,
                             [=](const EditorState& s) {
                                 const logic::Wall* w = item(s.level.walls, index);
                                 const auto* c = w != nullptr ? std::get_if<logic::CircleShape>(&w->shape) : nullptr;
                                 return c != nullptr ? static_cast<double>(c->radius) : 0.0;
                             },
                             [=](EditorState& s, double v) {
                                 logic::Wall* w = item(s.level.walls, index);
                                 if (auto* c = w != nullptr ? std::get_if<logic::CircleShape>(&w->shape) : nullptr) {
                                     c->radius = static_cast<float>(v);
                                 }
                             }});
        } else {
            const auto& line = std::get<logic::PolylineShape>(wall.shape);
            note = printf1(tr("%zu points"), line.points.size());
            specs.push_back({tr("Thickness"), "", 0.5, 1.0, 1000.0, 2, true,
                             [=](const EditorState& s) {
                                 const logic::Wall* w = item(s.level.walls, index);
                                 const auto* p = w != nullptr ? std::get_if<logic::PolylineShape>(&w->shape) : nullptr;
                                 return p != nullptr ? static_cast<double>(p->thickness) : 0.0;
                             },
                             [=](EditorState& s, double v) {
                                 logic::Wall* w = item(s.level.walls, index);
                                 if (auto* p = w != nullptr ? std::get_if<logic::PolylineShape>(&w->shape) : nullptr) {
                                     p->thickness = static_cast<float>(v);
                                 }
                             }});
        }
    } else if (selection.isFieldRegion() && index < state.level.fieldRegions.size()) {
        title = printf1(tr("Field region %zu"), index);
        note = std::string(tr("Units inside follow its arrow. They pass through it.")) + " " +
               tr("0 flows east; positive turns clockwise.");
        position(&logic::Level::fieldRegions);
        rotation(&logic::Level::fieldRegions);
        for (int axis = 0; axis < 2; ++axis) {
            specs.push_back({axis == 0 ? tr("Width") : tr("Height"), "", 1.0, 1.0, 100000.0, 1, true,
                             [=](const EditorState& s) {
                                 const logic::FieldRegion* r = item(s.level.fieldRegions, index);
                                 return r != nullptr ? static_cast<double>(r->halfExtents[axis] * 2.0f) : 0.0;
                             },
                             [=](EditorState& s, double v) {
                                 if (logic::FieldRegion* r = item(s.level.fieldRegions, index)) {
                                     r->halfExtents[axis] = static_cast<float>(v) * 0.5f;
                                 }
                             }});
        }
        specs.push_back({tr("Strength"), "/s", 0.1, 0.1, 60.0, 2, true,
                         [=](const EditorState& s) {
                             const logic::FieldRegion* r = item(s.level.fieldRegions, index);
                             return r != nullptr ? static_cast<double>(r->strength) : 0.0;
                         },
                         [=](EditorState& s, double v) {
                             if (logic::FieldRegion* r = item(s.level.fieldRegions, index)) {
                                 r->strength = static_cast<float>(v);
                             }
                         }});
    } else if (selection.isMarker() && index < state.level.markers.size()) {
        const logic::Marker& marker = state.level.markers[index];
        title = std::string(marker.kind == logic::MarkerKind::Spawn ? tr("Spawn") : tr("Exit")) + " " +
                std::to_string(index);
        char edge[96];
        std::snprintf(edge, sizeof(edge), tr("Edge: %s"), edgeName(marker.edge));
        note = std::string(edge) + ". " + tr("Markers are placed on an edge and cannot be recoloured or rotated.");
        // Clamping after each edit keeps the marker on its edge and off its
        // neighbours, as dragging its grips does.
        specs.push_back({tr("Offset"), "", 1.0, 0.0, 100000.0, 1, true,
                         [=](const EditorState& s) {
                             const logic::Marker* m = item(s.level.markers, index);
                             return m != nullptr ? static_cast<double>(m->offset) : 0.0;
                         },
                         [=](EditorState& s, double v) {
                             if (logic::Marker* m = item(s.level.markers, index)) {
                                 m->offset = static_cast<float>(v);
                                 editor::clampMarker(s.level, index);
                             }
                         }});
        specs.push_back({tr("Length"), "", 1.0, editor::kMinimumMarkerLength, 100000.0, 1, true,
                         [=](const EditorState& s) {
                             const logic::Marker* m = item(s.level.markers, index);
                             return m != nullptr ? static_cast<double>(m->length) : 0.0;
                         },
                         [=](EditorState& s, double v) {
                             if (logic::Marker* m = item(s.level.markers, index)) {
                                 m->length = static_cast<float>(v);
                                 editor::clampMarker(s.level, index);
                             }
                         }});
    } else {
        open = false;
    }

    set("inspect_open", m_inspectOpen, open);
    set("inspect_has_color", m_inspectHasColor, hasColor);
    set("inspect_title", m_inspectTitle, title);
    set("inspect_note", m_inspectNote, note);
}

void EditorOverlay::buildToolSettings(Group& group) {
    std::string card;
    std::string hint;
    switch (m_state->tool) {
        case editor::Tool::Select:
            break;
        case editor::Tool::Rectangle:
        case editor::Tool::Triangle:
        case editor::Tool::Circle:
            card = "wall";
            hint = tr("Drag out a box. The tool stays armed.");
            break;
        case editor::Tool::Polyline:
            card = "line";
            hint = tr("Click to add points. Enter or a double click finishes; Esc drops it.");
            group.specs.push_back({tr("Thickness"), "", 0.5, 1.0, 200.0, 2, false,
                                   [](const EditorState& s) { return s.newPolylineThickness; },
                                   [](EditorState& s, double v) { s.newPolylineThickness = static_cast<float>(v); }});
            break;
        case editor::Tool::Spawn:
        case editor::Tool::Exit:
            card = "marker";
            hint = tr("Drag along an edge of the level.");
            break;
        case editor::Tool::FieldRegion:
            card = "field";
            hint = tr("Drag out a box. Units inside follow its arrow.");
            group.specs.push_back({tr("Strength"), "/s", 0.1, 0.1, 60.0, 2, false,
                                   [](const EditorState& s) { return s.newFieldStrength; },
                                   [](EditorState& s, double v) { s.newFieldStrength = static_cast<float>(v); }});
            break;
        case editor::Tool::Paint:
            card = "paint";
            hint = std::string(tr("Right drag does the opposite. Alt-click picks a colour.")) + " " +
                   tr("Shift-click: straight line. Shift+1-8: colour. [ ]: size.");
            group.specs.push_back({tr("Brush"), tr("cells"), 1.0, 1.0, editor::kMaxBrushSize, 0, false,
                                   [](const EditorState& s) { return static_cast<double>(s.paint.size); },
                                   [](EditorState& s, double v) { s.paint.size = static_cast<int>(std::lround(v)); }});
            break;
    }
    set("tool_card", m_toolCard, card);
    set("tool_hint", m_toolHint, hint);
}

bool EditorOverlay::hexColor(int id, std::string& out) const {
    const EditorState& state = *m_state;
    switch (id) {
        case HexInspect:
            if (state.selection.isWall() && state.selection.index < state.level.walls.size()) {
                out = toHex(state.level.walls[state.selection.index].color);
                return true;
            }
            return false;
        case HexBackground:
            out = toHex(state.level.backgroundColor);
            return true;
        case HexPalette:
            out = toHex(state.level.wallGrid.palette[state.paint.colorIndex % logic::kPaletteSize]);
            return true;
        case HexNewWall:
            out = toHex(state.newWallColor);
            return true;
        default:
            return false;
    }
}

void EditorOverlay::setHexColor(int id, const std::string& hex) {
    EditorState& state = *m_state;
    logic::Rgb color;
    if (!parseHex(hex, color)) {
        return;
    }
    std::string current;
    if (!hexColor(id, current) || current == toHex(color)) {
        return;
    }
    switch (id) {
        case HexInspect:
            state.beginMutation();
            state.level.walls[state.selection.index].color = color;
            break;
        case HexBackground:
            state.beginMutation();
            state.level.backgroundColor = color;
            break;
        case HexPalette:
            // Recolours every cell already painted with it.
            state.beginMutation();
            state.level.wallGrid.palette[state.paint.colorIndex % logic::kPaletteSize] = color;
            break;
        case HexNewWall:
            state.newWallColor = color;
            break;
        default:
            break;
    }
}

void EditorOverlay::syncHex() {
    static constexpr const char* kNames[] = {"inspect_hex", "background_hex", "palette_hex", "new_hex"};
    for (int id = 0; id < kHexCount; ++id) {
        if (m_focusGroup == kHexGroup && m_focusIndex == id) {
            continue;
        }
        std::string hex;
        if (hexColor(id, hex)) {
            set(kNames[id], m_hex[static_cast<std::size_t>(id)], hex);
        }
    }

    const logic::WallGrid& grid = m_state->level.wallGrid;
    Rml::Vector<Rml::String> palette;
    for (const logic::Rgb& color : grid.palette) {
        palette.push_back(toHex(color));
    }
    if (palette != m_palette) {
        m_palette = std::move(palette);
        m_model.DirtyVariable("palette");
    }
    set("palette_index", m_paletteIndex, static_cast<int>(m_state->paint.colorIndex));
}

void EditorOverlay::syncLists() {
    const EditorState& state = *m_state;
    const logic::Level& level = state.level;
    const editor::Selection& selection = state.selection;

    Rml::Vector<Row> walls;
    for (std::size_t i = 0; i < level.walls.size(); ++i) {
        walls.push_back({std::to_string(i) + "  " + wallKindName(level.walls[i]),
                         selection.isWall() && selection.index == i});
    }
    Rml::Vector<Row> markers;
    for (std::size_t i = 0; i < level.markers.size(); ++i) {
        const logic::Marker& marker = level.markers[i];
        markers.push_back({std::to_string(i) + "  " +
                               (marker.kind == logic::MarkerKind::Spawn ? tr("Spawn") : tr("Exit")) + " · " +
                               edgeName(marker.edge),
                           selection.isMarker() && selection.index == i});
    }
    Rml::Vector<Row> regions;
    for (std::size_t i = 0; i < level.fieldRegions.size(); ++i) {
        regions.push_back({std::to_string(i) + "  " + tr("Field region"),
                           selection.isFieldRegion() && selection.index == i});
    }

    const auto sameRows = [](const Rml::Vector<Row>& a, const Rml::Vector<Row>& b) {
        return std::equal(a.begin(), a.end(), b.begin(), b.end(),
                          [](const Row& x, const Row& y) { return x.label == y.label && x.on == y.on; });
    };
    const auto setRows = [&](const char* name, Rml::Vector<Row>& field, Rml::Vector<Row> rows) {
        if (!sameRows(field, rows)) {
            field = std::move(rows);
            m_model.DirtyVariable(name);
        }
    };
    setRows("walls", m_walls, std::move(walls));
    setRows("markers", m_markers, std::move(markers));
    setRows("regions", m_regions, std::move(regions));

    Rml::Vector<Row> levels;
    for (const std::filesystem::path& path : m_levelPaths) {
        levels.push_back({logic::levelDisplayName(path), state.path == path});
    }
    setRows("levels", m_levels, std::move(levels));

    const std::vector<logic::Problem> problems = logic::validate(level);
    Rml::Vector<Row> problemRows;
    m_problemWalls.clear();
    for (const logic::Problem& problem : problems) {
        char text[200];
        std::snprintf(text, sizeof(text), tr("Wall %zu: %s"), problem.wallIndex, problem.message.c_str());
        problemRows.push_back({text, false});
        m_problemWalls.push_back(problem.wallIndex);
    }
    setRows("problems", m_problems, std::move(problemRows));
    set("problem_count", m_problemCount, static_cast<int>(problems.size()));
    set("problems_label", m_problemsLabel,
        problems.empty() ? std::string() : printf1(tr("%zu problem(s) block saving"), problems.size()));

    Rml::Vector<Rml::String> warnings;
    for (const std::string& note : logic::warnings(level)) {
        warnings.push_back(note);
    }
    if (warnings != m_warnings) {
        m_warnings = std::move(warnings);
        m_model.DirtyVariable("warnings");
    }
}

void EditorOverlay::syncLabels(float zoom, glm::vec2 cursor) {
    const EditorState& state = *m_state;

    int tool = 0;
    for (int i = 0; i < static_cast<int>(std::size(kTools)); ++i) {
        if (kTools[i] == state.tool) {
            tool = i;
        }
    }
    set("tool", m_tool, tool);
    set("paint_mode", m_paintMode, static_cast<int>(state.paint.mode));
    set("brush_square", m_brushSquare, state.paint.shape == editor::BrushShape::Square);
    set("auto_smooth", m_autoSmooth, state.paint.autoSmooth);
    set("snap_position", m_snapPosition, state.snap.position);
    set("snap_rotation", m_snapRotation, state.snap.rotation);
    set("can_undo", m_canUndo, state.undo.canUndo());
    set("can_redo", m_canRedo, state.undo.canRedo());

    set("level_name", m_levelName,
        state.path.empty() ? std::string(tr("Untitled")) : logic::levelDisplayName(state.path));
    set("dirty", m_dirty, state.dirty);
    set("saved_label", m_savedLabel, std::string(state.dirty ? tr("Unsaved changes") : tr("Saved")));
    set("tool_label", m_toolLabel, std::string(editor::toolName(state.tool)));

    char text[64];
    std::snprintf(text, sizeof(text), "Zoom %.2fx", static_cast<double>(zoom));
    set("zoom_label", m_zoomLabel, std::string(text));
    std::snprintf(text, sizeof(text), "X %05.0f  Y %05.0f", static_cast<double>(cursor.x),
                  static_cast<double>(cursor.y));
    set("cursor_label", m_cursorLabel, std::string(text));
}

void EditorOverlay::levelSaved() {
    refreshLevels();
    if (m_state != nullptr && !m_state->path.empty()) {
        set("save_name", m_saveName, logic::levelDisplayName(m_state->path));
    }
}

void EditorOverlay::refreshLevels() {
    // Read when shown and after saving, not every frame: it's a directory walk.
    m_levelPaths = logic::listLevels();
}

// --- Fields --------------------------------------------------------------

std::string EditorOverlay::format(const FieldSpec& spec, double value) const {
    char text[48];
    std::snprintf(text, sizeof(text), "%.*f", spec.decimals, value);
    // No "-0.0" after rounding a small negative.
    if (std::string(text).find_first_not_of("-0.") == std::string::npos && text[0] == '-') {
        return text + 1;
    }
    return text;
}

int EditorOverlay::groupIndex(const Rml::String& name) const {
    if (name == "hex") {
        return kHexGroup;
    }
    if (name == "name") {
        return kNameGroup;
    }
    for (int g = 0; g < kGroupCount; ++g) {
        if (name == m_groups[static_cast<std::size_t>(g)].name) {
            return g;
        }
    }
    return -1;
}

void EditorOverlay::apply(int group, int index, double value) {
    if (group < 0 || group >= kGroupCount) {
        return;
    }
    Group& g = m_groups[static_cast<std::size_t>(group)];
    if (index < 0 || index >= static_cast<int>(g.specs.size())) {
        return;
    }
    const FieldSpec& spec = g.specs[static_cast<std::size_t>(index)];
    if (!std::isfinite(value)) {
        return;
    }
    value = std::clamp(value, spec.min, spec.max);
    // Compared as shown, so a value that rounds to the same text isn't an edit.
    if (format(spec, value) == format(spec, spec.get(*m_state))) {
        return;
    }
    if (spec.undoable) {
        m_state->beginMutation();
    }
    spec.set(*m_state, value);
}

void EditorOverlay::commitText(int group, int index) {
    if (group == kHexGroup) {
        if (index >= 0 && index < kHexCount) {
            setHexColor(index, m_hex[static_cast<std::size_t>(index)]);
        }
        return;
    }
    if (group < 0 || group >= kGroupCount) {
        return;
    }
    Group& g = m_groups[static_cast<std::size_t>(group)];
    if (index < 0 || index >= static_cast<int>(g.fields.size())) {
        return;
    }
    const std::string& text = g.fields[static_cast<std::size_t>(index)].text;
    char* end = nullptr;
    const double value = std::strtod(text.c_str(), &end);
    // Garbage is dropped; the next sync puts the real value back.
    if (end != text.c_str()) {
        apply(group, index, value);
    }
}

void EditorOverlay::showValue(int group, int index) {
    if (group == kHexGroup) {
        static constexpr const char* kNames[] = {"inspect_hex", "background_hex", "palette_hex", "new_hex"};
        std::string hex;
        if (index >= 0 && index < kHexCount && hexColor(index, hex)) {
            set(kNames[index], m_hex[static_cast<std::size_t>(index)], hex);
        }
        return;
    }
    if (group < 0 || group >= kGroupCount) {
        return;
    }
    Group& g = m_groups[static_cast<std::size_t>(group)];
    if (index < 0 || index >= static_cast<int>(g.specs.size())) {
        return;
    }
    const FieldSpec& spec = g.specs[static_cast<std::size_t>(index)];
    std::string text = format(spec, spec.get(*m_state));
    if (g.fields[static_cast<std::size_t>(index)].text != text) {
        g.fields[static_cast<std::size_t>(index)].text = std::move(text);
        m_model.DirtyVariable(g.variable);
    }
}

void EditorOverlay::onStep(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    if (args.size() < 3) {
        return;
    }
    const int group = groupIndex(args[0].Get<Rml::String>());
    const int index = args[1].Get<int>(-1);
    const double direction = args[2].Get<double>(0.0);
    const bool fast = args.size() > 3 && args[3].Get<int>(0) != 0;
    if (group < 0 || group >= kGroupCount) {
        return;
    }
    Group& g = m_groups[static_cast<std::size_t>(group)];
    if (index < 0 || index >= static_cast<int>(g.specs.size())) {
        return;
    }
    const FieldSpec& spec = g.specs[static_cast<std::size_t>(index)];
    apply(group, index, spec.get(*m_state) + direction * spec.step * (fast ? 10.0 : 1.0));
    // Stepping the field being typed in replaces what was typed.
    showValue(group, index);
}

void EditorOverlay::onFocus(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    if (args.size() < 2) {
        return;
    }
    if (!typing() && m_window != nullptr) {
        SDL_StartTextInput(m_window);
    }
    m_focusGroup = groupIndex(args[0].Get<Rml::String>());
    m_focusIndex = args[1].Get<int>(-1);
}

void EditorOverlay::onBlur(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    if (args.size() < 2) {
        return;
    }
    const int group = groupIndex(args[0].Get<Rml::String>());
    const int index = args[1].Get<int>(-1);
    commitText(group, index);
    if (m_focusGroup == group && m_focusIndex == index) {
        m_focusGroup = -1;
        m_focusIndex = -1;
        if (m_window != nullptr) {
            SDL_StopTextInput(m_window);
        }
    }
}

void EditorOverlay::onChange(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    // Every keystroke changes the text; only Enter lands it.
    if (args.size() < 3 || !args[2].Get<bool>(false)) {
        return;
    }
    const int group = groupIndex(args[0].Get<Rml::String>());
    const int index = args[1].Get<int>(-1);
    if (group == kNameGroup) {
        if (editor::saveLevelAs(*m_state, m_saveName)) {
            refreshLevels();
        }
        return;
    }
    commitText(group, index);
    // The field keeps the keyboard, now showing the value as it landed.
    showValue(group, index);
}

void EditorOverlay::onSwatch(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    if (args.size() < 2) {
        return;
    }
    const Rml::String target = args[0].Get<Rml::String>();
    const int index = args[1].Get<int>(-1);
    if (index < 0 || index >= static_cast<int>(m_swatches.size())) {
        return;
    }
    const int id = target == "inspect"      ? HexInspect
                   : target == "background" ? HexBackground
                   : target == "palette"    ? HexPalette
                   : target == "new"        ? HexNewWall
                                            : -1;
    if (id < 0) {
        return;
    }
    setHexColor(id, m_swatches[static_cast<std::size_t>(index)]);
    showValue(kHexGroup, id);
}

void EditorOverlay::onToggle(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    if (args.empty()) {
        return;
    }
    const Rml::String name = args[0].Get<Rml::String>();
    if (name == "snap_position") {
        m_state->snap.position = !m_state->snap.position;
    } else if (name == "snap_rotation") {
        m_state->snap.rotation = !m_state->snap.rotation;
    } else if (name == "auto_smooth") {
        m_state->paint.autoSmooth = !m_state->paint.autoSmooth;
    }
}

void EditorOverlay::onTab(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    if (args.empty()) {
        return;
    }
    const std::string tab = args[0].Get<Rml::String>();
    // Tapping the open tab folds the sheet out of the way.
    set("tab", m_tab, tab == m_tab ? std::string() : tab);
    if (m_tab == "files") {
        refreshLevels();
    }
}

void EditorOverlay::onPickTool(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    const int index = args.empty() ? -1 : args[0].Get<int>(-1);
    if (index >= 0 && index < static_cast<int>(std::size(kTools))) {
        m_state->tool = kTools[index];
    }
}

void EditorOverlay::onPaintMode(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    const int mode = args.empty() ? -1 : args[0].Get<int>(-1);
    if (mode >= 0 && mode <= static_cast<int>(editor::PaintMode::Slope)) {
        m_state->paint.mode = static_cast<editor::PaintMode>(mode);
    }
}

void EditorOverlay::onBrush(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    if (!args.empty()) {
        m_state->paint.shape = args[0].Get<int>(0) != 0 ? editor::BrushShape::Square : editor::BrushShape::Circle;
    }
}

void EditorOverlay::onPalette(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    const int index = args.empty() ? -1 : args[0].Get<int>(-1);
    if (index < 0 || index >= static_cast<int>(logic::kPaletteSize)) {
        return;
    }
    // Tapping the colour already in hand opens it for editing.
    if (index == static_cast<int>(m_state->paint.colorIndex)) {
        set("palette_edit", m_paletteEdit, !m_paletteEdit);
    } else {
        m_state->paint.colorIndex = static_cast<std::uint8_t>(index);
    }
}

void EditorOverlay::onPickItem(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    if (args.size() < 2) {
        return;
    }
    const Rml::String kind = args[0].Get<Rml::String>();
    const int index = args[1].Get<int>(-1);
    if (index < 0) {
        return;
    }
    const editor::SelectionKind selection = kind == "wall"     ? editor::SelectionKind::Wall
                                            : kind == "marker" ? editor::SelectionKind::Marker
                                                               : editor::SelectionKind::FieldRegion;
    m_state->selection = editor::Selection{selection, static_cast<std::size_t>(index)};
}

void EditorOverlay::onPickProblem(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    const int index = args.empty() ? -1 : args[0].Get<int>(-1);
    if (index >= 0 && index < static_cast<int>(m_problemWalls.size())) {
        m_state->selection = editor::Selection{editor::SelectionKind::Wall, m_problemWalls[static_cast<std::size_t>(index)]};
    }
}

void EditorOverlay::onOpenLevel(Rml::DataModelHandle, Rml::Event&, const Args& args) {
    const int index = args.empty() ? -1 : args[0].Get<int>(-1);
    if (index < 0 || index >= static_cast<int>(m_levelPaths.size())) {
        return;
    }
    const std::filesystem::path path = m_levelPaths[static_cast<std::size_t>(index)];
    if (editor::openLevel(*m_state, path)) {
        set("save_name", m_saveName, logic::levelDisplayName(path));
        m_levelSwapped = true;
    }
}

} // namespace horde::scene
