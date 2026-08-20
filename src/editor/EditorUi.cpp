#include "editor/EditorUi.hpp"

#include <glm/trigonometric.hpp>
#include <imgui.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
#include <optional>
#include <utility>
#include <variant>
#include <vector>

#include "editor/EditorState.hpp"
#include "editor/Markers.hpp"
#include "editor/Tools.hpp"
#include "logic/LevelFiles.hpp"
#include "logic/LevelIO.hpp"
#include "logic/LevelValidation.hpp"

namespace horde::editor {

bool deleteSelected(EditorState& state) {
    if (state.selection.isWall() && state.selection.index < state.level.walls.size()) {
        state.beginMutation();
        state.level.walls.erase(state.level.walls.begin() + static_cast<std::ptrdiff_t>(state.selection.index));
        state.selection.clear();
        return true;
    }

    if (state.selection.isMarker() && state.selection.index < state.level.markers.size()) {
        const logic::MarkerKind kind = state.level.markers[state.selection.index].kind;
        if (logic::countMarkers(state.level, kind) <= 1) {
            state.status = "Every level needs at least one spawn and one exit.";
            return false;
        }
        state.beginMutation();
        state.level.markers.erase(state.level.markers.begin() + static_cast<std::ptrdiff_t>(state.selection.index));
        state.selection.clear();
        return true;
    }

    return false;
}

namespace {

void drawToolbar(EditorState& state) {
    ImGui::SetNextWindowPos(ImVec2(320.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(240.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Tools");

    const Tool tools[] = {Tool::Select,   Tool::Rectangle, Tool::Triangle, Tool::Circle,
                          Tool::Polyline, Tool::Spawn,     Tool::Exit};

    for (int i = 0; i < static_cast<int>(std::size(tools)); ++i) {
        const Tool tool = tools[i];
        char label[64];
        std::snprintf(label, sizeof(label), "%d  %s", i + 1, toolName(tool));

        if (ImGui::RadioButton(label, state.tool == tool)) {
            state.tool = tool;
        }
    }

    ImGui::SeparatorText("New wall colour");

    float color[3] = {static_cast<float>(state.newWallColor.r) / 255.0f,
                      static_cast<float>(state.newWallColor.g) / 255.0f,
                      static_cast<float>(state.newWallColor.b) / 255.0f};
    if (ImGui::ColorEdit3("##newcolor", color)) {
        state.newWallColor = {static_cast<std::uint8_t>(color[0] * 255.0f),
                              static_cast<std::uint8_t>(color[1] * 255.0f),
                              static_cast<std::uint8_t>(color[2] * 255.0f)};
    }

    ImGui::SeparatorText("New polyline");
    ImGui::DragFloat("Thickness", &state.newPolylineThickness, 0.25f, 1.0f, 200.0f);

    ImGui::End();
}

void drawLevelPanel(EditorState& state, bool& wantsExit) {
    ImGui::SetNextWindowPos(ImVec2(20.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280.0f, 260.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Level");

    if (ImGui::Button("Back to menu", ImVec2(160.0f, 0.0f))) {
        wantsExit = true;
    }

    ImGui::SeparatorText("Bounds");

    float size[2] = {state.level.size.x, state.level.size.y};
    if (ImGui::DragFloat2("Size", size, 1.0f, 50.0f, 10000.0f, "%.0f")) {
        state.beginMutation();
        state.level.size = {size[0], size[1]};
        clampAllMarkers(state.level);
    }

    ImGui::SeparatorText("Background");

    float background[3] = {static_cast<float>(state.level.backgroundColor.r) / 255.0f,
                           static_cast<float>(state.level.backgroundColor.g) / 255.0f,
                           static_cast<float>(state.level.backgroundColor.b) / 255.0f};
    if (ImGui::ColorEdit3("Colour", background)) {
        state.beginMutation();
        state.level.backgroundColor = {static_cast<std::uint8_t>(background[0] * 255.0f),
                                       static_cast<std::uint8_t>(background[1] * 255.0f),
                                       static_cast<std::uint8_t>(background[2] * 255.0f)};
    }

    ImGui::SeparatorText("Snapping");

    ImGui::Checkbox("Snap position", &state.snap.position);
    ImGui::BeginDisabled(!state.snap.position);
    ImGui::DragFloat("Grid", &state.snap.gridSize, 0.5f, 1.0f, 500.0f, "%.1f");
    ImGui::EndDisabled();

    ImGui::Checkbox("Snap rotation", &state.snap.rotation);
    ImGui::BeginDisabled(!state.snap.rotation);
    ImGui::DragFloat("Step", &state.snap.rotationDegrees, 0.5f, 1.0f, 180.0f, "%.1f deg");
    ImGui::EndDisabled();

    ImGui::SeparatorText("History");

    ImGui::BeginDisabled(!state.undo.canUndo());
    if (ImGui::Button("Undo")) {
        state.undo.undo(state.level);
        state.selection.clear();
        state.dirty = true;
    }
    ImGui::EndDisabled();

    ImGui::SameLine();

    ImGui::BeginDisabled(!state.undo.canRedo());
    if (ImGui::Button("Redo")) {
        state.undo.redo(state.level);
        state.selection.clear();
        state.dirty = true;
    }
    ImGui::EndDisabled();

    if (!state.status.empty()) {
        ImGui::SeparatorText("Status");
        ImGui::TextWrapped("%s", state.status.c_str());
    }

    ImGui::End();
}

void drawInspector(EditorState& state) {
    ImGui::SetNextWindowPos(ImVec2(20.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280.0f, 320.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Inspector");

    if (state.selection.kind == SelectionKind::None) {
        ImGui::TextDisabled("Nothing selected.");
        ImGui::End();
        return;
    }

    if (state.selection.isWall()) {
        logic::Wall& wall = state.level.walls[state.selection.index];

        ImGui::Text("Wall %zu", state.selection.index);
        ImGui::SeparatorText("Transform");

        float center[2] = {wall.center.x, wall.center.y};
        if (ImGui::DragFloat2("Centre", center, 0.5f)) {
            state.beginMutation();
            wall.center = {center[0], center[1]};
        }

        // Circles ignore rotation, so do not offer it for them.
        if (!std::holds_alternative<logic::CircleShape>(wall.shape)) {
            float degrees = glm::degrees(wall.rotation);
            if (ImGui::DragFloat("Rotation", &degrees, 1.0f, -360.0f, 360.0f, "%.1f deg")) {
                state.beginMutation();
                wall.rotation = glm::radians(degrees);
            }
            ImGui::TextDisabled("Positive rotation is clockwise on screen.");
        }

        ImGui::SeparatorText("Size");

        if (auto* rect = std::get_if<logic::RectangleShape>(&wall.shape)) {
            float extents[2] = {rect->halfExtents.x * 2.0f, rect->halfExtents.y * 2.0f};
            if (ImGui::DragFloat2("Width/Height", extents, 0.5f, 1.0f, 100000.0f)) {
                state.beginMutation();
                rect->halfExtents = {std::max(extents[0], 1.0f) * 0.5f, std::max(extents[1], 1.0f) * 0.5f};
            }
        } else if (auto* tri = std::get_if<logic::TriangleShape>(&wall.shape)) {
            float extents[2] = {tri->halfExtents.x * 2.0f, tri->halfExtents.y * 2.0f};
            if (ImGui::DragFloat2("Width/Height", extents, 0.5f, 1.0f, 100000.0f)) {
                state.beginMutation();
                tri->halfExtents = {std::max(extents[0], 1.0f) * 0.5f, std::max(extents[1], 1.0f) * 0.5f};
            }
        } else if (auto* circle = std::get_if<logic::CircleShape>(&wall.shape)) {
            float radius = circle->radius;
            if (ImGui::DragFloat("Radius", &radius, 0.5f, 1.0f, 100000.0f)) {
                state.beginMutation();
                circle->radius = std::max(radius, 1.0f);
            }
        } else {
            auto& line = std::get<logic::PolylineShape>(wall.shape);
            ImGui::Text("%zu points", line.points.size());
            float thickness = line.thickness;
            if (ImGui::DragFloat("Thickness", &thickness, 0.25f, 1.0f, 1000.0f)) {
                state.beginMutation();
                line.thickness = std::max(thickness, 1.0f);
            }
        }

        ImGui::SeparatorText("Colour");

        float color[3] = {static_cast<float>(wall.color.r) / 255.0f, static_cast<float>(wall.color.g) / 255.0f,
                          static_cast<float>(wall.color.b) / 255.0f};
        if (ImGui::ColorPicker3("##wallcolor", color, ImGuiColorEditFlags_DisplayRGB | ImGuiColorEditFlags_Uint8)) {
            state.beginMutation();
            wall.color = {static_cast<std::uint8_t>(color[0] * 255.0f), static_cast<std::uint8_t>(color[1] * 255.0f),
                          static_cast<std::uint8_t>(color[2] * 255.0f)};
        }
    } else {
        logic::Marker& marker = state.level.markers[state.selection.index];

        ImGui::Text("%s %zu", marker.kind == logic::MarkerKind::Spawn ? "Spawn" : "Exit", state.selection.index);
        ImGui::TextDisabled("Markers are placed on an edge and cannot be recoloured or rotated.");
        ImGui::Text("Edge: %s", marker.edge == logic::Edge::North   ? "north"
                                : marker.edge == logic::Edge::South ? "south"
                                : marker.edge == logic::Edge::East  ? "east"
                                                                    : "west");
        ImGui::Text("Offset: %.1f   Length: %.1f", marker.offset, marker.length);
    }

    ImGui::Separator();

    if (ImGui::Button("Delete", ImVec2(120.0f, 0.0f))) {
        deleteSelected(state);
    }

    ImGui::End();
}

void drawWallList(EditorState& state) {
    ImGui::SetNextWindowPos(ImVec2(320.0f, 340.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(240.0f, 280.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Contents");

    ImGui::SeparatorText("Walls (last draws on top)");

    for (std::size_t i = 0; i < state.level.walls.size(); ++i) {
        const logic::Wall& wall = state.level.walls[i];
        const char* kind = std::holds_alternative<logic::RectangleShape>(wall.shape)  ? "Rectangle"
                           : std::holds_alternative<logic::TriangleShape>(wall.shape) ? "Triangle"
                           : std::holds_alternative<logic::CircleShape>(wall.shape)   ? "Circle"
                                                                                      : "Polyline";

        char label[64];
        std::snprintf(label, sizeof(label), "%zu  %s##wall%zu", i, kind, i);

        const bool selected = state.selection.isWall() && state.selection.index == i;
        if (ImGui::Selectable(label, selected)) {
            state.selection = Selection{SelectionKind::Wall, i};
        }
    }

    if (state.level.walls.empty()) {
        ImGui::TextDisabled("No walls yet.");
    }

    ImGui::SeparatorText("Markers");

    for (std::size_t i = 0; i < state.level.markers.size(); ++i) {
        const logic::Marker& marker = state.level.markers[i];
        char label[64];
        std::snprintf(label, sizeof(label), "%zu  %s##marker%zu", i,
                      marker.kind == logic::MarkerKind::Spawn ? "Spawn" : "Exit", i);

        const bool selected = state.selection.isMarker() && state.selection.index == i;
        if (ImGui::Selectable(label, selected)) {
            state.selection = Selection{SelectionKind::Marker, i};
        }
    }

    ImGui::End();
}

void drawFilesPanel(EditorState& state) {
    ImGui::SetNextWindowPos(ImVec2(600.0f, 20.0f), ImGuiCond_FirstUseEver);
    ImGui::SetNextWindowSize(ImVec2(280.0f, 300.0f), ImGuiCond_FirstUseEver);
    ImGui::Begin("Files");

    // A plain list of the levels that actually exist beats a native dialog that
    // opens somewhere unrelated, and works identically on all three platforms.
    ImGui::SeparatorText("Open");

    const std::vector<std::filesystem::path> levels = logic::listLevels();
    if (levels.empty()) {
        ImGui::TextDisabled("No levels saved yet.");
    }

    for (const std::filesystem::path& path : levels) {
        const std::string name = logic::levelDisplayName(path);
        if (ImGui::Selectable(name.c_str(), state.path == path)) {
            std::string error;
            if (std::optional<logic::Level> loaded = logic::loadLevel(path, &error)) {
                state.level = std::move(*loaded);
                state.path = path;
                state.selection.clear();
                state.hovered.clear();
                state.undo.clear();
                state.dirty = false;
                state.status = "Opened " + name;
            } else {
                state.status = "Could not open " + name + ": " + error;
            }
        }
    }

    ImGui::SeparatorText("Save");

    static char nameBuffer[128] = "untitled";
    ImGui::InputText("Name", nameBuffer, sizeof(nameBuffer));

    const std::vector<logic::Problem> problems = logic::validate(state.level);

    ImGui::BeginDisabled(!problems.empty());
    if (ImGui::Button("Save", ImVec2(120.0f, 0.0f))) {
        const std::filesystem::path path = logic::levelPathForName(nameBuffer);
        std::string error;
        if (logic::saveLevel(state.level, path, &error)) {
            state.path = path;
            state.dirty = false;
            state.status = "Saved " + logic::levelDisplayName(path);
        } else {
            state.status = "Could not save: " + error;
        }
    }
    ImGui::EndDisabled();

    if (!problems.empty()) {
        ImGui::SameLine();
        ImGui::TextColored(ImVec4(1.0f, 0.6f, 0.3f, 1.0f), "fix %zu problem(s)", problems.size());
    }

    ImGui::SameLine();
    if (ImGui::Button("New", ImVec2(80.0f, 0.0f))) {
        state.level = logic::makeDefaultLevel();
        state.path.clear();
        state.selection.clear();
        state.hovered.clear();
        state.undo.clear();
        state.dirty = false;
        state.status = "New level";
    }

    if (state.dirty) {
        ImGui::TextColored(ImVec4(1.0f, 0.85f, 0.3f, 1.0f), "Unsaved changes");
    }

    ImGui::SeparatorText("Problems");

    if (problems.empty()) {
        ImGui::TextDisabled("None. This level can be saved.");
    }

    for (const logic::Problem& problem : problems) {
        char label[160];
        std::snprintf(label, sizeof(label), "Wall %zu: %s##problem%zu", problem.wallIndex, problem.message.c_str(),
                      problem.wallIndex);
        if (ImGui::Selectable(label)) {
            state.selection = Selection{SelectionKind::Wall, problem.wallIndex};
        }
    }

    ImGui::End();
}

} // namespace

void drawEditorUi(EditorState& state, bool& wantsExit) {
    drawToolbar(state);
    drawLevelPanel(state, wantsExit);
    drawInspector(state);
    drawWallList(state);
    drawFilesPanel(state);
}

} // namespace horde::editor
