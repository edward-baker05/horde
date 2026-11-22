#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <vector>

#include "editor/UndoStack.hpp"
#include "logic/Level.hpp"
#include "logic/Turret.hpp"

namespace horde::gfx {
class Camera2D;
class SpriteBatch;
} // namespace horde::gfx

namespace horde::sim {
class TurretEffects;
class WallColliders;
} // namespace horde::sim

namespace horde::scene {

// Turret placement in a round's prep phase: a ghost of the armed kind that
// follows the cursor, and placing, moving, rotating and removing turrets, all
// undoable. The round's tray arms a kind (arm), and the round pays for what is
// placed when it starts.
//
// Turrets placed since the last takePlaced() are pending: they can still be
// moved, turned and removed, and placing more is refused past the budget. The
// scene takes them all when the round starts, and from then on they are fixed.
// A newly placed turret grows in, purely as a look.
//
// Mouse and keys, while the cursor isn't over a panel:
//   left click        place the armed kind; or, with none armed, grab a pending turret (or its
//                     whole line) to drag it, or a line's end handle to stretch it
//   left drag         with a blocker or floor trap armed, lay a line of them end to end
//   G                 toggle snapping to the wall grid (on by default)
//   right click       disarm or cancel a line; or, with none armed, remove the pending turret
//                     (or line) under the cursor
//   Q / E             rotate the ghost, the dragged turret or the pending one (or line, about
//                     its middle) under the cursor:
//                     45 degrees (15 with Shift) while snapping, else 15 (1 with Shift)
//   Delete            remove the pending turret (or line) under the cursor
//
// A line is laid as one unit: dragging either end's handle stretches, shrinks
// or swings it about the other end, adding or dropping turrets to fill it.
//   Esc               cancel a line, then disarm
//   Ctrl+Z / Ctrl+Y   undo / redo among pending turrets (Ctrl+Shift+Z redoes too)
// Anything not used here falls through to the scene.
class TurretPrep {
public:
    // Both must outlive this.
    void init(const logic::Level& level, const sim::WallColliders& walls);

    // Returns true if it used the event.
    bool handleEvent(const SDL_Event& event, const gfx::Camera2D& camera);

    // Advances newly placed turrets growing in.
    void update(float dt);

    // Arms `kind` for placing, or disarms.
    void arm(std::optional<logic::TurretKind> kind);

    std::optional<logic::TurretKind> armed() const {
        return m_armed;
    }

    // What pending turrets may cost in all; placing past it is refused. None
    // for no limit.
    void setBudget(std::optional<int> budget) {
        m_budget = budget;
    }

    // What the pending turrets cost.
    int pendingCost() const;

    // The pending turrets, which are fixed from now on: they can no longer be
    // moved or removed, and undo stops short of them. Disarms if
    // `disarm` and drops any drag where it began.
    std::vector<logic::Turret> takePlaced(bool disarm);

    // A placed turret has gone from play (a spent mine, a broken blocker): its
    // ground is free again, though its slot stays used.
    void markGone(std::size_t index);

    // Pending turrets, their reach, and the ghost; fixed ones are drawn from
    // the simulation (renderWaveTurrets).
    // `weapons` is the weapon sprite sheet, or null to draw every turret as a box.
    void render(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, SDL_GPUTexture* weapons) const;

    // Every turret placed, pending or fixed, in slot order: the same order
    // the simulation holds them in.
    const std::vector<logic::Turret>& turrets() const {
        return m_turrets;
    }

    // Why the armed kind can't go where the cursor is, or null if it can or
    // nothing is armed.
    const char* ghostProblem() const;

    // Why `turret` can't go where it is, or null if it can. `ignore` is the
    // index of a placed turret to leave out of the overlap check: the one
    // being moved.
    const char* placementProblem(const logic::Turret& turret, std::optional<std::size_t> ignore) const;

    // The topmost placed turret still in play under `point`.
    std::optional<std::size_t> turretAt(glm::vec2 point) const;

