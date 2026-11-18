#!/usr/bin/env python3
"""Builds the unit sprite atlas, its palettes and the generated headers.

Units are crowd texture, not characters: top-down heads with no faces, drawn at
three levels of detail picked from the camera zoom. Art is stored as ROLES
(hair, hair light, skin, clothes, accessory...), not colours, and coloured on
the GPU from a palette chosen per unit by hashing its stable ID, so ~50 cells
give hundreds of thousands of distinct-looking units.

Files
-----
assets/art/unit_sheet.png      EDIT THIS. The authoring sheet, drawn in the fixed
                               key colours below (one per role). Pixels must be
                               fully opaque key colours or fully transparent.
assets/textures/units.png      Generated. R = role index, A = 255 where filled.
src/gfx/UnitSpriteData.hpp     Generated. Atlas layout + palettes for C++.
shaders/src/unit_sprites.hlsli Generated. Atlas layout + palette offsets for HLSL.

Usage
-----
    py -3 tools/make_unit_atlas.py              # sheet -> atlas + headers
    py -3 tools/make_unit_atlas.py --bootstrap  # (re)write the sheet from the
                                                # built-in templates first
                                                # (refuses to overwrite without --force)

Palettes (and their weights) live in PALETTES below: edit, re-run, rebuild.
Changing only palettes needs no art change; the headers carry the colours.

Sheet layout
------------
Three LOD blocks stacked vertically (near, mid, far), each a grid of
ATLAS_COLUMNS x VARIANT_ROWS cells of CELL x CELL pixels. Variant v sits at
column v % ATLAS_COLUMNS, row v // ATLAS_COLUMNS of every block. A unit's head
centre is the cell centre (CELL / 2), and its head is HEAD[lod] pixels across,
which the renderer scales to the unit's diameter.

Variants: every human hairstyle x accessory, then every alien type.
"""

import argparse
import pathlib
import struct
import sys
import zlib

ROOT = pathlib.Path(__file__).resolve().parent.parent
SHEET = ROOT / "assets" / "art" / "unit_sheet.png"
ATLAS = ROOT / "assets" / "textures" / "units.png"
CPP_OUT = ROOT / "src" / "gfx" / "UnitSpriteData.hpp"
HLSL_OUT = ROOT / "shaders" / "src" / "unit_sprites.hlsli"

CELL = 13
ATLAS_COLUMNS = 8
LODS = ["near", "mid", "far"]
HEAD = {"near": 7, "mid": 5, "far": 5}       # head diameter in sprite pixels
WINDOW = {"near": 11, "mid": 9, "far": 5}    # square drawn around the cell centre

# ---------------------------------------------------------------------------
# Roles. Index 0 is empty. Order is baked into the shader via the header.
# ---------------------------------------------------------------------------
ROLES = ["empty", "hair", "hair_light", "hair_dark", "skin", "skin_light", "clothes",
         "accessory", "accessory_dark", "glasses", "glint"]
ROLE = {name: i for i, name in enumerate(ROLES)}

# Template characters -> role. Aliens reuse the hair roles for their skin and
# the accessory role for antenna tips; species picks which palette fills them.
CHAR_ROLE = {"H": "hair", "L": "hair_light", "D": "hair_dark", "S": "skin", "K": "skin_light",
             "C": "clothes", "A": "accessory", "a": "accessory_dark", "G": "glasses", "W": "glint",
             "X": "hair", "Y": "hair_light", "Z": "hair_dark"}

# Key colours for the authoring sheet. Human rows are drawn in the first set,
# alien rows in the second; both read back as the same roles.
KEY_HUMAN = {"hair": (110, 76, 50), "hair_light": (150, 110, 72), "hair_dark": (72, 48, 32),
             "skin": (224, 178, 140), "skin_light": (242, 206, 172), "clothes": (72, 86, 118),
             "accessory": (176, 64, 60), "accessory_dark": (114, 42, 39), "glasses": (22, 22, 26),
             "glint": (140, 156, 186)}
KEY_ALIEN = {"hair": (150, 80, 220), "hair_light": (191, 147, 231), "hair_dark": (97, 52, 143),
             "accessory": (255, 240, 90), "clothes": (170, 176, 190)}

