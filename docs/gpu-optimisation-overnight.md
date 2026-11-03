# GPU optimisation experiments (overnight, 2026-11-03)

Branch `bailen-gpu-optimisation-overnight`, cut from `Bailen-gpu-shader-test`
at `a45cfc4`. Everything measured on an RTX 5080 (driver 621.01), Windows 11,
release build, `gpu-collide` physics, 8 s measured after 6 s warm-up, 2-3
repeats per row. Raw results are in `bench_results/overnight/` (git-ignored).

Ported to `Bailen-gpu-shader-test`: the benchmark backend sweep, the
row-merged neighbour loop, congestion slowdown, velocity smoothing, the jitter
and mean speed metrics, and the `crowd-100k-smooth` preset. The Gauss-Seidel
solver, the thread group size variants and collision kick were measured and
not ported; their code is only on `bailen-gpu-optimisation-overnight`, so the
flags for them below don't exist on other branches.

## TL;DR

- **Chebyshev is the best solver per millisecond.** At the same GPU cost it
  leaves about half the penetration of relaxed, and 16 Chebyshev passes match
  48 relaxed ones. Worth considering as the default for crowds.
- **The new Gauss-Seidel solver converges more per pass but loses on cost.**
  A pass costs 3-4x a Jacobi pass (one compute pass per colour), and its extra
  convergence doesn't make up for that. Not ported.
- **D3D12 beats Vulkan where the GPU is actually busy**: 6% at 2M units, 10% at
  4M, and 5-10% cheaper per collide pass at 100k. At 100k-1M with default
  settings the two are within noise. SDL already picks D3D12 on Windows, so
  nothing to change.
- **Thread group size (64/128/256/512) and the row-merged neighbour loop make
  no measurable difference**, within ±10% run-to-run noise.
- **`gpu sim ms` has a floor of about 0.25-0.35 ms** from fence timing, so
  differences between small workloads mostly measure noise. Compare per-pass
  costs by the slope over pass counts instead (below).

## What was added

| Change | Flag | Ported |
| --- | --- | --- |
| Sweep GPU backends in the benchmark script; sets `SDL_GPU_DRIVER` per run | `tools/bench.py --gpu-driver direct3d12,vulkan` | yes |
| Benchmark a saved build instead of rebuilding | `tools/bench.py --exe path/to/horde.exe` | yes |
| Multicolour in-place Gauss-Seidel solver (`unit_collide_gs.comp`) | `--solver gauss-seidel`, `--gs-colors 4\|9`, preset key `gsColors` | no |
| Neighbour loop walks each row of 3 cells as one contiguous key range (3 loops, not 9) | always on, collide and smooth shaders | yes |
| Collide pass at other thread group sizes (`unit_collide_wg*.comp`) | `--collide-group 64\|128\|256\|512` | no |

Gauss-Seidel colours cells by `(x, y) mod 2` (4 colours) or `mod 3` (9) and
runs one compute pass per colour, moving only that colour's units in place.
They have to be separate passes: SDL_GPU puts no barrier between dispatches
inside one compute pass on either backend (checked in
`SDL_gpu_d3d12.c`/`SDL_gpu_vulkan.c`), only at pass boundaries.

## Measuring: the fence floor

`GpuTimer` times a span by the gap between two fences, which includes submit
and signal latency. At 10k units `gpu sim ms` is still about 0.29 ms, so most
of a small workload's figure is that floor, not compute. The per-pass cost
below is the slope between 16 and 48 passes, which cancels it out.

## Solvers

### Per-pass cost (slope, ms per pass)

| Workload | Backend | relaxed | chebyshev | gauss-seidel (4 colours) |
| --- | --- | --- | --- | --- |
| crowd-100k (dense) | D3D12 | 0.0111 | 0.0112 | 0.0438 |
| crowd-100k (dense) | Vulkan | 0.0124 | 0.0134 | 0.0493 |
| 1M grid on 8000x8000 | D3D12 | 0.0367 | - | 0.108 |
| 1M grid on 8000x8000 | Vulkan | 0.0382 | - | 0.103 |

At 100k a Gauss-Seidel pass costs exactly 4 Jacobi passes: each colour's
dispatch still launches a thread for every unit, and at this size a pass is
bound by launch and barrier cost, not by the work. At 1M the ratio drops to
about 2.9x.

### Quality vs cost, `crowd-100k-relaxed` (D3D12)

