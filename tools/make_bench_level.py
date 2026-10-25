#!/usr/bin/env python3
"""Writes assets/levels/bench_walls.level.json: a 2000x2000 level using every
wall kind, for benchmarking unit-vs-wall collision (tools/bench.py --level).

Units spawn along the whole west edge and head for an exit along the whole
east edge, through a field of circular pillars, past rotated boxes and
triangles, and around a funnel of polylines near the exit. Rerun to
regenerate; edit freely in the level editor afterwards.
"""

import json
import math
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
GREY = [150, 150, 165]


def rect(cx, cy, hx, hy, deg=0.0):
    return {"kind": "rectangle", "center": [cx, cy], "color": GREY, "rotation": deg, "halfExtents": [hx, hy]}


def tri(cx, cy, hx, hy, deg=0.0):
    return {"kind": "triangle", "center": [cx, cy], "color": GREY, "rotation": deg, "halfExtents": [hx, hy]}


def circle(cx, cy, r):
    return {"kind": "circle", "center": [cx, cy], "color": GREY, "radius": r}


def polyline(cx, cy, points, thickness, deg=0.0):
    return {"kind": "polyline", "center": [cx, cy], "color": GREY, "rotation": deg, "thickness": thickness,
            "points": [[x - cx, y - cy] for x, y in points]}


walls = []

# A staggered field of pillars across the western half.
for col in range(8):
    for row in range(10):
        x = 250 + col * 100
        y = 120 + row * 180 + (90 if col % 2 else 0)
        walls.append(circle(x, y, 18 + (row * 7 + col * 3) % 14))

# Rotated boxes and triangles in the middle.
for i, (x, y, deg) in enumerate([(1150, 300, 30), (1150, 1700, -30), (1250, 700, 60), (1250, 1300, -60)]):
    walls.append(rect(x, y, 90, 20, deg))
walls.append(rect(1100, 1000, 25, 160, 0))
for i, (x, y, deg) in enumerate([(1400, 450, 90), (1400, 1550, 90), (1350, 1000, -90)]):
    walls.append(tri(x, y, 60, 50, deg))

# A funnel of thick polylines in front of the exit (centred on the east edge).
walls.append(polyline(1700, 500, [(1500, 150), (1700, 500), (1850, 900)], 16))
walls.append(polyline(1700, 1500, [(1500, 1850), (1700, 1500), (1850, 1100)], 16))
# A zig-zag of thin segments partly blocking the funnel's mouth.
walls.append(polyline(1600, 1000, [(1580, 880), (1620, 940), (1580, 1000), (1620, 1060), (1580, 1120)], 6))

level = {
    "version": 1,
    "size": [2000.0, 2000.0],
    "backgroundColor": [38, 42, 50],
    "walls": walls,
    "markers": [
        {"kind": "spawn", "edge": "west", "offset": 0.0, "length": 2000.0},
        {"kind": "exit", "edge": "east", "offset": 0.0, "length": 2000.0},
    ],
}

out = ROOT / "assets" / "levels" / "bench_walls.level.json"
out.parent.mkdir(parents=True, exist_ok=True)
out.write_text(json.dumps(level, indent=2) + "\n", encoding="utf-8")
print(f"{out}: {len(walls)} walls")