# ---------------------------------------------------------------------------
# Templates (straight down, units face the stage = up). Edit the SHEET for
# hand-tuning; these only seed it via --bootstrap.
# ---------------------------------------------------------------------------
SHOULDERS = [".........", ".........", ".........", "CCCCCCCCC",
             "CCCCCCCCC", "CCCCCCCCC", ".CCCCCCC.", "...CCC..."]

HUMAN_NEAR = {
    "short":     ["...HHH...", "..HLHHH..", ".HLHHHHH.", ".HHHHHHH.", ".HHHHHHH.", "..HHHHD..", "...DDD...", "........."],
    "pompadour": ["..LLLLL..", ".HHLLLHH.", ".HHHHHHH.", ".HHHHHHH.", ".HHHHHHH.", "..HHHHH..", "...DDD...", "........."],
    "afro":      ["..HHHHH..", ".HHDHHHH.", "HHHHHHDHH", "HDHHHHHHH", "HHHHDHHHH", "HHHHHHHDH", ".HDHHHHH.", "..HHHHH.."],
    "beehive":   ["...HHH...", "..HHHHH..", ".HHLLLHH.", ".HHLLLHH.", ".HHLLLHH.", "..HHHHH..", "...DDD...", "........."],
    "mohawk":    ["....H....", "...SLS...", "..SSLSS..", ".SSSHSSS.", ".SSSHSSS.", "..SSHSS..", "...SHS...", "....D...."],
    "bald":      ["...SSS...", "..SKSSS..", ".SKSSSSS.", ".SSSSSSS.", ".SSSSSSS.", "..SSSSS..", "...SSS...", "........."],
    "mullet":    ["...HHH...", "..HLHHH..", ".HLHHHHH.", ".HHHHHHH.", ".HHHHHHH.", "..HHHHH..", "...HHH...", "...HDH..."],
    "perm":      ["..H.H.H..", ".HHHHHHH.", "HHLHHDHHH", ".HHHHHHHH", "HHHDHHHH.", ".HHHHHDHH", "..HHHHH..", "...H.H..."],
    "ponytail":  ["...HHH...", "..HLHHH..", ".HLHHHHH.", ".HHHHHHH.", ".HHHHHHH.", "..HHHHH..", "....D....", "....H...."],
}
HUMAN_MID = {
    "short":     [".......", "..HHH..", ".HLHHH.", ".HHHHH.", ".HHHHH.", "..HDH..", "......."],
    "pompadour": ["..LLL..", ".HHHHH.", ".HHHHH.", ".HHHHH.", ".HHHHH.", "..HDH..", "......."],
    "afro":      ["..HHH..", ".HHHHH.", "HHHHHHH", "HHHHHHH", "HHHHHHH", ".HHHHH.", "..HHH.."],
    "beehive":   [".......", "..HHH..", ".HLLLH.", ".HLLLH.", ".HLLLH.", "..HHH..", "......."],
    "mohawk":    ["...H...", "..SHS..", ".SSHSS.", ".SSHSS.", ".SSHSS.", "..SHS..", "...H..."],
    "bald":      [".......", "..SSS..", ".SKSSS.", ".SSSSS.", ".SSSSS.", "..SSS..", "......."],
    "mullet":    [".......", "..HHH..", ".HLHHH.", ".HHHHH.", ".HHHHH.", "..HHH..", "..HDH.."],
    "perm":      [".H.H.H.", "HHHHHHH", ".HHHHHH", "HHHHHHH", "HHHHHH.", ".HHHHHH", "..H.H.."],
    "ponytail":  [".......", "..HHH..", ".HLHHH.", ".HHHHH.", ".HHHHH.", "..HDH..", "...H..."],
}
ALIEN_NEAR = {
    "antennae": [".A.....A.", "..Z...Z..", "..XXXXX..", ".XYXXXXX.", ".XXXXXXX.", ".XXXXXXX.", "..XXXXX..", "...ZZZ..."],
    "brain":    ["..XXXXX..", ".XYXZXXX.", "XYXZXXXXX", "XXXXZXXXX", "XXXZXXXXX", ".XXXZXXX.", "..XXXXX..", "........."],
    "tentacle": ["...XXX...", "..XYXXX..", ".XYXXXXX.", ".XXXXXXX.", "..XXXXX..", ".X.X.X.X.", ".Z.Z.Z.Z.", "........."],
    "spiky":    [".Z.Z.Z.Z.", "..XXXXX..", ".XYXXXXX.", ".XXXXXXX.", ".XXXXXXX.", "..XXXXX..", "...XXX...", "........."],
}
ALIEN_MID = {
    "antennae": [".A...A.", "..XXX..", ".XXXXX.", ".XXXXX.", ".XXXXX.", "..XXX..", "......."],
    "brain":    ["..XXX..", ".XXZXX.", "XXZXXXX", "XXXZXXX", ".XZXXX.", "..XXX..", "......."],
    "tentacle": ["..XXX..", ".XXXXX.", ".XXXXX.", "..XXX..", ".X.X.X.", ".Z.Z.Z.", "......."],
    "spiky":    [".Z.Z.Z.", "..XXX..", ".XXXXX.", ".XXXXX.", ".XXXXX.", "..XXX..", "......."],
}
FAR = [".XXX.", "XXXXX", "XXXXX", "XXXXX", ".XXX."]