Penetration is the mean overlap depth as a fraction of a diameter at the end
of the run (lower is better). Overlap % hardly moves: a crowd this dense is
always touching, so penetration is the number that matters.

| Solver | Passes | GPU sim ms | Penetration |
| --- | --- | --- | --- |
| relaxed (r 1.0) | 4 | 0.38 | 0.308 |
| relaxed | 16 | 0.50-0.57 | 0.198 |
| relaxed | 48 | 0.92 | 0.099 |
| average (w 2.0) | 16 | 0.56 | 0.191 |
| chebyshev (rho 0.95) | 8 | 0.42 | 0.164 |
| **chebyshev** | **16** | **0.47-0.55** | **0.095** |
| chebyshev | 48 | 0.91 | 0.043 |
| gauss-seidel (r 1.0) | 4 | 0.55 | 0.166 |
| gauss-seidel (r 1.0) | 8 | 0.67 | 0.110 |
| gauss-seidel (r 1.3) | 8 | 0.69 | 0.058 |
| gauss-seidel (r 1.0) | 16 | 0.99-1.06 | 0.060 |
| gauss-seidel (r 1.0) | 48 | 2.46 | 0.026 |
| gauss-seidel, 9 colours | 16 | 1.80 | 0.061 |

- Chebyshev-16 costs the same as relaxed-16 and halves the penetration; it
  takes relaxed 48 passes, about 1.8x the time, to get there.
- Gauss-Seidel has the best quality per pass (r 1.3 at 8 passes ≈ Chebyshev at
  16), but at 1.3-1.5x the time. Over-relaxing it helps: penetration at 8 passes
  went 0.181 / 0.147 / 0.111 / 0.058 / 0.062 for relaxation 0.5 / 0.8 / 1.0 /
  1.3 / 1.6.
- 9 colours costs 1.7x as much as 4 for the same quality: the race that 4
  colours allows doesn't matter in practice.

On the sparser 1M workloads (8000x8000 grid, 6000x6000 random) every solver
ends with almost no overlap (0.0-0.2%), so penetration there is averaged over a
handful of units and jumps around run to run; those runs only measure cost.

## D3D12 vs Vulkan

GPU sim ms, default settings (relaxed, 4 passes), random spawn:

| Units | Level | D3D12 | Vulkan | |
| --- | --- | --- | --- | --- |
| 10k | default | 0.298 | 0.285 | floor, noise |
| 100k | default | 0.25-0.34 | 0.25-0.30 | floor, noise |
| 1M | 8000x8000 | 0.68-0.73 | 0.67-0.75 | even |
| 2M | 12000x12000 | 1.49 | 1.59 | D3D12 6% faster |
| 4M | 16000x16000 | 4.21 / 3.92 | 4.57 / 4.34 | D3D12 8-10% faster |

The 4M row was run twice, D3D12 first and then Vulkan first, and D3D12 won
both times, so it isn't thermal drift. At 4M frame rate is no longer capped:
219-224 fps on D3D12 against 207-209 on Vulkan. The per-pass slopes agree:
D3D12 is 5-10% cheaper per collide pass at 100k and 4% at 1M. Vulkan sometimes
shows a lower single-pass figure at 100k, which is the fixed overhead (fence
floor and submission), not compute.

The driver caps every run at 240 fps, so `fps` and `frame ms` can't separate
the backends below about 2M units.

## Thread group size

`crowd-100k-relaxed` and 1M grid (8 passes), GPU sim ms:

| Group | crowd D3D12 | crowd Vulkan | 1M D3D12 | 1M Vulkan |
| --- | --- | --- | --- | --- |
| 64 | 0.41 | 0.52 | 0.91 | 0.87 |
| 128 | 0.54 | 0.50 | 0.86 | 0.87 |
| 256 | 0.52 | 0.49 | 0.89 | 0.86 |
| 512 | 0.51 | 0.49 | 0.87 | 0.90 |

No pattern beyond noise. 256 stays.

## Row-merged neighbour loop

Old 9-cell loop (baseline build) against the 3-row loop, relaxed solver:

| Workload | Backend | 9 cells | 3 rows |
| --- | --- | --- | --- |
| crowd-100k | D3D12 | 0.496 | 0.514 |
| crowd-100k | Vulkan | 0.549 | 0.502 |
| 1M grid, 8 passes | D3D12 | 0.909 | 0.895 |
| 1M grid, 8 passes | Vulkan | 0.905 | 0.859 |

