# Turrets — design

**Date:** 2026-11-03
**Status:** implemented (untested in play)

Before a level's wave starts, the player gets a prep phase to place turrets.
Most turrets sit on top of walls, out of the horde's reach, and damage units
passing by. A few are placed on open ground, in the horde's way: units collide
with them, they can hurt units on contact, and the crowd wears them down.

Units live on the GPU, up to around a million at once, so turrets are mostly
area effects. Each unit tests itself against the turrets near it, the way it
already tests explosions and field regions. Nothing on the CPU picks single
targets: one unit at a time is meaningless against a horde, and it would mean
reading unit positions back every frame.

Vocabulary is defined in `CONTEXT.md`; this adds **turret**, **mount**,
**blocker**, **prep phase**, **wave** and **leak** to it (section 9).

---

## Scope

**In:** a prep phase followed by one long wave; seven turret kinds (section
3); placing, rotating and removing turrets during prep; per-unit turret damage
on the GPU; blockers as solid, destructible colliders; units despawning at
exits and counting as leaks; at most 64 turrets per level.

**Out, deliberately:**
- **Economy.** Undecided. Each kind keeps a cost field, but nothing charges it
  yet, and placement is limited only by the turret cap.
- **Damage to mounts.** Units can't reach them.
- **Multiple waves**, repairs and carrying damage between waves.
- **Edge-mounted turrets and modular turrets** (see Future work).
- Saving turrets into level files: they belong to a play session.

---

## 1. Phases

1. **Prep:** the level is loaded and no units spawn. The player chooses a
   turret kind, sees a ghost preview with its range, rotates it if it has a
   facing, and places it. Placed turrets can be moved or removed freely, and
   every place, move, rotate and remove can be undone and redone (section
   1.1). A start button begins the wave.
2. **Wave:** spawns stream in at `spawnRate`. Turrets are locked in place.
   Spawning stops when the wave's length is reached (section 1.2). The wave
   ends once no units remain, and the result screen shows kills and leaks.

`LevelScene` keeps its current sandbox behaviour (respawn at exit, the debug
explosion tool) when it isn't in this mode, so the benchmark and presets don't
change.

### 1.1 Undo in prep

`editor::UndoStack` already snapshots whole values rather than recording
commands, and a turret list (64 small structs at most) is as cheap to copy as
a level. It's currently hard-wired to `logic::Level`. Make it a template on the
snapshot type, so the editor keeps `UndoStack<Level>` and prep uses
`UndoStack<std::vector<Turret>>`. As in the editor, push a snapshot before each
change, and drag a whole move as one step. The stack is cleared when the wave
starts.

### 1.2 Wave length

The wave length is set either by **unit count** (spawning stops after N units
have entered) or by **time** (spawning stops after T seconds). A setting
switches between the two. Either way, the wave ends only once the last unit has
died or leaked.

Wave settings belong to the level: `logic::WaveSettings` on `Level` (length,
unit count, duration, spawn rate), saved under an optional `"wave"` key. The
default is 10,000 units at 1000 units/s, which levels saved before waves
existed also get. A timed wave spawns `ceil(spawnRate × duration)` units, which
sizes the GPU buffers up front just as a unit count does.

**Entering wave mode:** the main menu's Play and the editor's test play start
in wave mode (`LevelMode::Wave`). Presets and the benchmark stay in sandbox
mode, and a radio button in the debug panel switches between them on restart.

## 2. Placement classes

**Mount:** placed anywhere on top of a wall, whether a placed wall or a solid
painted cell. Its whole footprint must lie over wall. Units can't reach it, so
it has no HP. Omnidirectional kinds ignore facing. Directional kinds (Flamer,
Beam) take a facing that the player rotates freely during placement, snapping
when grid snapping is on.

**Blocker:** placed on open background, in the horde's path. Its footprint must
be fully inside the level, and it can't overlap walls, markers, field regions
or other turrets. It also needs a clearance band in front of spawns, so units
don't enter inside it. Blockers are solid and have HP.

**Floor trap:** placed like a blocker, but not solid. The Mine is the only one
for now.

