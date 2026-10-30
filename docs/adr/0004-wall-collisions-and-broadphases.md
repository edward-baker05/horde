# Walls are binned convex primitives; the broadphase is a per-tier choice

## Walls

Units collide with level walls through `sim::WallColliders`, built once when a
level starts (walls never move). Every wall is flattened into convex
primitives, each with a signed distance function: a rotated box, a circle, a
triangle, or one capsule per polyline segment. The primitives are then binned
into a coarse uniform grid of cells eight unit-radii across, so a unit only
tests the few primitives near it however many walls the level has. `GpuWalls`
uploads the same data, and `shaders/src/walls.hlsli` repeats the CPU maths, so
the GPU, spawn placement and the benchmark's "units in walls" check all agree
on the shapes.

Wall pushes happen inside every collision pass, right after the unit-vs-unit
correction, and once more in `unit_finalize`. Because velocity is derived from
the corrected position, units slide along walls instead of sticking to them.

Polyline capsules have rounded ends where the renderer draws square ones. The
collision is at most half a thickness larger at each end, and it closes the gaps
the renderer leaves at bends, which is what a unit sliding along it wants.

We also considered baking a signed distance field texture of the whole level:
one texture fetch per unit, whatever the walls. We rejected it for now. Its
accuracy depends on resolution, thin polylines need about one texel per two
world units (16 MB for a 2000×2000 level), and it would need rebaking whenever
the editor changes a wall. Binned primitives are exact and already cheap. An SDF
is still the move if wall counts ever reach thousands in one place.

## Broadphase

`GpuSpatialGrid` offers three methods behind one interface (`Broadphase`), and
the collision shader variants share one binding layout:

| method | build passes | cost | caveat |
|---|---|---|---|
| bitonic sort | ~210 at 1M units | O(n log² n) | the original; kept as a baseline |
| counting sort | 7 | O(n + cells) | atomics order units within a cell non-deterministically |
| buckets | 2 | O(n + cells) | a cell holding more than `bucketCapacity` units hides the extras from its neighbours |

Measured on an RTX 5080, 4 collision passes, GPU simulation time per frame:

| units, level | bitonic | counting | buckets |
|---|---|---|---|
| 100k, 2000² with 91 walls | 0.95 ms | 0.54 ms | 0.42 ms |
| 1M, 8000², no walls | 2.96 ms | 1.97 ms | 2.38 ms |

Buckets win on small grids. Counting sort wins on large ones, where the buckets
method has to clear and scatter across millions of cells. The collision passes,
not the broadphase, dominate at high counts.

**Update (2026-10-30):** counting sort is now the only broadphase. Bitonic and
buckets were removed, along with their shaders, `--broadphase`,
`--bucket-capacity` and the `gpu-collide-*` registry entries. The table above
is kept as the reason.

## Solver

Each collision pass is Jacobi: every unit reads the same state (one unit
buffer) and writes the next (the other), so a pass doesn't depend on thread
order. For each overlapping neighbour, a unit takes half the overlap along the
line between them. `CollisionSettings::solver` picks how those halves are
combined:

- **relaxed** (default): the sum, times `relaxation`. Fast to converge. It can
  overshoot when many neighbours push at once and relaxation is high.
- **average**: the mean (position-based dynamics' constraint averaging). It
  never overshoots, but a unit pressed from every side moves only by the
  average push, so pressure spreads through a crowd more slowly.

Measured on an RTX 5080, mean overlap depth at the end of the run (fraction of
a diameter) on the walls level:

| units, passes | relaxed (0.5) | average |
|---|---|---|
| 10k, 4 | 0.12 | 0.22 |
| 100k, 4 | 0.22 | 0.30 |
| 10k, 8 | 0.07 | 0.15 |
| 100k, 8 | 0.17 | 0.26 |

Both fully separate a stationary random spawn.

The average solver can be over-relaxed: `averageOmega` (`--omega`, 1–2,
default 1 = the plain average) scales the mean, as in Müller et al.'s PBD.
Overlap depth for the same runs:

| units, passes | ω 1 | 1.25 | 1.5 | 1.75 | 2 | relaxed |
|---|---|---|---|---|---|---|
| 10k, 4 | 0.218 | 0.195 | 0.175 | 0.161 | 0.147 | 0.117 |
| 100k, 4 | 0.303 | 0.289 | 0.278 | 0.267 | 0.257 | 0.222 |
| 10k, 8 | 0.148 | 0.127 | 0.113 | 0.100 | 0.091 | 0.070 |
| 100k, 8 | 0.257 | 0.240 | 0.226 | 0.213 | 0.202 | 0.173 |

Each step up in ω helps, and nothing diverged even at 2, but the relaxed
solver still converges further. The reason is that summing at relaxation 0.5
across a typical 4–6 overlapping neighbours pushes harder than any ω ≤ 2 on the
mean. ω makes no difference to GPU time. The average solver is worth choosing
when the hard no-overshoot guarantee at ω = 1 matters (for example tiny
time steps or very dense spawns); otherwise relaxed is the better default.

## Scaling down

`sim::collisionPreset` (`--quality low|medium|high|ultra`, also in the level
panel) sets passes, relaxation and broadphase together. Low runs 1 buckets pass
at relaxation 0.8, and ultra runs 8 counting passes. On an Intel UHD 770
(`--low-power-gpu`), 100k units on the walls level ran at 177 / 135 / 90 / 58 fps
across low to ultra. Mean overlap depth fell from 0.23 to 0.07 of a diameter.

**Update (2026-10-30):** the quality presets were removed, to be re-evaluated
now that counting sort is the only broadphase. `--quality` and the level
panel's preset menu are gone; set passes and relaxation directly. The figures
above are from the old presets.

Every method uses only compute features all three SDL_GPU backends have
(structured buffers, 32-bit atomics, group-shared memory). There are no wave
intrinsics or 64-bit atomics.

## Not done yet, in rough order of expected payoff

- **Reorder units by cell** after the counting sort, so the collision passes
  read neighbours from contiguous memory instead of scattered across the unit
  buffer. This is the biggest likely win at 1M+. It moves unit indices every
  frame, so per-unit game data (health, team) would need the same permutation
  or a stable ID.
- **Adaptive quality**: lower the pass count when GPU sim time exceeds a frame
  budget, instead of picking a preset by hand.
- **Fewer passes for the same quality**: graph-coloured Gauss-Seidel (cells in
  a 2×2 checkerboard), or warm-starting from last frame's corrections. Jacobi
  only spreads crowd pressure one neighbour per pass.
- **Pathing**: units head straight for the exit and slide along whatever is in
  the way. A flow field (the old `bailen-testing` branch has a `VectorField`)
  would send them around walls rather than into them.