    // Whether markGone() has taken placed turret `index` out of play.
    bool gone(std::size_t index) const {
        return index < m_gone.size() && m_gone[index];
    }

private:
    // Where a turret of `kind` facing `facing` goes for `point`: with snapping
    // on, moved so its footprint's edges lie on the wall grid's cell lines.
    glm::vec2 snapCenter(logic::TurretKind kind, glm::vec2 point, float facing) const;
    // The armed kind as it would be placed at the cursor.
    logic::Turret ghost() const;
    // The turrets a line drag from m_lineStart to the cursor would place,
    // end to end; the first is where the drag began.
    std::vector<logic::Turret> lineTurrets() const;
    // `count` turrets of `kind` end to end from `first` (snapped) along `dir`
    // (unit length). Directional kinds face so their long side runs along the
    // line; the rest take `facing`.
    std::vector<logic::Turret> layLine(logic::TurretKind kind, glm::vec2 first, glm::vec2 dir, std::size_t count,
                                       float facing) const;
    // How far apart layLine puts turrets of `kind` along `dir`.
    float lineSpacing(logic::TurretKind kind, glm::vec2 dir) const;

    // A pending line's ends: its first and last turrets, the direction from
    // first to last, and the outer tips where its handles sit.
    struct LineEnds {
        std::size_t first = 0;
        std::size_t last = 0;
        std::size_t count = 0;
        glm::vec2 dir{1.0f, 0.0f};
        glm::vec2 startTip{0.0f};
        glm::vec2 endTip{0.0f};
    };
    std::optional<LineEnds> lineEnds(std::uint32_t line) const;
    // The pending lines' ids, each once.
    std::vector<std::uint32_t> pendingLines() const;

    // Why `turret` can't stand where it is, leaving out turret `ignore` and
    // every turret of line `ignoreLine` (0 for none); limits aren't checked.
    const char* siteProblem(const logic::Turret& turret, std::optional<std::size_t> ignore,
                            std::uint32_t ignoreLine) const;
    // Why `members` can't replace line `line`'s turrets, or null if they can.
    const char* replaceProblem(const std::vector<logic::Turret>& members, std::uint32_t line) const;
    // Why the drag in progress would snap back, or null.
    const char* dragProblem() const;
    // Puts `members` in place of line `line`'s turrets, as one undo step.
    void replaceLine(std::uint32_t line, std::vector<logic::Turret> members);
    // The stretch in progress, laid out from its anchor to the cursor.
    std::vector<logic::Turret> stretchTurrets() const;

    // The topmost pending turret under `point`.
    std::optional<std::size_t> pendingAt(glm::vec2 point) const;
    void rotate(float radians);
    // Removes the turret, or the whole line it belongs to.
    void remove(std::size_t index);

    const logic::Level* m_level = nullptr;
    const sim::WallColliders* m_walls = nullptr;

    std::vector<logic::Turret> m_turrets;
    // By index: how far each has grown in, 0 to 1.
    std::vector<float> m_grow;
    // Turrets before this index are fixed; the rest are pending.
    std::size_t m_fixed = 0;
    // By index: gone from play, so neither blocking nor pickable.
    std::vector<bool> m_gone;
    std::optional<int> m_budget;
    editor::SnapshotStack<std::vector<logic::Turret>> m_undo;

    std::optional<logic::TurretKind> m_armed;
    float m_ghostFacing = 0.0f;
    glm::vec2 m_cursor{0.0f, 0.0f};
    bool m_cursorInWorld = false; // false while the cursor is over a panel

    // A drag in progress: which turret, where the grab was relative to its
    // centre, and every turret as it was before, to undo to or snap back to.
    std::optional<std::size_t> m_dragging;
    glm::vec2 m_dragOffset{0.0f, 0.0f};
    std::vector<logic::Turret> m_beforeDrag;

    // Snap placement and moves to the wall grid, and rotation to 45 degrees.
    bool m_snap = true;

    // A line of ground turrets being dragged out: where it began (already
    // snapped). Placed on release.
    std::optional<glm::vec2> m_lineStart;
    // The id the next line laid takes; never reused, so undo can't mix lines.
    std::uint32_t m_nextLine = 1;

    // A line's end being dragged: the line, the turret at the other end that
    // stays put, and where the grab was relative to the dragged end turret.
    struct Stretch {
        std::uint32_t line = 0;
        logic::TurretKind kind{};
        glm::vec2 anchor{0.0f};
        float facing = 0.0f;
        glm::vec2 offset{0.0f};
    };
    std::optional<Stretch> m_stretch;

    // Line handles' radius in world units: a fixed size on screen.
    float m_handleRadius = 6.0f;
};

// What `turret` reaches, faintly in `color`: its cone, beam, pulse or blast.
void renderTurretReach(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, const logic::Turret& turret, glm::vec4 color);

// The turrets as the wave has them: beams where they point now, blockers
// darkening as they wear down, and spent mines and broken blockers gone.
void renderWaveTurrets(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, SDL_GPUTexture* weapons,
                       const sim::TurretEffects& effects);

} // namespace horde::scene