ACCESSORIES = ["none", "cap", "sunglasses", "headband", "bow"]
ACCESSORY_WEIGHTS = [3, 1, 1, 1, 1]          # out of sum: most units wear nothing
ACC_OVERLAY = {
    "cap":        {0: "..aaaaa..", 1: "..AAAAA..", 2: ".AAAAAAA.", 3: ".AAAaAAA.", 4: ".AAAAAAA.", 5: "..AAAAA.."},
    "sunglasses": {1: "..GGGGW.."},           # pushed up on the head
    "headband":   "ring",                     # front half of the head outline
    "bow":        {0: ".....AaA.", 1: ".....A.A."},
}
FREE_PAINT = {"cap", "bow"}                   # may paint outside the head

# Placement of each LOD's template inside a CELL x CELL cell (head centre at 6.5).
OFFSET = {"near": (2, 3), "mid": (3, 3), "far": (4, 4)}

# ---------------------------------------------------------------------------
# Palettes. Repeats weight a colour. Shades are derived (see shade()).
# ---------------------------------------------------------------------------
PALETTES = {
    "human_hair": [(28, 24, 26)] * 3 + [(52, 36, 30)] * 3 + [(78, 52, 38)] * 3 + [(110, 76, 50)] * 2 + [
        (150, 110, 72), (130, 60, 40), (176, 92, 52), (190, 160, 100), (220, 192, 128), (150, 148, 145),
        (214, 210, 202)],
    "human_skin": [(242, 206, 172), (224, 178, 140), (194, 140, 100), (150, 102, 72), (104, 70, 50)],
    "human_clothes": [(196, 194, 186), (54, 56, 62), (72, 86, 118), (118, 54, 52), (92, 100, 72),
                      (148, 122, 86), (62, 70, 92), (128, 96, 108)],
    "human_accessory": [(176, 64, 60), (66, 108, 156), (196, 168, 86), (214, 212, 204), (44, 44, 48)],
    "alien_skin": [(70, 110, 240), (240, 96, 190), (150, 80, 220), (60, 210, 220), (140, 230, 70)],
    "alien_tip": [(255, 240, 90), (255, 255, 255), (255, 120, 60)],
    "alien_suit": [(170, 176, 190), (120, 126, 140)],
    "glasses": [(22, 22, 26), (140, 156, 186)],  # glasses, glint
}
LIGHT, DARK, CLOTHES_DARKEN = 0.3, -0.35, -0.15


def shade(c, k):
    return tuple(max(0, min(255, int(v + (255 - v) * k if k > 0 else v * (1 + k)))) for v in c)


# ---------------------------------------------------------------------------
# Variants
# ---------------------------------------------------------------------------
def variants():
    out = [("human", s, a) for s in HUMAN_NEAR for a in ACCESSORIES]
    out += [("alien", s, "none") for s in ALIEN_NEAR]
    return out


def overlay(rows, acc):
    if acc == "none":
        return rows
    g = [list(r) for r in rows]
    h, w = len(g), len(g[0])
    spec = ACC_OVERLAY[acc]
    if spec == "ring":
        for y in range(0, 4):
            for x in range(w):
                if g[y][x] != "." and any(not (0 <= x + dx < w and 0 <= y + dy < h) or g[y + dy][x + dx] == "."
                                          for dx, dy in ((1, 0), (-1, 0), (0, -1))):
                    g[y][x] = "A"
    else:
        for y, row in spec.items():
            for x, ch in enumerate(row):
                if ch != "." and (acc in FREE_PAINT or g[y][x] != "."):
                    g[y][x] = ch
    return ["".join(r) for r in g]


