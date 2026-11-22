# Weapon categories — design

**Date:** 2026-11-21
**Status:** planned, not implemented
**Branch:** `bailen-weapon-categories`

The horde is now a crowd of obsessed fans, and the turrets are how the star's
people deal with them. Each turret belongs to one of four **weapon
categories** that fit the theme:

- **Merch:** hands fans merchandise to satisfy their obsession.
- **Sound:** sound waves. Smooth jazz calms and slows, booming bass hurts a
  whole area, and sudden shockwaves stun.
- **Chemical:** pepper spray and tear gas do damage over time across an area,
  and chloroform puts fans to sleep.
- **Physical:** bouncers shove fans back, and riot water cannons push them.

Categories **never change how play works**. Every unit reacts to every
category the same way, and all damage takes from the same hit points. A
category is for two things only:

1. **Visual distinction:** colour, and later the art style of a turret.
2. **Upgrade paths:** grouping turrets for a future upgrade or tech tree.

This builds on the turret, status-effect and bullet work
(`2026-11-03-turrets-design.md` and `2026-11-04-unit-effects-and-bullets-design.md`).
The existing eight turret kinds are left alone. Every weapon here is a new
kind.

---

## Scope

**In:** the category tag; three new delivery mechanics (section 3) and sleep
(section 2); the twelve single-category weapons and two combined ones in
section 4; placeholder sprites (section 5); a palette grouped by category.

**Out, deliberately:**
- **Upgrade paths.** The category is recorded so they can be built later.
- **Resistances or weaknesses by category.** These never happen; see above.
- **Equipping gear at runtime.** A bouncer with pepper spray is its own turret
  kind, not a bouncer with a part attached (section 1).
- **A separate "satisfied" state for merch.** Merch is ordinary damage. A unit
  that reaches 0 hit points disappears, the same as any kill.
- **Final art.** It comes later; the placeholders are built to be swapped out.
- **Economy.** It is still undecided, as in the turret spec.

---

## 1. Weapon = turret

"Weapon" and "turret" mean the same thing. Each different set of gear is a
separate turret kind with its own entry in the definition table:

- **Bouncer:** Physical.
- **Bouncer with pepper spray:** Physical primary, Chemical secondary.
- **T-shirt cannon:** Merch.
- **Chloroform T-shirt cannon:** Merch primary, Chemical secondary.

Every turret has exactly one **primary category**, which decides its colour
and where it sits in the palette. It may also list **secondary categories**
for what its extra gear adds. Secondary categories are labels only, kept for
future upgrade paths.

## 2. Sleep

Sleep is a status effect: **2 s of stun, then 1 s slowed to 50 %**.

It needs no new GPU state. A sleep hit applies two existing effects at once:

- stun for 2 s;
- slow for 3 s at a factor of 0.5.

A stunned unit already has zero desired velocity, so the slow only shows once
the stun ends, leaving exactly 1 s of half speed. Re-applying it follows the
existing rules: the longer duration and the stronger slow win.

`TurretDef` gains `sleepSeconds` (2) and `wakeSlowSeconds` (1). When the
status table is built, these expand into stun and slow values. A unit asleep
looks the same as a stunned one; giving it its own tint is optional, using
the spare float in `UnitStatus`.

## 3. New delivery mechanics

The existing deliveries are cone (Flamer), pulse (Zapper), lobbed shell
(Mortar), sweeping beam (Beam), contact (Spike block), mine and bullet
(Gunner). Three more are needed.

### 3.1 Aura

Continuous effects on every unit within a radius, such as the jazz speaker's
slow. This is a cone with a full 360° arc. `unit_integrate` already handles
cones, so a cosine half-angle of −1 covers it and **no shader change is
needed**.

### 3.2 Push zone

A continuous shove along a direction for every unit inside a cone or a box.
Bouncers push the fans in front of them back; the water cannon pushes along
its facing.

- **New shape:** `TurretShape::Push`.
- **Per-turret data:** the cone or box as now, plus a push strength in world
  units per second. Damage is 0 unless the weapon also sets it.
