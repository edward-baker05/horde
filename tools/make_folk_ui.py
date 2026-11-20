#!/usr/bin/env python3
"""Draw the Polish folk art used by the phone UI into assets/ui/folk/.

rosette.png  a Lowicz-style wycinanki paper-cut flower, layered colour on black
pasiak.png   a strip of Lowicz striped wool (pasiak), tiled across the screen
paper.png    faint cut-paper flowers on cream, sized to cover the white band
tulips.png   a folk border of tulips and leaves, used as a divider
bird_l.png   a wycinanki bird (ptaszek) facing left; bird_r.png is its mirror
scallop.png  a strip of paper scallops, the cut edge of the white band

Drawn 4x oversize and downsampled so edges are smooth. Needs Pillow.
"""

import math
from pathlib import Path

from PIL import Image, ImageDraw

OUT = Path(__file__).resolve().parent.parent / "assets" / "ui" / "folk"
SS = 4  # supersampling factor

# Lowicz wycinanki palette: bright layered papers.
RED = (214, 33, 61)
MAGENTA = (194, 24, 111)
ORANGE = (240, 122, 20)
YELLOW = (246, 190, 0)
GREEN = (31, 133, 66)
LIME = (130, 190, 40)
BLUE = (29, 63, 158)
VIOLET = (98, 52, 150)
BLACK = (20, 16, 18)
CREAM = (247, 240, 225)


def petal(draw, cx, cy, angle, r0, r1, width, colour):
    """Pointed teardrop from radius r0 to r1 along `angle`, `width` at its widest."""
    pts = []
    steps = 24
    for i in range(steps + 1):
        t = i / steps
        r = r0 + (r1 - r0) * t
        w = width * math.sin(math.pi * t) ** 0.8 * (1 - 0.35 * t)
        pts.append((r, w))
    outline = [(r, w) for r, w in pts] + [(r, -w) for r, w in reversed(pts)]
    ca, sa = math.cos(angle), math.sin(angle)
    poly = [(cx + r * ca - w * sa, cy + r * sa + w * ca) for r, w in outline]
    draw.polygon(poly, fill=colour)


def disc(draw, cx, cy, r, colour):
    draw.ellipse((cx - r, cy - r, cx + r, cy + r), fill=colour)


def ring_of(n, offset, fn):
    for i in range(n):
        fn(offset + 2 * math.pi * i / n)