Between -4% and +9%: within noise, maybe slightly ahead at 1M. Kept because
the collide and smooth shaders share it now.

## Suggested next steps

- Make Chebyshev (rho 0.95, 16 passes) the default for crowd presets, or add a
  `crowd-100k-chebyshev` preset.
- At 100k the fixed per-step cost (grid build, reorder, integrate, finalize,
  about 0.35 ms with the floor) is larger than the collide passes. Fewer compute
  passes per step (merging scan stages, or grid count into integrate) is the
  likelier win there than anything in the solver.
- For an exact GPU time per stage, timestamp queries would need SDL_GPU support
  or a native query path; the fence timer can't resolve sub-0.1 ms stages.

## Reproducing

```sh
python tools/bench.py --preset crowd-100k-relaxed --solver relaxed,chebyshev,gauss-seidel --collision-iterations 16 --gpu-driver direct3d12,vulkan --repeats 2
python tools/bench.py --physics gpu-collide --units 4m --level-size 16000x16000 --gpu-driver vulkan,direct3d12 --repeats 3
python tools/bench.py --preset crowd-100k-relaxed --solver gauss-seidel --collision-iterations 8 --relaxation 1.3
python tools/bench.py --preset crowd-100k-relaxed -- --collide-group 128
```

## Crowd flow: stopping dense crowds boiling (2026-11-03, later)

In a dense crowd units popcorn: each step's collision corrections point
different ways, `unit_finalize` keeps them as velocity (velocity is derived
from how far a unit actually moved), and the next step carries units into
fresh overlaps. Explosion-damage patterns (the dark health tint) then mix
instead of flowing and warping together. Meanwhile steering always asks for
full speed toward the exit, so units keep walking into the crowd: at speed 200
and 60 Hz that is two thirds of a diameter a step.

Three options, all off by default:

| Option | Flag / preset key | What it does |
| --- | --- | --- |
| Congestion slowdown | `--congestion START,STOP` / `congestionStart`, `congestionStop` | `unit_integrate` reads last step's grid as a density map and scales the desired speed down as the 3x3 cells centred two cells ahead go from START to STOP units per cell (hex packing ~1.15) |
| Velocity smoothing | `--smoothing F` / `smoothing` | `unit_smooth.comp`, one extra pass after finalize: velocity blended toward the touching neighbours' weighted mean (XSPH viscosity) |
| Collision kick | `--collision-kick F` / `collisionKick` | finalize keeps collisions slowing a unit whole, but only F of sideways pushes and speed-ups (not ported) |

Congestion and smoothing are in the level panel under *Crowd flow*.

The benchmark now reports **jitter**: each unit's speed relative to the mean
velocity of the units touching it, averaged (world units/s). Low means the
crowd moves as a body; high means it boils and mixes.

`crowd-100k-relaxed`, D3D12, 2 repeats:

| Variant | GPU sim ms | Overlap % | Penetration | Jitter | Mean speed |
| --- | --- | --- | --- | --- | --- |
| baseline | 0.49 | 65.5 | 0.198 | 74.6 | 139 |
| kick 0.3 | 0.49 | 77.6 | 0.198 | 31.9 | 78 |
| kick 0 | 0.58 | 87.0 | 0.212 | 29.6 | 49 |
| smoothing 0.25 | 0.50 | 64.0 | 0.200 | 34.4 | 121 |
| smoothing 0.5 | 0.53 | 63.0 | 0.195 | 16.3 | 114 |
| smoothing 1.0 | 0.49 | 62.5 | 0.191 | 18.2 | 111 |
| congestion 1.2-2.0 | 0.47 | 67.2 | 0.168 | 59.0 | 129 |
| congestion 1.0-1.6 | 0.45 | 73.0 | 0.137 | 20.4 | 94 |
| congestion 0.9-1.3 | 0.44 | 81.8 | 0.081 | 6.7 | 57 |
| congestion 1.2-2.0 + smoothing 0.5 | 0.42 | 65.9 | 0.171 | 11.5 | 105 |
| **congestion 1.0-1.6 + smoothing 0.5** | **0.56** | **73.1** | **0.136** | **4.6** | **86** |
| congestion 0.9-1.3 + smoothing 0.5 | 0.45 | 82.1 | 0.080 | 2.2 | 55 |

