#pragma once

#include <cstddef>
#include <filesystem>
#include <string>

#include "editor/UndoStack.hpp"
#include "logic/Level.hpp"

namespace horde::editor {

// Which tool the left mouse button is currently driving.
enum class Tool { Select, Rectangle, Triangle, Circle, Polyline, Spawn, Exit };

enum class SelectionKind { None, Wall, Marker };

// What is selected or hovered. `index` indexes Level::walls or Level::markers
// depending on `kind`, and is meaningless when kind is None.
struct Selection {
    SelectionKind kind = SelectionKind::None;
    std::size_t index = 0;

    bool isWall() const {
        return kind == SelectionKind::Wall;
    }

    bool isMarker() const {
        return kind == SelectionKind::Marker;
    }

    void clear() {
        kind = SelectionKind::None;
        index = 0;
    }

    bool operator==(const Selection& other) const {
        return kind == other.kind && (kind == SelectionKind::None || index == other.index);
    }
};

// Both toggles default off: off-grid, off-axis placement is the norm and
// snapping is the opt-in aid.
struct SnapSettings {
    bool position = false;
    bool rotation = false;
    float gridSize = 10.0f;
    float rotationDegrees = 15.0f;
};

// Everything the editor is currently doing. Passed by reference to the UI, the
// tools and the handles, so none of them own any of it.
struct EditorState {
    logic::Level level = logic::makeDefaultLevel();

    Selection selection;
    Selection hovered;

    Tool tool = Tool::Select;
    SnapSettings snap;

    // Colour applied to the next wall placed.
    logic::Rgb newWallColor{180, 180, 190};

    // Thickness applied to the next polyline drawn.
    float newPolylineThickness = 6.0f;

    // Empty until the level has been saved or loaded from somewhere.
    std::filesystem::path path;
    bool dirty = false;

    // Last save or load outcome, shown in the UI. Empty when there is nothing
    // to report.
    std::string status;

    UndoStack undo;

    // Snapshots the level so the next mutation can be undone. Call immediately
    // BEFORE mutating, once per user-visible operation — at the start of a drag,
    // not once per frame of it.
    void beginMutation() {
        undo.push(level);
        dirty = true;
    }
};

} // namespace horde::editor
