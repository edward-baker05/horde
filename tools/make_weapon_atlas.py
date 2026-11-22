#!/usr/bin/env python3
"""Generates assets/textures/weapons.png, the placeholder weapon sprites.

Each weapon is a square in its primary weapon category's colour, with a darker
border and its number in white blocky pixel digits. A weapon that combines
categories gets a stripe of its secondary category's colour along the bottom.
Edges are hard: no anti-aliasing anywhere.

The atlas is 256x256: a 4x4 grid of 64x64 cells. Weapon n (1-based, the
TurretDef::spriteId) is in cell n - 1, read left to right then top to bottom.
Unused cells are transparent. Real art replaces cells in the PNG; the code
does not change.

The numbers and categories match docs/superpowers/specs/2026-11-21-weapon-
categories-design.md section 4. The colours match logic::weaponCategoryColor
in src/logic/Turret.cpp; keep the two in step.

Committed output, so this only needs re-running if the placeholders change.
"""

import pathlib
import struct
import zlib

COLS = 4
ROWS = 4
CELL = 64
WIDTH = COLS * CELL
HEIGHT = ROWS * CELL
BORDER = 4
STRIPE = 8      # height of the secondary-category stripe, inside the border
DIGIT_SCALE = 5  # screen pixels per glyph pixel

CATEGORY_COLOURS = {
    "merch": (214, 60, 160),
    "sound": (60, 120, 220),
    "chemical": (90, 180, 70),
    "physical": (230, 140, 40),
}

# spriteId: (primary, secondary or None)
WEAPONS = {
    1: ("merch", None),         # T-shirt cannon
    2: ("merch", None),         # Merch slinger
    3: ("merch", None),         # Merch stand
    4: ("sound", None),         # Smooth jazz speaker
    5: ("sound", None),         # Bass stack
    6: ("sound", None),         # Feedback amp
    7: ("chemical", None),      # Pepper spray
    8: ("chemical", None),      # Tear gas launcher
    9: ("chemical", None),      # Chloroform mister
    10: ("physical", None),     # Bouncer
    11: ("physical", None),     # Crowd barrier
    12: ("physical", None),     # Riot water cannon
    13: ("physical", "chemical"),  # Bouncer with pepper spray
    14: ("merch", "chemical"),     # Chloroform T-shirt cannon
}

# 3x5 digits, '#' lit.
DIGITS = {
    "0": ["###", "#.#", "#.#", "#.#", "###"],
    "1": [".#.", "##.", ".#.", ".#.", "###"],
    "2": ["###", "..#", "###", "#..", "###"],
    "3": ["###", "..#", "###", "..#", "###"],
    "4": ["#.#", "#.#", "###", "..#", "..#"],
    "5": ["###", "#..", "###", "..#", "###"],
    "6": ["###", "#..", "###", "#.#", "###"],
    "7": ["###", "..#", "..#", "..#", "..#"],
    "8": ["###", "#.#", "###", "#.#", "###"],
    "9": ["###", "#.#", "###", "..#", "###"],
}


def darker(colour):
    return tuple(c * 3 // 5 for c in colour)


def number_mask(text):
    """Set of (x, y) cell-local pixels lit by `text`, centred in the cell."""
    glyph_w, glyph_h, gap = 3, 5, 1
    width = (len(text) * (glyph_w + gap) - gap) * DIGIT_SCALE
    height = glyph_h * DIGIT_SCALE
    left = (CELL - width) // 2
    top = (CELL - height) // 2
    lit = set()
    for i, ch in enumerate(text):
        gx0 = left + i * (glyph_w + gap) * DIGIT_SCALE
        for row, line in enumerate(DIGITS[ch]):
            for col, mark in enumerate(line):
                if mark != "#":
                    continue
                for dy in range(DIGIT_SCALE):
                    for dx in range(DIGIT_SCALE):
                        lit.add((gx0 + col * DIGIT_SCALE + dx, top + row * DIGIT_SCALE + dy))
    return lit


def draw_cell(pixels, sprite_id, primary, secondary):
    index = sprite_id - 1
    ox = (index % COLS) * CELL
    oy = (index // COLS) * CELL
    fill = CATEGORY_COLOURS[primary]
    edge = darker(fill)
    stripe = CATEGORY_COLOURS[secondary] if secondary else None
    digits = number_mask(str(sprite_id))
    for y in range(CELL):
        for x in range(CELL):
            if (x, y) in digits:
                colour = (255, 255, 255)
            elif x < BORDER or y < BORDER or x >= CELL - BORDER or y >= CELL - BORDER:
                colour = edge
            elif stripe and y >= CELL - BORDER - STRIPE:
                colour = stripe
            else:
                colour = fill
            pixels[(oy + y) * WIDTH + ox + x] = colour + (255,)


def write_png(path, pixels):
    raw = bytearray()
    for y in range(HEIGHT):
        raw.append(0)  # filter: none
        for x in range(WIDTH):
            raw.extend(pixels[y * WIDTH + x])

    def chunk(kind, data):
        body = kind + data
        return struct.pack(">I", len(data)) + body + struct.pack(">I", zlib.crc32(body) & 0xFFFFFFFF)

    png = b"\x89PNG\r\n\x1a\n"
    png += chunk(b"IHDR", struct.pack(">IIBBBBB", WIDTH, HEIGHT, 8, 6, 0, 0, 0))
    png += chunk(b"IDAT", zlib.compress(bytes(raw), 9))
    png += chunk(b"IEND", b"")
    path.write_bytes(png)


def main():
    assert max(WEAPONS) <= COLS * ROWS, "more weapons than cells: grow the grid"
    pixels = [(0, 0, 0, 0)] * (WIDTH * HEIGHT)
    for sprite_id, (primary, secondary) in WEAPONS.items():
        draw_cell(pixels, sprite_id, primary, secondary)
    out = pathlib.Path(__file__).resolve().parent.parent / "assets" / "textures" / "weapons.png"
    write_png(out, pixels)
    print(f"wrote {out} ({len(WEAPONS)} weapons)")


if __name__ == "__main__":
    main()
