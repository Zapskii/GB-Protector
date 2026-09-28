#!/usr/bin/env python3
"""Procedural tiles and sprites for GB-Protector -> gfx.h

Every tile is drawn as ASCII: '.' = colour 0, '1' = light, '2' = dark,
'#' = black.  No dependencies, so `python3 mkgfx.py` runs anywhere.
gfx.h is committed so a plain `make` needs no Python at all.
"""

TILES = []          # list of 16-byte tiles, index == tile number
NAMES = []          # (define name, tile number)


def enc(rows):
    """8 rows of 8 chars -> 16 bytes of 2bpp tile data."""
    assert len(rows) == 8, rows
    out = []
    for r in rows:
        assert len(r) == 8, r
        lo = hi = 0
        for ch in r:
            c = {'.': 0, '1': 1, '2': 2, '#': 3}[ch]
            lo = (lo << 1) | (c & 1)
            hi = (hi << 1) | (c >> 1)
        out += [lo, hi]
    return out


def put(index, rows, name=None):
    while len(TILES) <= index:
        TILES.append(enc(["........"] * 8))
    TILES[index] = enc(rows)
    if name:
        NAMES.append((name, index))


BLANK = ["........"] * 8

# ---------------------------------------------------------------- BG tiles
put(0, BLANK, "T_BLANK")
put(1, ["..#...#.",
        ".###.###",
        "########",
        "#2#2#2#2",
        "2#2#2#2#",
        "#2#2#2#2",
        "2#2#2#2#",
        "#2#2#2#2"], "T_SURFACE")
put(2, ["2#2#2#2#",
        "#2#2#2#2",
        "2#2#2#2#",
        "#2#2#2#2",
        "2#2#2#2#",
        "#2#2#2#2",
        "2#2#2#2#",
        "#2#2#2#2"], "T_FILL")
put(3, ["########"] + ["........"] * 7, "T_HUD_TOP")
put(4, ["........"] * 7 + ["########"], "T_HUD_BOT")

# ------------------------------------------------------------------- font
FONT = {
 '0': ".###.|#...#|#..##|#.#.#|##..#|#...#|.###.",
 '1': "..#..|.##..|..#..|..#..|..#..|..#..|.###.",
 '2': ".###.|#...#|....#|...#.|..#..|.#...|#####",
 '3': ".###.|#...#|....#|..##.|....#|#...#|.###.",
 '4': "...#.|..##.|.#.#.|#..#.|#####|...#.|...#.",
 '5': "#####|#....|####.|....#|....#|#...#|.###.",
 '6': ".###.|#....|#....|####.|#...#|#...#|.###.",
 '7': "#####|....#|...#.|..#..|.#...|.#...|.#...",
 '8': ".###.|#...#|#...#|.###.|#...#|#...#|.###.",
 '9': ".###.|#...#|#...#|.####|....#|...#.|.##..",
 'A': ".###.|#...#|#...#|#####|#...#|#...#|#...#",
 'B': "####.|#...#|#...#|####.|#...#|#...#|####.",
 'C': ".###.|#...#|#....|#....|#....|#...#|.###.",
 'D': "####.|#...#|#...#|#...#|#...#|#...#|####.",
 'E': "#####|#....|#....|####.|#....|#....|#####",
 'F': "#####|#....|#....|####.|#....|#....|#....",
 'G': ".###.|#...#|#....|#.###|#...#|#...#|.###.",
 'H': "#...#|#...#|#...#|#####|#...#|#...#|#...#",
 'I': ".###.|..#..|..#..|..#..|..#..|..#..|.###.",
 'J': "..###|...#.|...#.|...#.|...#.|#..#.|.##..",
 'K': "#...#|#..#.|#.#..|##...|#.#..|#..#.|#...#",
 'L': "#....|#....|#....|#....|#....|#....|#####",
 'M': "#...#|##.##|#.#.#|#.#.#|#...#|#...#|#...#",
 'N': "#...#|##..#|#.#.#|#..##|#...#|#...#|#...#",
 'O': ".###.|#...#|#...#|#...#|#...#|#...#|.###.",
 'P': "####.|#...#|#...#|####.|#....|#....|#....",
 'Q': ".###.|#...#|#...#|#...#|#.#.#|#..#.|.##.#",
 'R': "####.|#...#|#...#|####.|#.#..|#..#.|#...#",
 'S': ".####|#....|#....|.###.|....#|....#|####.",
 'T': "#####|..#..|..#..|..#..|..#..|..#..|..#..",
 'U': "#...#|#...#|#...#|#...#|#...#|#...#|.###.",
 'V': "#...#|#...#|#...#|#...#|#...#|.#.#.|..#..",
 'W': "#...#|#...#|#...#|#.#.#|#.#.#|##.##|#...#",
 'X': "#...#|#...#|.#.#.|..#..|.#.#.|#...#|#...#",
 'Y': "#...#|#...#|.#.#.|..#..|..#..|..#..|..#..",
 'Z': "#####|....#|...#.|..#..|.#...|#....|#####",
}


