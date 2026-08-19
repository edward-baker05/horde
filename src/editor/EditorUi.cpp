#include "editor/EditorUi.hpp"

#include <imgui.h>

#include "editor/EditorState.hpp"

namespace horde::editor {
namespace {

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
    drawLevelPanel(state, wantsExit);
}

} // namespace horde::editor