def rosette(size):
    s = size * SS
    img = Image.new("RGBA", (s, s), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    c = s / 2
    u = s / 2  # unit: radius of the whole cut-out

    # Black backing paper: scalloped disc, as wycinanki are cut from one sheet.
    ring_of(24, 0, lambda a: disc(d, c + 0.88 * u * math.cos(a), c + 0.88 * u * math.sin(a), 0.11 * u, BLACK))
    disc(d, c, c, 0.9 * u, BLACK)

    # Outer leaves and between them tulip-like red petals.
    ring_of(12, 0, lambda a: petal(d, c, c, a, 0.42 * u, 0.95 * u, 0.13 * u, GREEN))
    ring_of(12, math.pi / 12, lambda a: petal(d, c, c, a, 0.40 * u, 0.86 * u, 0.12 * u, RED))
    ring_of(12, math.pi / 12, lambda a: petal(d, c, c, a, 0.46 * u, 0.74 * u, 0.06 * u, YELLOW))
    ring_of(12, 0, lambda a: petal(d, c, c, a, 0.50 * u, 0.80 * u, 0.05 * u, LIME))

    # Dots between the petal tips.
    ring_of(12, math.pi / 12, lambda a: disc(d, c + 0.92 * u * math.cos(a), c + 0.92 * u * math.sin(a), 0.035 * u, ORANGE))

    # Inner layers, smallest on top, as stacked paper.
    disc(d, c, c, 0.48 * u, MAGENTA)
    ring_of(16, 0, lambda a: petal(d, c, c, a, 0.18 * u, 0.50 * u, 0.07 * u, ORANGE))
    disc(d, c, c, 0.30 * u, BLUE)
    ring_of(8, math.pi / 8, lambda a: petal(d, c, c, a, 0.08 * u, 0.30 * u, 0.08 * u, VIOLET))
    ring_of(8, 0, lambda a: petal(d, c, c, a, 0.06 * u, 0.26 * u, 0.06 * u, YELLOW))
    disc(d, c, c, 0.11 * u, RED)
    disc(d, c, c, 0.06 * u, CREAM)
    ring_of(8, 0, lambda a: disc(d, c + 0.38 * u * math.cos(a), c + 0.38 * u * math.sin(a), 0.03 * u, CREAM))

    return img.resize((size, size), Image.LANCZOS)


def pasiak(width, height):
    # One repeat of a Lowicz stripe: a wide ground colour with thin bands.
    pattern = [
        (ORANGE, 10), (BLACK, 2), (GREEN, 4), (BLACK, 2), (YELLOW, 3), (BLACK, 2),
        (MAGENTA, 8), (BLACK, 2), (BLUE, 4), (BLACK, 2), (LIME, 3), (BLACK, 2),
        (RED, 10), (BLACK, 2), (VIOLET, 4), (BLACK, 2), (YELLOW, 3), (BLACK, 2),
    ]
    img = Image.new("RGBA", (width, height), BLACK + (255,))
    d = ImageDraw.Draw(img)
    x = 0
    while x < width:
        for colour, w in pattern:
            d.rectangle((x, 0, x + w - 1, height - 1), fill=colour)
            x += w
    return img


def small_flower(d, cx, cy, r, petal_colour, centre_colour, petals=8):
    ring_of(petals, 0, lambda a: petal(d, cx, cy, a, 0.15 * r, r, 0.32 * r, petal_colour))
    disc(d, cx, cy, 0.28 * r, centre_colour)


def paper(width, height, cell):
    # A whole image rather than a tile: the RmlUi renderer clamps textures, so
    # decorator repeats do not tile. Flowers on a diagonal grid, dots between.
    w, h, c = width * SS, height * SS, cell * SS
    ground = (247, 240, 225)
    ink = (238, 226, 203)
    img = Image.new("RGBA", (w, h), ground + (255,))
    d = ImageDraw.Draw(img)
    for row in range(int(h / c) + 2):
        for col in range(int(w / c) + 2):
            x, y = col * c, row * c
            if (row + col) % 2 == 0:
                small_flower(d, x, y, c * 0.26, ink, ground)
            else:
                disc(d, x, y, c * 0.05, ink)
    return img.resize((width, height), Image.LANCZOS)


def tulip(d, cx, base, h, colour, inner):
    # Three-lobed folk tulip on a short stem.
    w = h * 0.55
    top = base - h
    d.rectangle((cx - h * 0.04, base - h * 0.45, cx + h * 0.04, base), fill=GREEN)
    d.ellipse((cx - w / 2, top + h * 0.25, cx + w / 2, top + h * 0.75), fill=colour)
    for side in (-1, 1):
        petal(d, cx + side * w * 0.28, top + h * 0.62, -math.pi / 2 + side * 0.35, 0, h * 0.62, w * 0.22, colour)
    petal(d, cx, top + h * 0.62, -math.pi / 2, 0, h * 0.62, w * 0.24, colour)
    petal(d, cx, top + h * 0.62, -math.pi / 2, h * 0.05, h * 0.45, w * 0.1, inner)


def tulips(width, height):
    w, h = width * SS, height * SS
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    base = h * 0.92
    d.rounded_rectangle((0, base - h * 0.04, w, base + h * 0.04), radius=h * 0.04, fill=BLACK)
    count = 9
    gap = w / count
    colours = [(RED, YELLOW), (MAGENTA, ORANGE), (BLUE, YELLOW)]
    for i in range(count):
        cx = gap * (i + 0.5)
        colour, inner = colours[i % 3] if i != count // 2 else (RED, YELLOW)
        scale = 1.0 if i == count // 2 else 0.78
        # Leaves either side, then the flower over them.
        for side in (-1, 1):
            petal(d, cx, base, -math.pi / 2 + side * 0.9, 0, h * 0.5 * scale, h * 0.09 * scale, GREEN)
        tulip(d, cx, base - h * 0.08, h * 0.8 * scale, colour, inner)
        # Dots between flowers.
        if i < count - 1:
            disc(d, cx + gap / 2, base - h * 0.22, h * 0.05, ORANGE)
    return img.resize((width, height), Image.LANCZOS)


def bird(size):
    # A Kurpie/Lowicz style bird, facing right: layered papers on a black cut.
    w, h = size[0] * SS, size[1] * SS
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)

    def layer(grow, colours):
        body, tail, wing, head = colours
        # Tail fan, behind the body, sweeping up and back.
        for k, a in enumerate((-2.2, -2.55, -2.9)):
            petal(d, w * 0.36, h * 0.58, a, 0, h * 0.42 + grow, h * 0.13 + grow, tail[k])
        d.ellipse((w * 0.22 - grow, h * 0.42 - grow, w * 0.78 + grow, h * 0.88 + grow), fill=body)
        # Neck and head.
        d.ellipse((w * 0.62 - grow, h * 0.16 - grow, w * 0.88 + grow, h * 0.46 + grow), fill=head)
        petal(d, w * 0.70, h * 0.48, -1.2, 0, h * 0.22 + grow, h * 0.1 + grow, head)
        # Crest.
        for a in (-1.9, -1.55, -1.2):
            petal(d, w * 0.75, h * 0.2, a, 0, h * 0.13 + grow, h * 0.04 + grow, wing if grow == 0 else head)
        # Beak.
        d.polygon([(w * 0.86, h * 0.26 - grow), (w * 0.99 + grow, h * 0.32), (w * 0.86, h * 0.38 + grow)], fill=tail[2] if grow == 0 else head)
        # Legs.
        for x in (0.45, 0.58):
            d.rectangle((w * x - h * 0.015 - grow, h * 0.85, w * x + h * 0.015 + grow, h * 0.99), fill=BLACK)

    layer(h * 0.035, (BLACK, (BLACK, BLACK, BLACK), BLACK, BLACK))
    layer(0, (RED, (BLUE, MAGENTA, ORANGE), YELLOW, RED))
    # Wing: green leaf with a yellow vein and dots, as cut in layers.
    petal(d, w * 0.34, h * 0.62, -0.25, 0, w * 0.4, h * 0.13, GREEN)
    petal(d, w * 0.38, h * 0.62, -0.25, 0, w * 0.3, h * 0.05, YELLOW)
    for k in range(3):
        disc(d, w * (0.33 + 0.1 * k), h * 0.78, h * 0.03, CREAM)
    # Eye.
    disc(d, w * 0.77, h * 0.29, h * 0.055, CREAM)
    disc(d, w * 0.78, h * 0.29, h * 0.028, BLACK)
    return img.resize(size, Image.LANCZOS)


def scallop(width, height, count):
    # The full strip, for the same reason as paper(): no texture repeat.
    w, h = width * SS, height * SS
    img = Image.new("RGBA", (w, h), (0, 0, 0, 0))
    d = ImageDraw.Draw(img)
    step = w / count
    for i in range(count):
        d.ellipse((i * step, -h, (i + 1) * step, h), fill=(247, 240, 225))
    return img.resize((width, height), Image.LANCZOS)


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    rosette(512).save(OUT / "rosette.png", optimize=True)
    pasiak(780, 8).save(OUT / "pasiak.png", optimize=True)
    paper(780, 720, 96).save(OUT / "paper.png", optimize=True)
    tulips(720, 72).save(OUT / "tulips.png", optimize=True)
    right = bird((200, 180))
    right.save(OUT / "bird_r.png", optimize=True)
    right.transpose(Image.FLIP_LEFT_RIGHT).save(OUT / "bird_l.png", optimize=True)
    scallop(780, 24, 30).save(OUT / "scallop.png", optimize=True)
    print(f"wrote {OUT}")


if __name__ == "__main__":
    main()
