#!/usr/bin/env python3
"""Build horde in release, then benchmark physics systems across unit counts.

Every (physics, units, repeat) combination is one run of
`horde --bench ...`: the game boots straight into a level, warms up, times a
fixed number of seconds, writes a JSON summary and quits. This script collects
those into one results directory with a CSV and a Markdown table.

Examples:
    python tools/bench.py
    python tools/bench.py --physics gpu-grid,cpu-grid --units 1000,10000,100000
    python tools/bench.py --physics all --units 1e3,1e4,1e5,1e6 --repeats 3
    python tools/bench.py --no-build --duration 5 --frames
    python tools/bench.py --physics gpu-collide --quality low,medium,high,ultra --units 10k,100k,1m
    python tools/bench.py --physics gpu-collide --solver relaxed,average --collision-iterations 8
    python tools/bench.py --physics gpu-collide --solver average --omega 1,1.5,1.9
    python tools/bench.py --low-power-gpu --level assets/levels/bench_walls.level.json

Results land in bench_results/<timestamp>/ (git-ignored).
"""

from __future__ import annotations

import argparse
import csv
import datetime as dt
import json
import os
import platform
import shutil
import statistics
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent

# Frame rates a display cap would pin results to.
COMMON_REFRESH_RATES = (60, 75, 90, 100, 120, 144, 165, 170, 180, 240, 280, 360, 480)


def platform_presets() -> tuple[str, str, Path]:
    """(configure preset, build preset, executable) for this machine."""
    system = platform.system()
    if system == "Windows":
        return "windows", "windows-release", ROOT / "build/windows/bin/Release/horde.exe"
    if system == "Darwin":
        return "macos-release", "macos-release", ROOT / "build/macos-release/bin/horde"
    return "linux-release", "linux-release", ROOT / "build/linux-release/bin/horde"


def find_cmake() -> str:
    """CMake from $HORDE_CMAKE, PATH, or a CLion / Visual Studio install."""
    if os.environ.get("HORDE_CMAKE"):
        return os.environ["HORDE_CMAKE"]
    found = shutil.which("cmake")
    if found:
        return found
    if platform.system() == "Windows":
        candidates = []
        for base in (Path(r"C:\Program Files\JetBrains"), Path(r"C:\Program Files\Microsoft Visual Studio")):
            if base.is_dir():
                candidates += base.glob("*/bin/cmake/win/x64/bin/cmake.exe")  # CLion
                candidates += base.glob("*/*/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe")
        if candidates:
            return str(sorted(candidates)[-1])
    sys.exit("bench: cmake not found. Put it on PATH or set HORDE_CMAKE to its full path.")


def build(cmake: str, configure_preset: str, build_preset: str) -> None:
    print(f"== Building ({build_preset})", flush=True)
    for cmd in ([cmake, "--preset", configure_preset], [cmake, "--build", "--preset", build_preset]):
        result = subprocess.run(cmd, cwd=ROOT)
        if result.returncode != 0:
            sys.exit(f"bench: build failed: {' '.join(cmd)}")


def parse_counts(text: str) -> list[int]:
    """'1000,1e4,100k,1m' -> [1000, 10000, 100000, 1000000]"""
    counts = []
    for part in text.split(","):
        part = part.strip().lower()
        if not part:
            continue
        scale = 1
        if part[-1] in "km":
            scale = 1000 if part[-1] == "k" else 1_000_000
            part = part[:-1]
        counts.append(int(float(part) * scale))
    return counts


def list_physics(exe: Path) -> list[str]:
    out = subprocess.run([str(exe), "--list-physics"], capture_output=True, text=True, check=True).stdout
    return [line.split()[0] for line in out.splitlines() if line.strip()]


def git_info() -> dict:
    def git(*args: str) -> str:
        try:
            return subprocess.run(["git", *args], cwd=ROOT, capture_output=True, text=True).stdout.strip()
        except OSError:
            return ""

    return {"commit": git("rev-parse", "--short", "HEAD"), "branch": git("rev-parse", "--abbrev-ref", "HEAD"),
            "dirty": bool(git("status", "--porcelain", "--untracked-files=no"))}


