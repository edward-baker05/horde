#pragma once

#include <SDL3/SDL_gpu.h>

#include <glm/glm.hpp>

#include "gfx/Texture.hpp"

struct SDL_Window;

namespace horde::scene {
struct Services;
}

namespace horde::gfx {

// Draws units straight out of a GPU storage buffer of BaseUnitData, one quad
// per unit, with no per-unit CPU work.
//
// Shared by every physics system so that they all pay the same rendering cost
// and a benchmark compares their simulation, not their drawing.
class UnitRenderer {
public:
    UnitRenderer() = default;
    ~UnitRenderer();

    UnitRenderer(const UnitRenderer&) = delete;
    UnitRenderer& operator=(const UnitRenderer&) = delete;

    bool init(scene::Services& services);
    void shutdown();

    // Which crowd the sprites draw: they share art roles but not palettes.
    enum class Species : Uint32 { Humans = 0, Aliens = 1 };

    // Sprite level of detail. Auto picks from how many screen pixels a unit
    // covers; the rest force one, for looking at the art.
    enum class Lod : int { Auto = -1, Near = 0, Mid = 1, Low = 2, Circle = 3 };

    // When Auto switches LOD: a unit must span at least `nearPixels` screen
    // pixels to draw near art, `midPixels` for mid and `lowPixels` for low,
    // else a plain circle. Leaving a
    // LOD takes `hysteresis` pixels fewer than entering it, so zooming across
    // a threshold doesn't flicker.
    struct LodThresholds {
        float nearPixels = 40.0f;
        float midPixels = 14.0f;
        float lowPixels = 8.0f; // low art is 5px across: below 5 it loses pixels
        float hysteresis = 0.0f;
    };

    // The level of detail the latest sprite draw used (never Auto).
    Lod lastLod() const {
        return m_lod;
    }

    // Screen pixels one unit spanned in the latest draw.
    float lastUnitPixels() const {
        return m_unitPixels;
    }

    // Whether the sprite atlas loaded; without it units draw as discs.
    bool hasSprites() const {
        return m_unitAtlas != nullptr;
    }

    // Per-unit extras a physics system can supply.
    //
    // Interpolation between two physics steps: units are drawn `alpha` of the
    // way from `previous` (one float2 per unit) to their current position;
    // moves longer than `maxJump` (a respawn) snap instead.
    //
    // `health` (one float of hit points per unit) tints each unit from the full
    // colour at `maxHealth` down to black at 0.
    //
    // `ids` (one uint per unit) is each unit's stable ID, for systems that
    // reorder their units; unit 0 by ID is drawn red so it can be followed.
    //
    // `slots` (one uint per unit, by ID) is where each unit is now, for systems
    // that reorder: units are then drawn in ID order, so the one on top of an
    // overlap stays on top however the units are reordered.
    //
    // `status` (one sim::StatusEffect per unit) tints stunned, slowed and
    // burning units; `time` drives the burning flicker.
    //
    // `color` is the full-health colour, and `markFirst` draws the unit with
    // ID 0 red. The defaults are the horde's; bullets use their own.
    struct DrawOptions {
        SDL_GPUBuffer* previous = nullptr; // null: draw current positions as they are
        float alpha = 1.0f;
        float maxJump = 0.0f;
        SDL_GPUBuffer* health = nullptr; // null: everyone at full health
        float maxHealth = 1.0f;
        SDL_GPUBuffer* ids = nullptr; // null: a unit's ID is its index
        SDL_GPUBuffer* slots = nullptr; // null: draw in index order

        // Crowd sprites instead of flat discs: each unit's look comes from
        // hashing its ID with `seed`, so `ids` should be set when units are
        // reordered. The level of detail follows the zoom unless `lod` forces
        // one. `integerScale` rounds sprite pixels to whole screen pixels.
        bool sprites = true;
        Species species = Species::Humans;
        Lod lod = Lod::Auto;
        LodThresholds thresholds;
        bool integerScale = false;
        // Turn near and mid sprites to face the way each unit walks, snapped
        // to `directionSteps` directions (0 = any angle). Units slower than
        // `minTurnSpeed` (world units per second) keep facing up.
        bool rotate = true;
        Uint32 directionSteps = 16;
        float minTurnSpeed = 1.0f;
        Uint32 seed = 0;

        SDL_GPUBuffer* status = nullptr; // null: no status tints
        float time = 0.0f;
        glm::vec4 color{0.0f, 1.0f, 0.2f, 1.0f};
        bool markFirst = true;
    };

    // `units` holds at least `count` BaseUnitData entries.
    void draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, SDL_GPUBuffer* units, Uint32 count,
              const glm::mat4& viewProjection, float unitSize, const DrawOptions& options) const;

    // Draws the units where they are, with no blending.
    void draw(SDL_GPUCommandBuffer* commands, SDL_GPURenderPass* pass, SDL_GPUBuffer* units, Uint32 count,
              const glm::mat4& viewProjection, float unitSize) const {
        draw(commands, pass, units, count, viewProjection, unitSize, DrawOptions{});
    }

private:
    // Picks the sprite LOD for a unit `unitPixels` across, with hysteresis
    // so zooming across a threshold doesn't flicker between two.
    Lod pickLod(float unitPixels, const LodThresholds& thresholds) const;

    SDL_GPUDevice* m_device = nullptr;
    SDL_Window* m_window = nullptr;
    SDL_GPUTexture* m_atlas = nullptr;
    SDL_GPUTexture* m_unitAtlas = nullptr; // owned by m_unitAtlasTexture
    Texture m_unitAtlasTexture;
    // Updated by draw(), which is otherwise const.
    mutable Lod m_lod = Lod::Near;
    mutable float m_unitPixels = 0.0f;
    SDL_GPUGraphicsPipeline* m_pipeline = nullptr;
    SDL_GPUSampler* m_sampler = nullptr;
};

} // namespace horde::gfx