def template(species, style, acc, lod):
    """ASCII rows for one variant at one LOD."""
    if lod == "near":
        head = HUMAN_NEAR[style] if species == "human" else ALIEN_NEAR[style]
        head = overlay(head, acc)
        return ["".join(h if h != "." else s for h, s in zip(hr, sr)) for hr, sr in zip(head, SHOULDERS)]
    if lod == "mid":
        if species == "alien":
            return ALIEN_MID[style]
        if acc == "cap":  # a cap wearer reads as a cap-coloured head
            return [r.replace("H", "A").replace("L", "A").replace("D", "a") for r in HUMAN_MID["short"]]
        return HUMAN_MID[style]
    # far: one colour circle - cap colour, skin for bald heads, else hair
    if species == "human" and acc == "cap":
        ch = "A"
    elif species == "human" and style in ("bald", "mohawk"):
        ch = "S"
    else:
        ch = "H"
    return [r.replace("X", ch) for r in FAR]


# ---------------------------------------------------------------------------
# Minimal PNG I/O (no third-party dependencies, like make_placeholder_atlas.py)
# ---------------------------------------------------------------------------
def write_png(path, width, height, pixels):
    """pixels: list of rows of (r, g, b, a)."""
    raw = b"".join(b"\x00" + b"".join(struct.pack("BBBB", *p) for p in row) for row in pixels)

    def chunk(tag, data):
        return struct.pack(">I", len(data)) + tag + data + struct.pack(">I", zlib.crc32(tag + data) & 0xFFFFFFFF)

    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", width, height, 8, 6, 0, 0, 0))
                     + chunk(b"IDAT", zlib.compress(raw, 9)) + chunk(b"IEND", b""))