def variant_label(physics: str, quality: str | None, solver: str | None, omega: str | None) -> str:
    """'gpu-collide', 'gpu-collide@low', 'gpu-collide/average', 'gpu-collide@low/average w1.5'"""
    return (physics + (f"@{quality}" if quality else "") + (f"/{solver}" if solver else "")
            + (f" w{omega}" if omega else ""))


def run_one(exe: Path, args: argparse.Namespace, physics: str, quality: str | None, solver: str | None,
            omega: str | None, units: int, repeat: int, out_dir: Path) -> dict:
    label = variant_label(physics, quality, solver, omega)
    stem = f"{label.replace('@', '_').replace('/', '_').replace(' ', '_')}_{units}_r{repeat}"
    json_path = out_dir / "runs" / f"{stem}.json"
    log_path = out_dir / "logs" / f"{stem}.log"

    cmd = [str(exe), "--bench", "--physics", physics, "--units", str(units),
           "--warmup", str(args.warmup), "--duration", str(args.duration),
           "--spawn", args.spawn, "--seed", str(args.seed + repeat),
           "--width", str(args.width), "--height", str(args.height),
           "--out", str(json_path)]
    if args.fixed_dt is not None:
        cmd += ["--fixed-dt", str(args.fixed_dt)]
    if args.level:
        cmd += ["--level", args.level]
    if args.level_size:
        cmd += ["--level-size", args.level_size]
    # The preset first, so the individual flags below can override parts of it.
    if quality:
        cmd += ["--quality", quality]
    if args.broadphase:
        cmd += ["--broadphase", args.broadphase]
    if solver:
        cmd += ["--solver", solver]
    if omega:
        cmd += ["--omega", omega]
    if args.low_power_gpu:
        cmd.append("--low-power-gpu")
    for flag, value in (("--collision-iterations", args.collision_iterations), ("--relaxation", args.relaxation),
                        ("--speed", args.speed)):
        if value is not None:
            cmd += [flag, str(value)]
    if args.frames:
        cmd += ["--frames-out", str(out_dir / "frames" / f"{stem}.csv")]
    if args.no_gpu_sync:
        cmd.append("--no-gpu-sync")
    cmd += args.extra

    timeout = args.warmup + args.duration + args.timeout_slack
    try:
        proc = subprocess.run(cmd, capture_output=True, text=True, timeout=timeout)
        log_path.write_text(proc.stdout + proc.stderr, encoding="utf-8")
    except subprocess.TimeoutExpired as e:
        log_path.write_text(f"TIMEOUT after {timeout}s\n{e.stdout or ''}{e.stderr or ''}", encoding="utf-8")
        return {"label": label, "physics": physics, "units": units, "repeat": repeat, "status": "timeout"}

    if proc.returncode != 0 or not json_path.exists():
        return {"label": label, "physics": physics, "units": units, "repeat": repeat,
                "status": f"exit {proc.returncode}"}

    result = json.loads(json_path.read_text(encoding="utf-8"))
    result["repeat"] = repeat
    result["label"] = label
    return result


def stat(result: dict, key: str, field: str = "mean") -> float | None:
    value = result.get(key)
    if isinstance(value, dict):
        return value.get(field)
    return value


def mean_of_quality(runs: list[dict], field: str) -> float | None:
    values = [r["quality"][field] for r in runs if field in r.get("quality", {})]
    return statistics.fmean(values) if values else None


