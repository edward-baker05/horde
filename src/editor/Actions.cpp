#include "editor/Actions.hpp"

#include <cstddef>
#include <optional>
#include <utility>

#include "core/PolishMode.hpp"
#include "editor/EditorState.hpp"
#include "editor/Markers.hpp"
#include "logic/LevelFiles.hpp"
#include "logic/LevelIO.hpp"
#include "logic/LevelValidation.hpp"

namespace horde::editor {

namespace {

// After the level is swapped wholesale: nothing old is selected, hovered or
// undoable, and what's open is what's on disk.
void resetForLevel(EditorState& state) {
    state.selection.clear();
    state.hovered.clear();
    state.undo.clear();
    state.dirty = false;
}

} // namespace

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
            state.status = tr("Every level needs at least one spawn and one exit.");
            return false;
        }
        state.beginMutation();
        state.level.markers.erase(state.level.markers.begin() + static_cast<std::ptrdiff_t>(state.selection.index));
        state.selection.clear();
        return true;
    }

    if (state.selection.isFieldRegion() && state.selection.index < state.level.fieldRegions.size()) {
        state.beginMutation();
        state.level.fieldRegions.erase(state.level.fieldRegions.begin() +
                                       static_cast<std::ptrdiff_t>(state.selection.index));
        state.selection.clear();
        return true;
    }

    return false;
}

bool undo(EditorState& state) {
    if (!state.undo.undo(state.level)) {
        return false;
    }
    state.selection.clear();
    state.dirty = true;
    return true;
}

bool redo(EditorState& state) {
    if (!state.undo.redo(state.level)) {
        return false;
    }
    state.selection.clear();
    state.dirty = true;
    return true;
}

bool openLevel(EditorState& state, const std::filesystem::path& path) {
    const std::string name = logic::levelDisplayName(path);
    std::string error;
    std::optional<logic::Level> loaded = logic::loadLevel(path, &error);
    if (!loaded) {
        state.status = tr("Could not open ") + name + ": " + error;
        return false;
    }
    state.level = std::move(*loaded);
    state.path = path;
    resetForLevel(state);
    state.status = tr("Opened ") + name;
    return true;
}

void newLevel(EditorState& state) {
    state.level = logic::makeDefaultLevel();
    state.path.clear();
    resetForLevel(state);
    state.status = tr("New level");
}

bool saveLevelAs(EditorState& state, const std::string& name) {
    if (!logic::validate(state.level).empty()) {
        state.status = tr("Fix the problems before saving.");
        return false;
    }
    const std::filesystem::path path = logic::levelPathForName(name);
    std::string error;
    if (!logic::saveLevel(state.level, path, &error)) {
        state.status = tr("Could not save: ") + error;
        return false;
    }
    state.path = path;
    state.dirty = false;
    state.status = tr("Saved ") + logic::levelDisplayName(path);
    return true;
}

} // namespace horde::editor
