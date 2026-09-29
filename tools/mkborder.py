#!/usr/bin/env python3
"""Reword the Super Game Boy border art: art/border_sgb.png, edited in place.

The PNG is the source of truth for the border -- `make border` turns it into
border_data.c -- so changing the wording means changing that PNG.  This clears
the two text lines and redraws them, harvesting the letterforms from the art
itself: the redraw is in the art's own hand rather than a new font invented
here.  The one exception is 'V', which the art had no use for until "SAVE THE
HUMANS"; that bitmap is pinned below, taken from tools/mktitle.py's font, whose
letters this art's match exactly (compare E, N, M, S, A).

Each line is 7 rows of a 5x7 font: the title at 2x (12 px pitch), the subtitle
at 1x (6 px pitch), both centred on the 256-wide art, one blank cell for a
space.  Only pixels of the line's own colour are cleared, so the starfield
behind the text survives -- and re-running this is safe, because by then the
art holds every letter both lines need.

    tools/mkborder.py && make border
"""
from PIL import Image

ART = "art/border_sgb.png"
BG = (8, 8, 24, 255)                    # the art's navy background

# (top row, scale, colour, text) -- the lines, in the order they appear.
LINES = [
    (5, 2, (240, 90, 60, 255), "GB PROTECTOR"),
    (25, 1, (200, 210, 230, 255), "SAVE THE HUMANS"),
]
# What those lines say now: this is both what gets cleared and where the
# letters are harvested from.
OLD = ["GB-PROTECTOR", "DEFEND THE HUMANS"]

V = ["#...#", "#...#", "#...#", "#...#", "#...#", ".#.#.", "..#.."]

CELL, GLYPH = 6, 5                      # pen advance, and ink width, at 1x


def line_w(s, scale):
    return (GLYPH + (len(s) - 1) * CELL) * scale


def line_x(s, scale, width):
    """Left edge: centred, floored -- which is where the existing text sits."""
    return (width - line_w(s, scale)) // 2


def read_glyph(px, x, y, scale, col):
    """The 5x7 bitmap at (x, y), sampled back down from whatever scale."""
    return ["".join("#" if px[x + c * scale, y + r * scale] == col else "."
                    for c in range(GLYPH)) for r in range(7)]


im = Image.open(ART).convert("RGBA")
px = im.load()
W = im.size[0]

font = {}
for (y, scale, col, _new), old in zip(LINES, OLD):
    x = line_x(old, scale, W)
    for ch in old:
        if ch != " ":
            font.setdefault(ch, read_glyph(px, x, y, scale, col))
        x += CELL * scale
font["V"] = V

for (y, scale, col, new), old in zip(LINES, OLD):
    x = line_x(old, scale, W)                   # clear what is there now
    for _ in old:
        for r in range(7 * scale):
            for c in range(GLYPH * scale):
                if px[x + c, y + r] == col:
                    px[x + c, y + r] = BG
        x += CELL * scale

    x = line_x(new, scale, W)                   # then draw the new line
    for ch in new:
        if ch != " ":
            for r, row in enumerate(font[ch]):
                for c, bit in enumerate(row):
                    if bit == "#":
                        for dy in range(scale):
                            for dx in range(scale):
                                px[x + c * scale + dx, y + r * scale + dy] = col
        x += CELL * scale

im.save(ART)
for (y, scale, col, new) in LINES:
    print("%2d  %-16s %3d px, x %d..%d"
          % (y, new, line_w(new, scale), line_x(new, scale, W),
             line_x(new, scale, W) + line_w(new, scale) - 1))
