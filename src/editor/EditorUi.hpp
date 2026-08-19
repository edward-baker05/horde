#pragma once

namespace horde::editor {

struct EditorState;

// Draws every ImGui panel for the editor and applies whatever the user did to
// `state`. Sets `wantsExit` when the user asks to return to the menu.
void drawEditorUi(EditorState& state, bool& wantsExit);

} // namespace horde::editor