- **Smoothing is the fix for mixing**: jitter down 4.6x for a fifth of the
  speed, at about the cost of one collision pass. Past 0.5 it gains nothing.
- **Collision kick** helps half as much and halves the speed. Not worth it,
  not ported.
- **Congestion is the fix for density**: penetration falls with it, because
  units stop walking into the crowd. Its thresholds trade calm for speed.
  The first version sampled the 3x3 cells centred one cell ahead, which
  counted the unit's own row: any packed crowd read as blocked and everything
  gridlocked (speed 12). Centring two cells ahead fixed that.
- Overlap % rises with congestion while penetration falls: a slow crowd sits
  in many very light contacts rather than a few deep ones.
- Lower mean speed is partly the point: units queue instead of compressing.

`crowd-100k-smooth` is the 1.0-1.6 + 0.5 combination.

### Against relaxation (2026-11-03, later still)

Everything above ran at the preset's relaxation of 1.0. The same variants at
0.25 and 0.5 (`--relaxation`, D3D12, 2 repeats, 16 passes):

| Variant | Relaxation | GPU sim ms | Overlap % | Penetration | Jitter | Mean speed |
| --- | --- | --- | --- | --- | --- | --- |
| baseline | 0.25 | 0.47 | 65.1 | 0.194 | 8.8 | 127 |
| baseline | 0.5 | 0.46 | 63.4 | 0.171 | 9.0 | 120 |
| baseline | 1.0 | 0.48 | 65.4 | 0.199 | 74.0 | 139 |
| smoothing 0.5 | 0.25 | 0.48 | 65.8 | 0.190 | 3.0 | 125 |
| **smoothing 0.5** | **0.5** | **0.47** | **63.6** | **0.168** | **3.1** | **117** |
| smoothing 0.5 | 1.0 | 0.47 | 63.1 | 0.195 | 16.2 | 113 |
| congestion 1.0-1.6 | 0.25 | 0.46 | 81.8 | 0.121 | 3.5 | 59 |
| congestion 1.0-1.6 | 0.5 | 0.46 | 78.1 | 0.099 | 3.8 | 68 |
| congestion 1.0-1.6 | 1.0 | 0.43 | 73.3 | 0.138 | 21.1 | 94 |
| congestion 1.2-2.0 + smoothing 0.5 | 0.25 | 0.45 | 74.1 | 0.171 | 1.6 | 78 |
| **congestion 1.2-2.0 + smoothing 0.5** | **0.5** | **0.44** | **69.9** | **0.147** | **1.9** | **89** |
| congestion 1.2-2.0 + smoothing 0.5 | 1.0 | 0.46 | 66.1 | 0.171 | 11.5 | 105 |
| congestion 1.0-1.6 + smoothing 0.5 | 0.25 | 0.45 | 81.8 | 0.121 | 1.3 | 57 |
| congestion 1.0-1.6 + smoothing 0.5 | 0.5 | 0.46 | 78.7 | 0.098 | 1.5 | 66 |
| congestion 1.0-1.6 + smoothing 0.5 | 1.0 | 0.45 | 73.3 | 0.136 | 4.5 | 86 |
| congestion 0.9-1.3 + smoothing 0.5 | 0.25 | 0.44 | 88.3 | 0.072 | 1.0 | 37 |
| congestion 0.9-1.3 + smoothing 0.5 | 0.5 | 0.46 | 84.7 | 0.054 | 1.2 | 45 |
| congestion 0.9-1.3 + smoothing 0.5 | 1.0 | 0.43 | 81.6 | 0.080 | 2.2 | 56 |

- **Relaxation 1.0 is most of the boiling.** Dropping to 0.5 cuts baseline
  jitter 8x (74 to 9) and *lowers* penetration (0.199 to 0.171): at 1.0 a unit
  pushed by several neighbours at once overshoots, so the passes fight each
  other instead of converging.
- 0.5 beats 0.25 on penetration everywhere with about the same jitter: 0.25
  under-corrects in 16 passes.
- At 0.5, smoothing alone gets jitter to 3.1 at 117 speed (vs 139 at the
  boiling baseline), without slowing the crowd the way congestion does.
  Congestion on top is for capping density, not for calm.

Since then `crowd-100k-relaxed` and `crowd-100k-smooth` run at relaxation 0.5
and the level panel's slider stops at 0.5. Every `crowd-100k-relaxed` result
above this section was at 1.0: add `--relaxation 1` to reproduce them.