def read_png(path):
    """8-bit, non-interlaced PNGs: greyscale(+alpha), RGB(A) or palette. Returns (w, h, rows of RGBA)."""
    data = path.read_bytes()
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        sys.exit(f"{path}: not a PNG")
    pos, idat, plte, trns = 8, b"", None, None
    while pos < len(data):
        length, tag = struct.unpack(">I4s", data[pos:pos + 8])
        body = data[pos + 8:pos + 8 + length]
        pos += 12 + length
        if tag == b"IHDR":
            w, h, depth, ctype, _, _, interlace = struct.unpack(">IIBBBBB", body)
        elif tag == b"PLTE":
            plte = [tuple(body[i:i + 3]) for i in range(0, len(body), 3)]
        elif tag == b"tRNS":
            trns = body
        elif tag == b"IDAT":
            idat += body
    if depth != 8 or interlace:
        sys.exit(f"{path}: only 8-bit non-interlaced PNGs are supported (re-export as RGBA 8-bit)")
    channels = {0: 1, 2: 3, 3: 1, 4: 2, 6: 4}[ctype]
    stride = w * channels
    raw = zlib.decompress(idat)
    rows, prev = [], bytearray(stride)
    for y in range(h):
        f = raw[y * (stride + 1)]
        line = bytearray(raw[y * (stride + 1) + 1:(y + 1) * (stride + 1)])
        for i in range(stride):
            a = line[i - channels] if i >= channels else 0
            b = prev[i]
            c = prev[i - channels] if i >= channels else 0
            if f == 1:
                line[i] = (line[i] + a) & 255
            elif f == 2:
                line[i] = (line[i] + b) & 255
            elif f == 3:
                line[i] = (line[i] + (a + b) // 2) & 255
            elif f == 4:
                p = a + b - c
                pa, pb, pc = abs(p - a), abs(p - b), abs(p - c)
                line[i] = (line[i] + (a if pa <= pb and pa <= pc else b if pb <= pc else c)) & 255
        prev = line
        row = []
        for x in range(w):
            px = line[x * channels:(x + 1) * channels]
            if ctype == 6:
                row.append(tuple(px))
            elif ctype == 2:
                row.append((*px, 255))
            elif ctype == 4:
                row.append((px[0], px[0], px[0], px[1]))
            elif ctype == 0:
                row.append((px[0], px[0], px[0], 255))
            else:
                alpha = trns[px[0]] if trns and px[0] < len(trns) else 255
                row.append((*plte[px[0]], alpha))
        rows.append(row)
    return w, h, rows


# ---------------------------------------------------------------------------
def layout():
    vs = variants()
    rows_per_lod = (len(vs) + ATLAS_COLUMNS - 1) // ATLAS_COLUMNS
    return vs, rows_per_lod, ATLAS_COLUMNS * CELL, rows_per_lod * CELL * len(LODS)


def cell_origin(v, lod, rows_per_lod):
    return ((v % ATLAS_COLUMNS) * CELL, (LODS.index(lod) * rows_per_lod + v // ATLAS_COLUMNS) * CELL)


def bootstrap():
    vs, rows_per_lod, w, h = layout()
    px = [[(0, 0, 0, 0)] * w for _ in range(h)]
    for v, (species, style, acc) in enumerate(vs):
        keys = KEY_HUMAN if species == "human" else {**KEY_HUMAN, **KEY_ALIEN}
        for lod in LODS:
            cx, cy = cell_origin(v, lod, rows_per_lod)
            ox, oy = OFFSET[lod]
            for y, row in enumerate(template(species, style, acc, lod)):
                for x, ch in enumerate(row):
                    if ch != ".":
                        px[cy + oy + y][cx + ox + x] = keys[CHAR_ROLE[ch]] + (255,)
    write_png(SHEET, w, h, px)
    print(f"wrote {SHEET.relative_to(ROOT)} ({w}x{h})")


def build():
    vs, rows_per_lod, w, h = layout()
    if not SHEET.exists():
        sys.exit(f"{SHEET.relative_to(ROOT)} is missing; run with --bootstrap first")
    sw, sh, sheet = read_png(SHEET)
    if (sw, sh) != (w, h):
        sys.exit(f"sheet is {sw}x{sh}, expected {w}x{h} for {len(vs)} variants (did the variant list change? "
                 f"re-bootstrap with --force)")
    colour_role = {}
    for keys in (KEY_HUMAN, KEY_ALIEN):
        for role, c in keys.items():
            colour_role[c] = ROLE[role]
    errors = []
    out = [[(0, 0, 0, 0)] * w for _ in range(h)]
    for y in range(h):
        for x in range(w):
            r, g, b, a = sheet[y][x]
            if a == 0:
                continue
            if a != 255:
                errors.append(f"({x},{y}) is semi-transparent (alpha {a}); pixels must be opaque or clear")
            elif (r, g, b) not in colour_role:
                errors.append(f"({x},{y}) colour {(r, g, b)} is not a key colour")
            else:
                out[y][x] = (colour_role[(r, g, b)], 0, 0, 255)
    if errors:
        for e in errors[:20]:
            print("error:", e)
        sys.exit(f"{len(errors)} bad pixel(s) in {SHEET.relative_to(ROOT)}")
    write_png(ATLAS, w, h, out)
    write_headers(vs, rows_per_lod, w, h)
    print(f"wrote {ATLAS.relative_to(ROOT)} ({w}x{h}), {CPP_OUT.relative_to(ROOT)}, {HLSL_OUT.relative_to(ROOT)}")


def palette_table():
    """Flattened float4 palette + offsets. Hair-like entries are (base, light, dark)."""
    entries, offsets = [], {}

    def add(name, colours, shades):
        offsets[name] = len(entries)
        for c in colours:
            for k in shades:
                entries.append(c if k == 0 else shade(c, k))

    add("HUMAN_HAIR", PALETTES["human_hair"], (0, LIGHT, DARK))
    add("HUMAN_SKIN", PALETTES["human_skin"], (0, LIGHT))
    add("HUMAN_CLOTHES", PALETTES["human_clothes"], (CLOTHES_DARKEN,))
    add("HUMAN_ACCESSORY", PALETTES["human_accessory"], (0, DARK))
    add("ALIEN_SKIN", PALETTES["alien_skin"], (0, LIGHT, DARK))
    add("ALIEN_TIP", PALETTES["alien_tip"], (0,))
    add("ALIEN_SUIT", PALETTES["alien_suit"], (0,))
    add("GLASSES", PALETTES["glasses"], (0,))
    return entries, offsets


def write_headers(vs, rows_per_lod, w, h):
    entries, offsets = palette_table()
    human_styles = list(HUMAN_NEAR)
    alien_styles = list(ALIEN_NEAR)
    acc_table = [i for i, n in enumerate(ACCESSORY_WEIGHTS) for _ in range(n)]
    counts = {
        "HUMAN_HAIR_COUNT": len(PALETTES["human_hair"]), "HUMAN_SKIN_COUNT": len(PALETTES["human_skin"]),
        "HUMAN_CLOTHES_COUNT": len(PALETTES["human_clothes"]),
        "HUMAN_ACCESSORY_COUNT": len(PALETTES["human_accessory"]),
        "ALIEN_SKIN_COUNT": len(PALETTES["alien_skin"]), "ALIEN_TIP_COUNT": len(PALETTES["alien_tip"]),
        "ALIEN_SUIT_COUNT": len(PALETTES["alien_suit"]),
    }
    banner = "Generated by tools/make_unit_atlas.py - do not edit; edit the tool or assets/art/unit_sheet.png."

    hl = [f"// {banner}", "#ifndef HORDE_UNIT_SPRITES_HLSLI", "#define HORDE_UNIT_SPRITES_HLSLI", ""]
    hl += [f"static const uint ATLAS_COLUMNS = {ATLAS_COLUMNS};", f"static const uint ROWS_PER_LOD = {rows_per_lod};",
           f"static const uint CELL = {CELL};", f"static const float2 ATLAS_SIZE = float2({w}.0f, {h}.0f);",
           f"static const uint HUMAN_STYLE_COUNT = {len(human_styles)};",
           f"static const uint ACCESSORY_COUNT = {len(ACCESSORIES)};",
           f"static const uint ALIEN_STYLE_COUNT = {len(alien_styles)};",
           f"static const uint ALIEN_VARIANT_BASE = {len(human_styles) * len(ACCESSORIES)};",
           f"static const uint ACCESSORY_TABLE_SIZE = {len(acc_table)};",
           "static const uint ACCESSORY_TABLE[" + str(len(acc_table)) + "] = {" + ", ".join(map(str, acc_table)) + "};",
           "// Head diameter in sprite pixels, and the square drawn around the cell centre, per LOD."]
    hl += ["static const float LOD_HEAD[3] = {" + ", ".join(f"{HEAD[l]}.0f" for l in LODS) + "};",
           "static const float LOD_WINDOW[3] = {" + ", ".join(f"{WINDOW[l]}.0f" for l in LODS) + "};", ""]
    hl += [f"static const uint {k} = {v};" for k, v in counts.items()]
    hl += [f"static const uint PAL_{k} = {v};" for k, v in offsets.items()]
    hl += [f"static const uint PALETTE_SIZE = {len(entries)};", ""]
    hl += [f"static const uint ROLE_{n.upper()} = {i};" for i, n in enumerate(ROLES)]
    hl += ["", "#endif", ""]
    HLSL_OUT.write_text("\n".join(hl), newline="\n")

    cp = [f"// {banner}", "#pragma once", "", "#include <array>", "", "#include <glm/glm.hpp>", "",
          "namespace horde::gfx::unit_sprites {", "",
          f"inline constexpr int kAtlasWidth = {w};", f"inline constexpr int kAtlasHeight = {h};",
          f"inline constexpr int kVariantCount = {len(vs)};",
          f"inline constexpr int kPaletteSize = {len(entries)};", "",
          "// Matches `float4 Palette[PALETTE_SIZE]` in unit_sprite.frag.hlsl, linear 0-1 sRGB values.",
          "inline constexpr std::array<glm::vec4, kPaletteSize> kPalette{{"]
    cp += [f"    {{{r / 255:.4f}f, {g / 255:.4f}f, {b / 255:.4f}f, 1.0f}}," for r, g, b in entries]
    cp += ["}};", "", "} // namespace horde::gfx::unit_sprites", ""]
    CPP_OUT.write_text("\n".join(cp), newline="\n")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--bootstrap", action="store_true", help="write the authoring sheet from the built-in templates")
    ap.add_argument("--force", action="store_true", help="allow --bootstrap to overwrite an existing sheet")
    args = ap.parse_args()
    if args.bootstrap:
        if SHEET.exists() and not args.force:
            sys.exit(f"{SHEET.relative_to(ROOT)} exists; pass --force to overwrite your edits")
        bootstrap()
    build()


if __name__ == "__main__":
    main()