def aggregate(results: list[dict]) -> list[dict]:
    """One row per (physics[@quality][/solver], units): repeats averaged."""
    rows = []
    groups: dict[tuple[str, int], list[dict]] = {}
    for r in results:
        groups.setdefault((r["label"], r["units"]), []).append(r)

    for (physics, units), runs in groups.items():
        ok = [r for r in runs if r.get("status") == "ok"]
        row = {"physics": physics, "units": units, "runs": len(runs), "ok": len(ok)}
        if not ok:
            row["status"] = runs[0].get("status", "failed")
            rows.append(row)
            continue

        def mean_of(key: str, field: str = "mean") -> float | None:
            values = [v for v in (stat(r, key, field) for r in ok) if v is not None]
            return statistics.fmean(values) if values else None

        update = mean_of("update_ms")
        sim_gpu = mean_of("sim_gpu_ms")
        in_walls = mean_of_quality(ok, "units_in_walls")
        row.update({
            "status": "ok",
            "broadphase": ok[0].get("broadphase", "-"),
            "solver": ok[0].get("solver", "-"),
            "average_omega": ok[0].get("average_omega"),
            "in_walls_pct": in_walls * 100 / units if in_walls is not None and units else None,
            "fps_mean": mean_of("fps_mean"),
            "fps_1pct_low": mean_of("fps_1pct_low"),
            "frame_ms_mean": mean_of("frame_ms"),
            "frame_ms_p99": mean_of("frame_ms", "p99"),
            "update_ms_mean": update,
            "sim_gpu_ms_mean": sim_gpu,
            # What the physics itself costs per frame, CPU and GPU together.
            "sim_ms_mean": (update or 0.0) + (sim_gpu or 0.0),
            "overlap_pct": (lambda v: v * 100 if v is not None else None)(
                mean_of_quality(ok, "overlapping_fraction")),
            "penetration_mean": mean_of_quality(ok, "mean_penetration"),
            "penetration_max": mean_of_quality(ok, "max_penetration"),
            "fps_spread": (max(r["fps_mean"] for r in ok) - min(r["fps_mean"] for r in ok)) if len(ok) > 1 else 0.0,
        })
        rows.append(row)

    rows.sort(key=lambda r: (r["physics"], r["units"]))
    return rows


def fmt(value, digits: int = 2) -> str:
    if value is None:
        return "-"
    if isinstance(value, float):
        return f"{value:.{digits}f}"
    return str(value)


def write_summary(rows: list[dict], out_dir: Path, meta: dict) -> str:
    columns = ["physics", "units", "status", "fps_mean", "fps_1pct_low", "frame_ms_mean", "frame_ms_p99",
               "update_ms_mean", "sim_gpu_ms_mean", "sim_ms_mean", "overlap_pct", "penetration_mean", "penetration_max",
               "in_walls_pct", "broadphase", "solver", "average_omega", "fps_spread", "runs", "ok"]
    with open(out_dir / "summary.csv", "w", newline="", encoding="utf-8") as f:
        writer = csv.DictWriter(f, fieldnames=columns, extrasaction="ignore")
        writer.writeheader()
        writer.writerows(rows)

    headers = ["physics", "units", "fps", "1% low", "frame ms", "p99 ms", "cpu sim ms", "gpu sim ms", "sim ms",
               "overlap %", "penetration", "in walls %"]
    lines = ["| " + " | ".join(headers) + " |", "|" + "---|" * len(headers)]
    for r in rows:
        if r.get("status") != "ok":
            lines.append(f"| {r['physics']} | {r['units']:,} | {r.get('status')} |" + " |" * (len(headers) - 3))
            continue
        lines.append("| " + " | ".join([
            r["physics"], f"{r['units']:,}", fmt(r["fps_mean"], 1), fmt(r["fps_1pct_low"], 1),
            fmt(r["frame_ms_mean"]), fmt(r["frame_ms_p99"]), fmt(r["update_ms_mean"], 3),
            fmt(r["sim_gpu_ms_mean"], 3), fmt(r["sim_ms_mean"], 3), fmt(r["overlap_pct"], 1),
            fmt(r["penetration_mean"], 3), fmt(r["in_walls_pct"], 2)]) + " |")
    table = "\n".join(lines)

    notes = []
    cap = detect_cap(rows)
    if cap:
        notes.append(f"> **Frame rate looks capped at ~{cap} fps** (vsync, a driver frame limiter, G-Sync/Reflex...). "
                     "fps and frame ms cannot go above it; compare systems by *sim ms* instead.")

    md = [f"# horde benchmark {meta['started']}", "",
          f"- commit `{meta['git']['commit']}` on `{meta['git']['branch']}`"
          + (" (uncommitted changes)" if meta["git"]["dirty"] else ""),
          f"- {meta['settings']['warmup']}s warmup, {meta['settings']['duration']}s measured, "
          f"{meta['settings']['repeats']} repeat(s), spawn `{meta['settings']['spawn']}`",
          f"- GPU `{meta.get('gpu_name', '?')}`, backend `{meta.get('gpu_driver', '?')}`, "
          f"present mode `{meta.get('present_mode', '?')}`",
          f"- level `{meta['settings'].get('level') or 'default'}`",
          "", *notes, "" if notes else "", table, "",
          "*cpu sim ms* is the scene update on the CPU, *gpu sim ms* the physics compute work on the GPU, "
          "timed in isolation; *sim ms* is their sum. *overlap %* is the share of units overlapping another at the "
          "end of the run, *penetration* the mean overlap depth as a fraction of a unit's diameter, *in walls %* the "
          "share of units overlapping a wall. `name@preset` rows ran with `--quality preset`, `name/solver` rows with `--solver solver`, `wN` with `--omega N`.", ""]
    (out_dir / "summary.md").write_text("\n".join(md), encoding="utf-8")
    return table + ("\n\n" + notes[0] if notes else "")