- **In `unit_integrate`:** the push adds to the unit's velocity like the fling
  term does. It is not part of its desired velocity, so stunned units are
  still moved, which matches how explosions behave.
- **A bouncer** is a blocker (solid, has hit points, worn down by units) with
  a push zone in a short box in front of its facing. It is the first
  directional blocker.

### 3.3 Lingering cloud

A timed area that keeps applying a payload for a while: tear gas, and
chloroform gas if wanted later.

- **On the CPU:** `TurretEffects` keeps a list of active clouds, each with a
  centre, radius, seconds left, damage per second and a status index. A cloud
  is created where a lobbed canister lands, using the Mortar's
  aim-then-fly-then-land pipeline. On landing it becomes a cloud instead of
  an explosion.
- **On the GPU:** clouds are written into the same per-step `Turrets` array as
  aura-shaped entries, without a turret slot (they use the slot of the turret
  that fired them for the status lookup). This caps active clouds at
  `kMaxTurrets` minus the turrets in use. If that proves too low, move clouds
  into a block of their own, as explosions have.
- **Drawn** as a translucent disc in the category colour that fades over its
  last second.

## 4. Starting roster

The numbers are the IDs printed on the placeholder sprites (section 5). The
numbers below are starting values to tune in play. A unit has 30 hit points
and is 5 world units across.

### Merch (primary colour: magenta)

| # | Weapon | Class | Delivery | Effect |
|---|---|---|---|---|
| 1 | **T-shirt cannon** | mount | lobbed shell | Fires a bundle of shirts at the thickest crowd. Area damage, no fling. |
| 2 | **Merch slinger** | mount | bullet | Signed photos and vinyl frisbees at the nearest fan, pierce 2. |
| 3 | **Merch stand** | blocker | contact | Fans pressed against it are handed merch (contact damage). Lower hit points than a spike block. |

### Sound (primary colour: blue)

| # | Weapon | Class | Delivery | Effect |
|---|---|---|---|---|
| 4 | **Smooth jazz speaker** | mount | aura | Slows everything in range to 50 % continuously. No damage. |
| 5 | **Bass stack** | mount | pulse | Booming bass. Area damage every couple of seconds. |
| 6 | **Feedback amp** | mount | pulse | A sudden shockwave. Small damage, a 1 s stun and a small shove. |

### Chemical (primary colour: green)

| # | Weapon | Class | Delivery | Effect |
|---|---|---|---|---|
| 7 | **Pepper spray** | mount, directional | cone | Short cone that sets burning (damage over time). |
| 8 | **Tear gas launcher** | mount | lobbed → cloud | Canister at the thickest crowd. The cloud lingers about 4 s with damage over time and a mild slow. |
| 9 | **Chloroform mister** | mount, directional | cone, in puffs | A short cone every few seconds that puts fans to sleep. |

### Physical (primary colour: orange)

| # | Weapon | Class | Delivery | Effect |
|---|---|---|---|---|
| 10 | **Bouncer** | blocker, directional | contact + push zone | Tough. Shoves the fans in front of it back. |
| 11 | **Crowd barrier** | blocker | — | Very tough and harmless, for herding fans. |
| 12 | **Riot water cannon** | mount, directional | cone push | Pushes fans along its facing and slows them slightly. Tiny damage. |

### Combined (primary colour, plus secondary)

| # | Weapon | Primary + secondary | Effect |
|---|---|---|---|
| 13 | **Bouncer with pepper spray** | Physical + Chemical | The Bouncer, plus a short pepper cone in front that sets burning. |
| 14 | **Chloroform T-shirt cannon** | Merch + Chemical | The T-shirt cannon, whose blast also puts fans to sleep. |

The combined weapons are there to show the definition table handles more than
one category, not as a start on upgrades.

## 5. Placeholder sprites

Until the real art exists, each weapon is drawn as a **square in its primary
category's colour with its number (section 4) in the middle**.

- **The script:** `tools/make_weapon_atlas.py` writes
  `assets/textures/weapons.png`. It is pure Python with no PIL, in the same
  style as `make_placeholder_atlas.py`.
- **Layout:** 64×64 cells in a 4×4 grid, 16 slots, with weapon `n` in cell
  `n − 1`.