Sealing every route from spawn to exit is allowed. Blockers break, so a full
seal is a costly tactic rather than an exploit.

## 3. Turret kinds

| Kind | Class | Effect |
|---|---|---|
| **Flamer** | mount, directional | Cone along its facing. Continuous damage per second to every unit inside it, with no push. |
| **Zapper** | mount | Damage pulse over a radius every N seconds, with no push. |
| **Mortar** | mount | Every N seconds, lobs a shell at the densest grid cell within range, which explodes after a short flight time. |
| **Beam** | mount, directional | A line that sweeps back and forth across an arc centred on its facing. It pierces, damaging every unit it touches. |
| **Spike block** | blocker | Solid box. It damages units in contact and takes damage from them. |
| **Barricade** | blocker | Solid, high HP, no damage. It bunches and redirects the horde into other turrets. |
| **Mine** | floor trap | The first time a unit comes within its trigger radius, it explodes with a fling, then it's removed. |
| **Gunner** | mount | Fires bullets at the nearest unit in range. See the unit effects and bullets spec. |

Numbers (range, rate, damage, HP, footprint, cost) live in one definition table
in `logic/Turret.hpp` and are tuned by hand. A debug window edits them live.

## 4. Data model

```
TurretKind    enum of the seven kinds
TurretClass   Mount | Blocker | FloorTrap
TurretDef     kind, class, footprint, range, period, damage, maxHp, cost,
              directional, arc
Turret        kind, position, facing (radians), hp (blockers), cooldown
```

The CPU owns the list of placed turrets: at most `kMaxTurrets = 64`, checked
at placement. A turret is the CPU's record. The GPU only sees the derived
data in sections 5 and 6.

## 5. Damage to units (GPU)

