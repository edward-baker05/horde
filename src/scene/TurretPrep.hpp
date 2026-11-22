#pragma once

#include <SDL3/SDL_events.h>
#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>

#include <cstddef>
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

// The prep phase's turret placement: a palette to arm a kind from, a ghost
// that follows the cursor, and placing, moving, rotating and removing turrets,
// all undoable.
//
// Mouse and keys, while the cursor isn't over a panel:
//   left click        place the armed kind; or, with none armed, grab a turret to drag it
//   right click       disarm; or, with none armed, remove the turret under the cursor
//   Q / E             rotate the ghost, the dragged turret or the one under the cursor
//                     by 15 degrees (1 with Shift)
//   Delete            remove the turret under the cursor
//   Esc               disarm
//   Ctrl+Z / Ctrl+Y   undo / redo (Ctrl+Shift+Z redoes too)
// Anything not used here falls through to the camera, so the left button
// still pans when it isn't over a turret.
class TurretPrep {
public:
    // Both must outlive this.
    void init(const logic::Level& level, const sim::WallColliders& walls);

    // Returns true if it used the event.
    bool handleEvent(const SDL_Event& event, const gfx::Camera2D& camera);

    // Ends placement when the wave starts: disarms, so no ghost is left
    // behind, and drops any drag where it is.
    void finish();

    // Placed turrets, their reach, and the ghost.
    // `weapons` is the weapon sprite sheet, or null to draw every turret as a box.
    void render(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, SDL_GPUTexture* weapons) const;

    // The palette and hints, for the inside of the wave window.
    void ui();

    const std::vector<logic::Turret>& turrets() const {
        return m_turrets;
    }

    // Why `turret` can't go where it is, or null if it can. `ignore` is the
    // index of a placed turret to leave out of the overlap check: the one
    // being moved.
    const char* placementProblem(const logic::Turret& turret, std::optional<std::size_t> ignore) const;

private:
    // The topmost placed turret under `point`.
    std::optional<std::size_t> turretAt(glm::vec2 point) const;
    void rotate(float radians);
    void remove(std::size_t index);

    const logic::Level* m_level = nullptr;
    const sim::WallColliders* m_walls = nullptr;

    std::vector<logic::Turret> m_turrets;
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
};

// The turrets as the wave has them: beams where they point now, blockers
// darkening as they wear down, and spent mines and broken blockers gone.
void renderWaveTurrets(gfx::SpriteBatch& batch, SDL_GPUTexture* atlas, SDL_GPUTexture* weapons,
                       const sim::TurretEffects& effects);

} // namespace horde::scene
