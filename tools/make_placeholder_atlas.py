#!/usr/bin/env python3
"""Generates assets/textures/atlas.png, the placeholder sprite atlas.

The atlas is 64x64, four 32x32 cells, addressed by gfx::atlasCell(col, row, 2, 2):

    (0,0) bordered square   tiles
    (1,0) filled disc       unlocked tech node
    (0,1) crossed disc      locked tech node
    (1,1) solid             lines, edges, flat fills

Everything is drawn in white and shaped with alpha, so the per-sprite tint in
Sprite::color is what actually colours it.

Committed output, so this only needs re-running if the placeholder art changes.
Replace the whole file with real art when there is any; nothing in the code
depends on it being generated.
"""

import pathlib
import struct
import zlib

SIZE = 64
CELL = SIZE // 2


def disc(x, y, cx, cy, radius):
    dx = x - cx
    dy = y - cy
    return dx * dx + dy * dy <= radius * radius


def pixel(x, y):
    """Returns (r, g, b, a) for one texel."""
    col = x // CELL
    row = y // CELL
    lx = x % CELL
    ly = y % CELL
    centre = (CELL - 1) / 2.0

    if (col, row) == (0, 0):
        # Bordered square: solid 2px frame, dim fill.
        edge = lx < 2 or ly < 2 or lx >= CELL - 2 or ly >= CELL - 2
        return (255, 255, 255, 255 if edge else 190)

    if (col, row) == (1, 0):
        return (255, 255, 255, 255 if disc(lx, ly, centre, centre, CELL * 0.42) else 0)

    if (col, row) == (0, 1):
        if not disc(lx, ly, centre, centre, CELL * 0.42):
            return (255, 255, 255, 0)
        # Punch a diagonal slash out of the disc to read as "locked".
        if abs((lx - ly)) <= 2:
            return (255, 255, 255, 60)
        return (255, 255, 255, 255)

    return (255, 255, 255, 255)


def main():
    root = pathlib.Path(__file__).resolve().parent.parent
    out = root / "assets" / "textures" / "atlas.png"
    out.parent.mkdir(parents=True, exist_ok=True)

    raw = bytearray()
    for y in range(SIZE):
        raw.append(0)  # PNG filter type 0 (None) for this scanline
        for x in range(SIZE):
            raw.extend(pixel(x, y))

    def chunk(tag, data):
        body = tag + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body))

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", SIZE, SIZE, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")

    out.write_bytes(png)
    print(f"wrote {out} ({len(png)} bytes)")


if __name__ == "__main__":
    main()
