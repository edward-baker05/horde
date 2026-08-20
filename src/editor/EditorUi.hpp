#pragma once

namespace horde::editor {

struct EditorState;

// Draws every ImGui panel for the editor and applies whatever the user did to
// `state`. Sets `wantsExit` when the user asks to return to the menu.
void drawEditorUi(EditorState& state, bool& wantsExit);

// Deletes whatever is selected, snapshotting first. Refuses, returning false,
// when the selection is the last spawn or the last exit: every level keeps at
// least one of each at all times.
bool deleteSelected(EditorState& state);

} // namespace horde::editor