Each step's uniforms gain a turret block next to `Fields` and `Explosions`:
`TurretCount` and two `float4`s per turret (position, facing, range, arc, this
step's damage, kind). `unit_integrate` gets `turretDamage(centre)` alongside
`explosionPush`, which loops over the active turrets:

- **Flamer:** inside the range, and the angle to the unit is within the
  half-arc of its facing.
- **Zapper:** inside the radius on the step a pulse fires. The CPU sets that
  step's damage, and it's 0 otherwise.
- **Beam:** distance from the unit to the beam's segment is under half its
  width. The CPU advances the sweep angle each step.

Mortar shells and Mines don't go through this block. They use the existing
`Explosion` path, so they share its falloff, fling and drawing. That path
currently holds `kMaxExplosions = 8` at once. Raise it to 256, high enough that
it should never be reached in practice. Three places change together:
`kMaxExplosions` in `PhysicsSystem.hpp`, `MAX_EXPLOSIONS` in
`unit_integrate.comp.hlsl`, and the uniform-size `static_assert` in
`GpuCollidePhysics.cpp`. At 32 bytes each, 256 explosions add 8 KB to the
step's uniforms. That needs checking against SDL_GPU's uniform size limit.
If it doesn't fit, move explosions into a small storage buffer, as `Health`
already is. Each unit loops only up to `ExplosionCount`, so a high cap costs
nothing while it's unused.

At 64 turrets a flat loop per unit is affordable. If the cap grows, bin
turrets into the wall-bin grid.

Kills already happen in `unit_finalize` when health reaches 0. A kill counter
gets added next to the leak counter (section 7).

## 6. Blockers (GPU)

Blockers are walls while they stand. At "Start wave", and again whenever one
breaks, the physics system rebuilds its `WallColliders` as the level's own
walls plus one rotated box per standing blocker (`WallColliders::withExtra`),
and re-uploads `GpuWalls`. Every pass that keeps units out of walls (the
collision passes, `unit_finalize`'s sweep and spawn placement) then handles
blockers with no extra code. A blocker's box is tagged by its `radius` field,
which boxes don't otherwise use: 1 + its turret slot, or 0 for level walls.

**Damage to blockers:** each step, `unit_finalize` finds the tagged boxes a
unit is touching (within half a world unit of its radius). Each touching unit
atomically adds a fixed amount of wear, in thousandths of a hit point, to that
blocker's slot of the feedback buffer: 1 HP per touching unit per second.
A trickle of units barely scratches a blocker, while a crowd pressed against
it wears it down fast. The CPU reads the totals back asynchronously, a few
frames late, and removes the blocker at 0 HP.

This counts contact rather than overlap depth, as first planned. The
collision passes push units out of walls every pass, so the overlap left by
`unit_finalize` is too small and noisy to measure pressure by.

**Damage to units:** a Spike block also damages each unit in contact, by its
damage per second times the step's `Dt`, in the same contact test.

## 7. Exits and leaks

In wave mode, a unit reaching an exit despawns, using the same removal as a
death, and atomically increments a leak counter. It doesn't respawn, and no
exit damage applies. The CPU reads kills and leaks back asynchronously and
shows them in the HUD. `respawnAtExit` and `exitDamage` stay as they are for
sandbox mode.

## 8. Mortar targeting and mines

All turret feedback shares one small GPU buffer (`kFeedbackWords` uints, laid
out in `sim/TurretEffects.hpp`): kills and leaks, then four uints per turret
slot. It is downloaded on a command buffer of its own whose fence is polled,
never waited on, so results arrive a frame or two late and nothing stalls.

**Mortar:** when one is due, `turret_aim.comp` runs after the grid build,
one thread group per mortar. It scores every other grid cell within range by
the units in the 5x5 cells around it, and writes the best cell and the shot's
ID to the mortar's slot. When the CPU reads back an answer for the shot still
waiting, the shell lands at that cell 0.8 s after it was fired, as a flinging
explosion. If nobody is in range, the mortar looks again half a second later.

**Mine:** while armed, a mine is sent to `unit_integrate` as a turret effect
that does no damage. Each unit within its trigger radius adds 1 to the mine's
slot. Once the CPU sees a count, the mine goes off as a flinging explosion and
is spent.

## 9. Glossary additions (`CONTEXT.md`)

**Turret**: something the player places during the prep phase that damages or
obstructs units. Either a mount, a blocker or a floor trap.
_Avoid_: tower, defence, weapon

**Mount**: a turret placed on top of a wall, out of the horde's reach.
_Avoid_: emplacement

**Blocker**: a solid turret placed on open ground. Units collide with it, and
it has hit points.
_Avoid_: obstacle (already avoided for walls), barrier

**Prep phase**: the time before the wave, when turrets are placed and no units
spawn.
_Avoid_: build phase, setup

**Wave**: the stream of units from spawns, after the prep phase.
_Avoid_: round

**Leak**: a unit that reaches an exit during a wave. It despawns and is
counted.
_Avoid_: escape

## 10. Rendering

Turrets are sprites through `SpriteBatch`, drawn above walls and below units,
with blocker damage shown by tint. During prep, the ghost preview and every
turret's range or cone are drawn as overlays. During the wave, flames and beams
get a simple visual, and explosions are already drawn.

## 11. Testing

By hand in `LevelScene`, plus checks in `bench/Quality`:
- No unit stays embedded in a blocker (the existing "units in walls" check,
  extended to blockers).
- With turret damage set to 0 and blocker HP set to infinite, kills plus leaks
  equals units spawned.

---

## Future work

**Edge-mounted turrets.** Some kinds that can only be placed on a wall's edge,
facing along the edge's outward normal instead of a free facing. They'd attach
the way markers do: to one primitive's edge or one cell's open side, at a
fraction along it (see ADR 0002, edge-relative markers). Candidates are a
flamer or beam that covers exactly the corridor in front of the wall, and a fan
that pushes units off the wall.

**Modular turrets.** Turrets assembled from parts, rather than a fixed table of
kinds. To be designed later.

**Wave settings in the level editor.** A level's wave settings (length mode,
unit count, duration, spawn rate) are saved with it but can only be changed by
editing the file for now. The editor needs a panel for them. Its test-play
"units" setting doesn't apply in wave mode, so that panel should replace it.

## Open questions

- None yet.