def detect_cap(rows: list[dict]) -> int | None:
    """A refresh rate that the fastest runs all sit just under, if any."""
    fps = [r["fps_mean"] for r in rows if r.get("status") == "ok" and r.get("fps_mean")]
    if len(fps) < 2:
        return None
    top = max(fps)
    for rate in COMMON_REFRESH_RATES:
        if rate * 0.97 <= top <= rate * 1.01 and sum(1 for f in fps if f >= rate * 0.97) >= 2:
            return rate
    return None


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--physics", default="all", help="comma-separated names, or 'all' (default)")
    parser.add_argument("--units", default="1000,10000,100000", help="comma-separated counts; 1e5, 100k, 1m work")
    parser.add_argument("--repeats", type=int, default=1, help="runs per combination (seed varies per repeat)")
    parser.add_argument("--warmup", type=float, default=2.0, help="seconds before measuring")
    parser.add_argument("--duration", type=float, default=10.0, help="seconds measured")
    parser.add_argument("--spawn", default="random", choices=["random", "grid", "column"])
    parser.add_argument("--seed", type=int, default=1)
    parser.add_argument("--level", help="level file (default: built-in level)")
    parser.add_argument("--level-size", help="built-in level at another size, e.g. 8000x8000")
    parser.add_argument("--collision-iterations", type=int, help="relaxation passes per frame (game default 4)")
    parser.add_argument("--relaxation", type=float, help="fraction of each pass's correction applied (default 0.5)")
    parser.add_argument("--speed", type=float, help="unit speed toward the exit (default 60)")
    parser.add_argument("--quality", help="comma-separated presets to sweep (low, medium, high, ultra)")
    parser.add_argument("--solver", help="comma-separated collision solvers to sweep (relaxed, average)")
    parser.add_argument("--omega", help="comma-separated over-relaxation factors to sweep for the average solver")
    parser.add_argument("--broadphase", choices=["counting", "buckets", "bitonic"],
                        help="broadphase for gpu-collide (overrides the preset's)")
    parser.add_argument("--low-power-gpu", action="store_true", help="run on the integrated GPU, if there is one")
    parser.add_argument("--fixed-dt", type=float, help="simulation step per frame (game default 1/60; 0 = real)")
    parser.add_argument("--width", type=int, default=1600)
    parser.add_argument("--height", type=int, default=900)
    parser.add_argument("--frames", action="store_true", help="also save per-frame CSVs")
    parser.add_argument("--no-gpu-sync", action="store_true", help="don't time GPU sim work separately")
    parser.add_argument("--stop-below-fps", type=float, default=2.0,
                        help="skip larger unit counts for a system once it falls below this fps (0 = never)")
    parser.add_argument("--timeout-slack", type=float, default=90.0, help="seconds allowed beyond warmup+duration")
    parser.add_argument("--no-build", action="store_true", help="use the existing release build")
    parser.add_argument("--out-dir", type=Path, help="default bench_results/<timestamp>")
    parser.add_argument("extra", nargs="*", help="extra arguments passed to horde after --")
    args = parser.parse_args()

    configure_preset, build_preset, exe = platform_presets()
    if not args.no_build:
        build(find_cmake(), configure_preset, build_preset)
    if not exe.exists():
        sys.exit(f"bench: {exe} not found (build first, or drop --no-build)")

    available = list_physics(exe)
    physics = available if args.physics == "all" else [p.strip() for p in args.physics.split(",") if p.strip()]
    unknown = [p for p in physics if p not in available]
    if unknown:
        sys.exit(f"bench: unknown physics {unknown}; available: {available}")
    counts = sorted(parse_counts(args.units))

    started = dt.datetime.now().strftime("%Y-%m-%d_%H-%M-%S")
    out_dir = args.out_dir or ROOT / "bench_results" / started
    for sub in ("runs", "logs", *(["frames"] if args.frames else [])):
        (out_dir / sub).mkdir(parents=True, exist_ok=True)

    qualities: list[str | None] = [q.strip() for q in args.quality.split(",") if q.strip()] if args.quality else [None]
    solvers: list[str | None] = [v.strip() for v in args.solver.split(",") if v.strip()] if args.solver else [None]
    omegas: list[str | None] = [v.strip() for v in args.omega.split(",") if v.strip()] if args.omega else [None]
    variants = [(name, quality, solver, omega) for name in physics for quality in qualities for solver in solvers
                for omega in omegas]

    total = len(variants) * len(counts) * args.repeats
    print(f"== {total} run(s): physics {physics} x quality {qualities} x solver {solvers} x omega {omegas} "
          f"x units {counts} "
          f"x {args.repeats} "
          f"-> {out_dir}", flush=True)

    results: list[dict] = []
    n = 0
    for name, quality, solver, omega in variants:
        label = variant_label(name, quality, solver, omega)
        too_slow = False
        for units in counts:
            for repeat in range(args.repeats):
                n += 1
                if too_slow:
                    results.append({"label": label, "physics": name, "units": units, "repeat": repeat,
                                    "status": "skipped"})
                    continue
                print(f"[{n}/{total}] {label:<24} {units:>9,} units  run {repeat + 1}... ", end="", flush=True)
                r = run_one(exe, args, name, quality, solver, omega, units, repeat, out_dir)
                results.append(r)
                if r.get("status") == "ok":
                    print(f"{r['fps_mean']:8.1f} fps  frame {stat(r, 'frame_ms'):7.2f} ms  "
                          f"cpu sim {stat(r, 'update_ms'):7.3f} ms  gpu sim {fmt(stat(r, 'sim_gpu_ms'), 3)} ms",
                          flush=True)
                    if args.stop_below_fps and r["fps_mean"] < args.stop_below_fps:
                        too_slow = True
                else:
                    print(r.get("status"), "(see logs/)", flush=True)

    first_ok = next((r for r in results if r.get("status") == "ok"), {})
    meta = {
        "started": started,
        "git": git_info(),
        "host": {"system": platform.system(), "release": platform.release(), "machine": platform.machine(),
                 "processor": platform.processor()},
        "gpu_driver": first_ok.get("gpu_driver"),
        "gpu_name": first_ok.get("gpu_name"),
        "present_mode": first_ok.get("present_mode"),
        "settings": {k: v for k, v in vars(args).items() if k != "out_dir"},
    }
    (out_dir / "meta.json").write_text(json.dumps(meta, indent=2, default=str), encoding="utf-8")

    rows = aggregate(results)
    table = write_summary(rows, out_dir, meta)
    print("\n" + table)
    print(f"\nResults: {out_dir}")
    return 0 if all(r.get("status") in ("ok", "skipped") for r in results) else 1


if __name__ == "__main__":
    sys.exit(main())
