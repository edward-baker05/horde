#pragma once

#include <filesystem>
#include <string>

namespace horde::editor {

struct EditorState;

// What the editor's buttons and shortcuts do to its state, kept apart from
// how they're drawn. Each sets `state.status` when there is something to tell
// the user.

// Deletes whatever is selected, snapshotting first. Refuses, returning false,
// when the selection is the last spawn or the last exit: every level keeps at
// least one of each at all times.
bool deleteSelected(EditorState& state);

// Steps back or forward through the history. The selection is dropped, since
// what it pointed at may not exist on the other side.
bool undo(EditorState& state);
bool redo(EditorState& state);

// Replaces the level with the one at `path`, clearing the history. Leaves the
// state alone if it can't be read.
bool openLevel(EditorState& state, const std::filesystem::path& path);

// Starts over on the default level, clearing the history.
void newLevel(EditorState& state);

// Writes the level under `name` in the levels directory. Refuses a level that
// doesn't validate.
bool saveLevelAs(EditorState& state, const std::string& name);

} // namespace horde::editor
