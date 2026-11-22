# Unit effects and bullets — design

**Date:** 2026-11-04
**Status:** implemented (untested in play)

Two additions that the modular turret work will build on:

- **Status effects** on units: **stun**, **slow** and **burning**, each with a
  timer, applied by turrets and explosions.
- **Bullets**: real projectiles on the GPU that travel, stop at walls and
  blockers, hit units (piercing some), and are drawn moving.

Both stay entirely on the GPU per unit. The CPU never reads unit positions back.

Vocabulary is defined in `CONTEXT.md`; this adds **status effect**, **stun**,
**slow**, **burning**, **bullet** and **pierce** (section 7).

---

## Scope

**In:** three status effects with per-source strengths; Flamer setting units
on fire and Zapper stunning them; tints for affected units; a bullet pool with
piercing; GPU target finding for guns; a new Gunner mount that fires bullets.

**Out, deliberately:**
- **Fire spreading** between units.
- **Stacking:** re-applying an effect never adds to it (section 1).
- **Bullets for existing weapons**, such as a visible mortar shell.
- **Tracer drawing:** bullets are small discs for now.
- **Status immunity** or resistances.

---

## 1. Status effects

Every unit has a `UnitStatus`, six floats in a per-unit buffer like `Health`.
It is reordered with the other per-unit buffers, and is zero for every unit at
the start, including those still to spawn (as `Fling` is).

| Field | Meaning |
|---|---|
| stun | seconds of stun left |
| slow | seconds of slow left |
| slowFactor | speed multiplier while slowed, 0 to 1 |
| burn | seconds of burning left |
| burnDps | hit points lost per second while burning |
| (spare) | |

**What each does, in `unit_integrate`:**
- **Stun:** the unit stops trying to walk. Its desired velocity is 0 and it
  slows to a stop at its usual steering rate. The crowd, explosions and flings
  can still move it.
- **Slow:** its desired velocity is multiplied by `slowFactor`.
- **Burning:** it loses `burnDps × Dt` each step. Deaths count as kills as
  usual.
- Every timer counts down by `Dt`.

**Re-applying:** stun, slow and burn durations become whichever lasts longer,
the old or the new. Slow uses the stronger factor (the lower one) and burn the
higher damage per second. Nothing stacks.

## 2. Where effects come from

Effect strengths come from turret definitions. They are uploaded once per wave
as a **status table**: a small read-only storage buffer with one entry per
turret slot plus entry 0, which means "no effect". A source carries only its
index into the table:
- **Explosion:** `Explosion::status`, packed into the explosion's spare uniform
  float.
- **Turret effect** (cone, beam, mine): packed with the shape into the
  effect's last float, as `shape | slot << 4`.
- **Bullet:** in its per-bullet data.

New `TurretDef` fields: `stunSeconds`, `slowSeconds`, `slowFactor`,
`burnSeconds`, `burnDps`. First values:
- Flamer: sets units burning.
- Zapper: stuns briefly.
- Everything else: no effects.

## 3. Drawing affected units

`unit.vert` binds the status buffer and tints each unit on top of the health
fade:
- burning: orange, flickering per unit
- stunned: pale blue
- slowed: violet

It needs the time for the flicker, which goes in the vertex uniforms.

## 4. Bullets

A fixed pool of `kMaxBullets = 8192`:
- **Bullets**: `BaseUnitData` (top-left position, velocity), so
  `UnitRenderer` draws the pool as it draws units. A spent bullet sits at
  `DEAD_POSITION`, which drawing already skips.
- **BulletPrev**: last step's positions, so drawing blends between steps.
- **BulletParams**: damage, life left, hits it can still pierce, status
  index, the ID of the last unit it hit, radius.

**`bullet_step.comp`** runs once per step, after the grid build and reorder.
Each live bullet:
- moves in sub-steps of at most a unit radius, so it can't skip past a unit
  or through a thin wall;
- is spent on reaching a wall (blockers included), leaving the level, or
  running out of life;
- checks the 3×3 grid cells around it at each sub-step. On touching a unit
  other than the last one it hit, it deals its damage, applies its status, and
  is spent unless it has pierce left (which then drops by one).

**Pending damage:** bullets don't write `Health`, because two bullets can hit
one unit in the same step. They atomically add thousandths of a hit point to a
per-unit `PendingDamage` counter. `unit_finalize` applies and clears it in the
same step. Nothing reorders units between the two passes, so the counter needs
no reordering and is always zero across a reorder.

**Status from bullets** is written straight into `UnitStatus`. Two bullets
setting the same unit's status in one step race, but either result is fine.

## 5. Firing

Guns fire bullets at the nearest unit, aimed on the GPU, with no readback:
- The CPU runs each gun's cooldown. When guns fire, it gives each shot the
  next slot of the pool, cycling around it, so no GPU atomics are needed.
- **`turret_fire.comp`**, one thread per shot, runs after the reorder and
  before `bullet_step`. It searches the grid outward in rings from the gun
  for the nearest unit within range, leads it by its velocity over the
  bullet's flight time, adds a small spread, and writes the bullet into its
  slot. With nobody in range it writes a spent bullet.

**Gunner** (mount, directionless): range 250, ten shots a second, 6 damage, a
bullet speed of 500 world units per second, no pierce. `TurretDef` gains
`bulletSpeed` and `pierce`.

## 6. Testing

- The sandbox benchmark (no turrets) stays as it was: no units in walls, and
  about the same GPU time.
- By hand: Gunner bullets travel and hit, piercing bullets carry on, a Flamer
  sets units burning, a Zapper stuns, and tints show.
- The debug panel shows live bullets.

## 7. Glossary additions (`CONTEXT.md`)

- **Status effect**: a timed condition on a unit, set by a weapon. Stun, slow
  and burning for now.
- **Stun**: a status effect under which a unit stops walking, though it can
  still be pushed.
- **Slow**: a status effect under which a unit walks at a fraction of its
  speed.
- **Burning**: a status effect under which a unit loses hit points over time.
- **Bullet**: a projectile a weapon fires that travels across the level and
  hits units.
- **Pierce**: how many more units a bullet can hit after its first.
