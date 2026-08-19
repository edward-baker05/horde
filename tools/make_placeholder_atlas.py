#!/usr/bin/env python3
"""Generates assets/textures/atlas.png, the placeholder sprite atlas.

The atlas is 512x512: a 4x4 grid of 128x128 cells, addressed by
gfx::atlasCell(col, row, 4, 4):

    (0,0) bordered square   tiles, rectangle walls
    (1,0) filled disc       unlocked tech node, circle walls, rotate handle
    (2,0) isoceles triangle triangle walls; apex at the TOP of the cell, which
                            is -y in world space, i.e. apex-up on screen
    (0,1) crossed disc      locked tech node
    (1,1) solid             lines, edges, flat fills, backgrounds, markers

Everything is drawn in white and shaped with alpha, so the per-sprite tint in
Sprite::color is what actually colours it. Alpha is supersampled 4x4 per texel
so edges stay smooth when a cell is scaled up to a large world-space wall.

Committed output, so this only needs re-running if the placeholder art changes.
"""

import pathlib
import struct
import zlib

COLS = 4
ROWS = 4
CELL = 128
WIDTH = COLS * CELL
HEIGHT = ROWS * CELL
SUBSAMPLES = 4


def coverage(fn, lx, ly):
    """Fraction of a texel covered, by supersampling `fn` on a SUBSAMPLES grid."""
    hits = 0
    step = 1.0 / SUBSAMPLES
    for sy in range(SUBSAMPLES):
        for sx in range(SUBSAMPLES):
            if fn(lx + (sx + 0.5) * step, ly + (sy + 0.5) * step):
                hits += 1
    return hits / (SUBSAMPLES * SUBSAMPLES)


def in_disc(x, y):
    dx = x - CELL / 2.0
    dy = y - CELL / 2.0
    r = CELL * 0.5 - 0.5
    return dx * dx + dy * dy <= r * r


def in_triangle(x, y):
    """Isoceles triangle filling the cell, apex at top-centre, base at bottom."""
    # Half-width grows linearly from 0 at the apex to CELL/2 at the base.
    half = (y / CELL) * (CELL / 2.0)
    return abs(x - CELL / 2.0) <= half


def cell_pixel(col, row, lx, ly):
    """Returns (r, g, b, a) for one texel of cell (col, row)."""
    if (col, row) == (0, 0):
        border = CELL // 16
        edge = lx < border or ly < border or lx >= CELL - border or ly >= CELL - border
        return (255, 255, 255, 255 if edge else 190)

    if (col, row) == (1, 0):
        return (255, 255, 255, round(255 * coverage(in_disc, lx, ly)))

    if (col, row) == (2, 0):
        return (255, 255, 255, round(255 * coverage(in_triangle, lx, ly)))

    if (col, row) == (0, 1):
        alpha = coverage(in_disc, lx, ly)
        if alpha == 0.0:
            return (255, 255, 255, 0)
        # Punch a diagonal slash out of the disc to read as "locked".
        if abs(lx - ly) <= CELL // 16:
            return (255, 255, 255, round(60 * alpha))
        return (255, 255, 255, round(255 * alpha))

    return (255, 255, 255, 255)


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    out = root / "assets" / "textures" / "atlas.png"
    out.parent.mkdir(parents=True, exist_ok=True)

    raw = bytearray()
    for y in range(HEIGHT):
        raw.append(0)  # PNG filter type 0 (None) for this scanline
        row = y // CELL
        ly = y % CELL
        for x in range(WIDTH):
            raw.extend(cell_pixel(x // CELL, row, x % CELL, ly))

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")

    out.write_bytes(png)
    print(f"wrote {out} ({len(png)} bytes)")


if __name__ == "__main__":
    main()
