# Unit physics is a swappable system built from shared GPU stages

How units move and collide is behind one interface, `sim::PhysicsSystem`, with
implementations chosen by name from the table in `src/sim/PhysicsRegistry.cpp`.
`LevelScene` owns one and forwards `update`, `compute` and `renderPass` to it.
GPU systems are built from reusable stages instead of one monolithic shader
chain:

- **Broadphase**: `sim::GpuSpatialGrid` buckets units into a uniform grid
  (clear, key, sort, find ranges) and exposes `sortedKeys` and `cellRanges`.
- **Collision**: `sim::GpuUnitCollisions` runs N Jacobi relaxation passes
  (`unit_collide.comp`) over a pair of unit buffers, using the grid.
- **Integration and finalize**: per system. `gpu-collide` uses position-based
  dynamics: predict positions, correct them, then derive velocity from where
  each unit actually ended up, so collisions slow and deflect units.

Collision and movement rules (`sim::CollisionSettings`, `sim::MovementSettings`)
are part of `sim::SimConfig`, so every system is handed the same problem. All
systems draw through `gfx::UnitRenderer`, so a benchmark compares simulation and
not drawing.

## Consequences

A new approach (a counting sort, Gauss-Seidel by graph colouring, a
multithreaded CPU solver, walls) is a new system or a replaced stage. It doesn't
need an edit to `LevelScene`, and the old one stays runnable to compare against
with `tools/bench.py`. Anything else that needs neighbours later (separation
steering, target finding, area damage) builds on the same `GpuSpatialGrid`
rather than growing its own. Walls are expected to be another correction pass
beside the unit-unit one, between the broadphase and finalize.

`PhysicsSystem::readUnits` makes every system's state inspectable from the CPU.
The benchmark uses it to measure collision quality (overlapping units,
penetration depth) next to speed, because a system that skips collisions would
otherwise win.

Three costs are deliberate. Unit data stays `BaseUnitData {pos, vel}` because the
renderer and every shader share that layout: per-unit radius, mass or team
would mean widening it everywhere at once. Positions are the unit's top-left
corner, as the renderer draws them, so units are discs whose diameter is the unit
size. And the grid is built once per frame from predicted positions, so later
relaxation passes search slightly stale cells. That's fine while a pass moves a
unit by less than a cell, and `maxCorrection` caps it there.
