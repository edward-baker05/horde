#include "editor/EditorUi.hpp"

#include <imgui.h>

#include <cstdio>
#include <iterator>

#include "editor/EditorState.hpp"
#include "editor/Tools.hpp"

namespace horde::editor {
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

} // namespace

void drawEditorUi(EditorState& state, bool& wantsExit) {
    drawToolbar(state);
    drawLevelPanel(state, wantsExit);
}

} // namespace horde::editor