def glyph_rows(ch):
    rows = ["." + r.replace('#', '#') + ".." for r in FONT[ch].split('|')]
    return rows + ["........"]


FONT_DIGIT = 8
FONT_ALPHA = 18
for i, ch in enumerate("0123456789"):
    put(FONT_DIGIT + i, glyph_rows(ch))
for i, ch in enumerate("ABCDEFGHIJKLMNOPQRSTUVWXYZ"):
    put(FONT_ALPHA + i, glyph_rows(ch))
NAMES.append(("T_FONT_DIGIT", FONT_DIGIT))
NAMES.append(("T_FONT_ALPHA", FONT_ALPHA))

# ---------------------------------------------------------------- sprites
# Sprites are 8x16 (two tiles, top then bottom).  Sprite k lives at tile
# SPR_BASE + 2*k.  Art is given as 16 rows of 8 chars; missing rows blank.
SPR_BASE = 64
SPRITES = []


def sprite(name, rows):
    rows = list(rows) + ["........"] * (16 - len(rows))
    SPRITES.append((name, rows))


SHIP = [
    "......##........",
    "....######......",
    "#.##########2...",
    "################",
    "#.##########....",
    "....######......",
    "......##........",
    "................",
]
sprite("SPR_SHIP_L", [r[:8] for r in SHIP])
sprite("SPR_SHIP_R", [r[8:] for r in SHIP])
sprite("SPR_LANDER", [
    "..####..",
    ".######.",
    "##.##.##",
    "########",
    ".#.##.#.",
    "#..##..#",
    "#......#",
])
sprite("SPR_MUTANT", [
    "#..##..#",
    ".######.",
    "##.##.##",
    "########",
    ".##..##.",
    "#..##..#",
    "#.#..#.#",
    "..#..#..",
])
sprite("SPR_HUMAN", [
    "...##...",
    "..####..",
    "...##...",
    "..####..",
    ".#.##.#.",
    ".#.##.#.",
    "...##...",
    "..#..#..",
    "..#..#..",
    "..#..#..",
    "..#..#..",
    ".##..##.",
])
sprite("SPR_BULLET", ["####....", "####...."])
sprite("SPR_EBULLET", ["##......", "##......"])
sprite("SPR_EXP0", [
    "........",
    "...##...",
    "..####..",
    "..####..",
    "...##...",
])
sprite("SPR_EXP1", [
    "..#..#..",
    ".#.##.#.",
    "..####..",
    "#.####.#",
    "..####..",
    ".#.##.#.",
    "..#..#..",
])
sprite("SPR_EXP2", [
    "#..##..#",
    ".#....#.",
    "..#..#..",
    "##....##",
    "..#..#..",
    ".#....#.",
    "#..##..#",
])
sprite("SPR_EXP3", [
    "#......#",
    "...#.#..",
    "........",
    ".#....#.",
    "........",
    "...#.#..",
    "#......#",
])
sprite("SPR_BLIP_S", ["##......"])
sprite("SPR_BLIP_B", ["##......", "##......"])

for k, (name, rows) in enumerate(SPRITES):
    put(SPR_BASE + 2 * k, rows[:8])
    put(SPR_BASE + 2 * k + 1, rows[8:])
    NAMES.append((name, k))

NAMES.append(("SPR_BASE", SPR_BASE))

# ----------------------------------------------------------------- output
lines = [
    "/* Generated by mkgfx.py - do not edit.  Run `make gfx` to regenerate. */",
    "#ifndef GFX_H",
    "#define GFX_H",
    "",
    "#define GFX_TILE_COUNT %d" % len(TILES),
]
for name, val in NAMES:
    lines.append("#define %-14s %d" % (name, val))
lines += [
    "#define T_SPR(k)       (SPR_BASE + 2 * (k))",
    "",
    "static const unsigned char gfx_tiles[] = {",
]
for i, t in enumerate(TILES):
    lines.append("    " + ",".join("0x%02X" % b for b in t) + ",  /* %d */" % i)
lines += ["};", "", "#endif", ""]

with open("gfx.h", "w") as f:
    f.write("\n".join(lines))
print("gfx.h: %d tiles (%d bytes), %d sprites" %
      (len(TILES), len(TILES) * 16, len(SPRITES)))
