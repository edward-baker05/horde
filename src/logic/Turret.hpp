#pragma once

#include <glm/vec2.hpp>
#include <glm/vec4.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>

namespace horde::logic {

// What a weapon is themed as. Never changes how units react: every unit takes
// every category's damage and effects the same way. It picks a weapon's
// colour and art, and will group weapons into upgrade paths. See
// docs/superpowers/specs/2026-11-21-weapon-categories-design.md.
enum class WeaponCategory : std::uint8_t {
    Merch,    // merchandise that satisfies a fan's obsession
    Sound,    // smooth jazz, booming bass, shockwaves
    Chemical, // pepper spray, tear gas, chloroform
    Physical, // bouncers, barriers, water cannons
};

inline constexpr std::size_t kWeaponCategoryCount = 4;

const char* weaponCategoryName(WeaponCategory category);

// The category's colour, opaque. tools/make_weapon_atlas.py bakes the same
// colours into the placeholder sprites; keep the two in step.
glm::vec4 weaponCategoryColor(WeaponCategory category);

// The bit for `category` in TurretDef::secondaryCategories.
constexpr std::uint8_t categoryBit(WeaponCategory category) {
    return static_cast<std::uint8_t>(1u << static_cast<unsigned>(category));
}

// What a turret is. See docs/superpowers/specs/2026-11-03-turrets-design.md.
enum class TurretKind : std::uint8_t {
    Flamer,     // mount: a cone of continuous damage along its facing
    Zapper,     // mount: a damage pulse around it every so often
    Mortar,     // mount: lobs a shell at the thickest crowd in range
    Beam,       // mount: a piercing line that sweeps across an arc
    SpikeBlock, // blocker: hurts units touching it, and is worn down by them
    Barricade,  // blocker: tough, harmless, there to herd the horde
    Mine,       // floor trap: explodes the first time a unit comes near
    Gunner,     // mount: fires bullets at the nearest unit

    // Weapons, numbered as their placeholder sprites and in the weapon
    // categories spec, section 4.
    TShirtCannon, // 1 merch, mount: lobs a bundle of shirts at the thickest crowd
    MerchSlinger, // 2 merch, mount: signed photos and frisbees at the nearest fan
    MerchStand,   // 3 merch, blocker: hands merch to fans pressed against it
    JazzSpeaker,  // 4 sound, mount: slows everything around it
    BassStack,    // 5 sound, mount: a booming damage pulse
    FeedbackAmp,  // 6 sound, mount: a shockwave that stuns and shoves
    PepperSpray,  // 7 chemical, mount: a short cone that sets fans burning
    CrowdBarrier, // 11 physical, blocker: very tough, harmless
};

inline constexpr std::size_t kTurretKindCount = 16;

// How a turret reaches units, which decides how TurretEffects runs it.
enum class TurretDelivery : std::uint8_t {
    None,  // nothing of its own: a blocker, which only collides (and may hurt on contact)
    Cone,  // continuous, within range and the arc; an arc of a full turn is an aura
    Pulse, // every period, a burst over its range around it
    Shell, // every period, a blast at the thickest crowd in range, after a flight
    Sweep, // continuous along a line that sweeps back and forth across the arc
    Mine,  // a blast the first time a unit comes near, then spent
    Gun,   // every period, a bullet at the nearest unit in range
};

// Where a turret may go. Mounts sit on top of walls, out of the horde's
// reach; blockers and floor traps sit on open ground in its way.
enum class TurretClass : std::uint8_t {
    Mount,
    Blocker,   // solid: units collide with it, and it has hit points
    FloorTrap, // not solid: units walk over it
};

// One kind's fixed numbers. Distances are world units (a unit is 5 across),
// times seconds, damage hit points (a unit starts with 30 by default).
struct TurretDef {
    const char* name = "";
    TurretClass placement = TurretClass::Mount;
    TurretDelivery delivery = TurretDelivery::None;
    // Half the footprint's width and height, before rotating by the facing.
    glm::vec2 halfSize{6.0f, 6.0f};
    // Whether the player sets a facing. Omnidirectional turrets ignore it.
    bool directional = false;
    // How far it reaches: a cone's length, a pulse's or mortar's radius, a
    // beam's length, a mine's trigger radius.
    float range = 0.0f;
    // Full angle in radians: a flamer's cone, the arc a beam sweeps over.
    float arc = 0.0f;
    // Seconds between shots: a zapper's pulses, a mortar's shells, a gun's
    // bullets, a full back-and-forth of a beam's sweep.
    float period = 0.0f;
    // Hit points per second for continuous damage (flamer, beam, spike
    // contact); per shot for pulses, shells and mines, at the centre; per hit
    // for bullets.
    float damage = 0.0f;
    // A shell's or mine's blast radius, and a beam's width.
    float blastRadius = 0.0f;
    // How hard a pulse, shell or mine throws units, in world units per second
    // at the centre; 0 for damage alone.
    float blastPush = 0.0f;
    // Blockers' hit points; 0 for anything units can't hurt.
    float maxHp = 0.0f;
    // Status effects on the units it hits, each lasting this many seconds; 0
    // for none. Re-applying refreshes rather than stacks.
    float stunSeconds = 0.0f;
    float slowSeconds = 0.0f;
    float slowFactor = 1.0f; // speed multiplier while slowed, 0 to 1
    float burnSeconds = 0.0f;
    float burnDps = 0.0f; // hit points per second while burning
    // Guns: how fast their bullets fly, in world units per second, and how
    // many more units a bullet can hit after its first.
    float bulletSpeed = 0.0f;
    std::uint32_t pierce = 0;
    // Not charged yet: the economy is undecided.
    std::uint32_t cost = 0;
    // A short description for the palette's tooltip.
    const char* blurb = "";
    // The weapon category it is coloured and grouped by. None for the
    // original eight kinds, which predate categories.
    std::optional<WeaponCategory> category;
    // Categories its extra gear adds, as categoryBit()s: labels only, for
    // upgrade paths. A bouncer with pepper spray is Physical with Chemical here.
    std::uint8_t secondaryCategories = 0;
    // Its cell in textures/weapons.png, 1-based (weaponSpriteCell). 0: drawn as
    // a plain box in its placement class's colour, as the original kinds are.
    std::uint8_t spriteId = 0;
};

const TurretDef& turretDef(TurretKind kind);

// Short names for placement classes: "Mount", "Blocker", "Floor trap".
const char* turretClassName(TurretClass placement);

// The colour a placement class is drawn in, for kinds with no category.
glm::vec4 turretClassColor(TurretClass placement);

// What a kind is drawn in: its category's colour, else its class's.
glm::vec4 turretColor(const TurretDef& def);

// GPU shaders hold at most this many turrets; placement refuses any more.
inline constexpr std::size_t kMaxTurrets = 64;

// One placed turret.
struct Turret {
    TurretKind kind = TurretKind::Zapper;
    glm::vec2 center{0.0f, 0.0f};
    float facing = 0.0f; // radians; positive is clockwise on screen, 0 faces +x
    float hp = 0.0f;     // blockers only; starts at the kind's maxHp
};

// A turret of `kind` at full health.
Turret makeTurret(TurretKind kind, glm::vec2 center, float facing);

} // namespace horde::logic