- **Each cell:** a filled square in the category colour with a 4 px darker
  border, and the number in white in a blocky pixel digit font (a 3×5 glyph
  scaled up). Edges are hard, with no anti-aliasing.
- **Combined weapons** get a stripe of their secondary colour along the bottom
  edge, so they can be told apart at a glance.
- **Swapping in art:** the code draws `atlasCell(n − 1 mod 4, n − 1 div 4,
  4, 4)` from this texture. Real art replaces cells in the PNG and the code
  doesn't change. If there are ever more than 16 weapons, the grid grows to
  8×8.
- **The colours are baked into the PNG** and drawn with a white tint. The
  existing darkening of worn-down blockers still works, because it multiplies
  over the top.

Category colours live in one table (`weaponCategoryColor`) so the palette UI,
the clouds and the script all agree. The script repeats the table and says so
in a comment.

## 6. Data model

```cpp
enum class WeaponCategory : std::uint8_t { Merch, Sound, Chemical, Physical };
inline constexpr std::size_t kWeaponCategoryCount = 4;

const char* weaponCategoryName(WeaponCategory);
glm::vec4   weaponCategoryColor(WeaponCategory);

// Added to TurretDef:
WeaponCategory category = WeaponCategory::Physical; // primary
std::uint8_t   secondaryCategories = 0;             // bitmask, 1 << category
std::uint8_t   spriteId = 0;                        // 1-based, 0 = the old shared-atlas drawing
float sleepSeconds = 0.0f;      // section 2
float wakeSlowSeconds = 0.0f;
float pushStrength = 0.0f;      // world units per second, section 3.2
float cloudSeconds = 0.0f;      // section 3.3; 0 = lands as an explosion
```

The existing eight kinds get no category and keep `spriteId = 0`, so nothing
about them changes. The 14 new `TurretKind` values are appended after
`Gunner`, and `kTurretKindCount` rises to 22.

The prep palette groups kinds under a coloured header for each category, with
the existing turrets under "Other". That needs an optional category, or a
fifth "Unsorted" value used only by the old kinds. That choice can be made
when implementing.

## 7. Build order

Each step builds and runs on its own.

1. **Data and placeholders:** the category enum and colours, the new
   `TurretDef` fields, the weapon atlas script and texture, and drawing by
   `spriteId`.
2. **Simple weapons:** the ones that only need existing deliveries
   (1, 2, 3, 5, 6, 7, 11), plus the jazz speaker's aura (4).
3. **Sleep:** expanding it into the status table (9, 14).
4. **Push zone:** the shader shape, then the water cannon (12), then the
   directional blocker for the Bouncer (10, 13).
5. **Lingering cloud** (8).
6. **The palette grouped by category.**

## 8. Vocabulary

To add to `CONTEXT.md` once the turret vocabulary is on this branch:

**Weapon**: Another name for a turret, used when talking about what it does to
the horde.

**Weapon category**: What a turret is themed as: merch, sound, chemical or
physical. Changes only how it looks and its upgrade path, never how units
react. _Avoid_: damage type, element

**Primary category**: The one category a turret is coloured and grouped by.

**Secondary category**: A category a turret's extra gear adds. A label only.

**Sleep**: A status effect: a stun, followed by a short slow once it wears
off. _Avoid_: knockout, faint

**Push zone**: An area in which units are shoved continuously along one
direction. _Avoid_: knockback, wind

**Cloud**: A lingering area that keeps applying its effects to units inside it
until it fades. _Avoid_: gas, hazard zone

## Open questions

- **Where the turret code came from.** The turret, status-effect, bullet and
  wave-mode work was copied onto this branch from the uncommitted work in the
  `horde` checkout (`Bailen-gpu-shader-test`), then merged by hand with the
  sprite system on `main`. Status tints now apply to crowd sprites as well as
  discs, and bullets are always drawn as discs. If that work is later
  committed in `horde` as well, expect conflicts when the two branches meet.
- **A tint for sleeping units?** Optional; see section 2.
- **The palette for the old turrets:** an "Other" group, or a fifth category
  value? See section 6.
