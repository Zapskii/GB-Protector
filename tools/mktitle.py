#!/usr/bin/env python3
"""Title-screen image for GB-Protector -> title.h

Draws the 160x144 screen as a value grid (0 = lightest ... 3 = darkest, which
is what BGP 0xE4 renders), then cuts it into 8x8 tiles, dedupes them and
emits title_tiles[] + a 20x18 title_map[].  Dependency-free, like mkgfx.py:
title.h is committed, so a plain `make` needs no Python.  What it looks like
is checked with tools/shot.py against the real ROM, not with a preview here.
"""

W, H = 160, 144

PIX = [[0] * W for _ in range(H)]        # value grid 0..3


def rect(x0, y0, x1, y1, v):             # inclusive-exclusive
    for y in range(y0, y1):
        for x in range(x0, x1):
            PIX[y][x] = v


def dither(x0, y0, x1, y1, a, b):
    for y in range(y0, y1):
        for x in range(x0, x1):
            PIX[y][x] = a if (x + y) % 2 == 0 else b


def blit(art, x, y, scale, colors):
    """art rows of chars -> scaled pixels; '.' leaves the background."""
    for r, row in enumerate(art):
        for c, ch in enumerate(row):
            if ch == '.':
                continue
            v = colors[ch]
            for dy in range(scale):
                for dx in range(scale):
                    PIX[y + r * scale + dy][x + c * scale + dx] = v


# The in-game art, straight out of mkgfx.py, so the title is the game's ship.
SHIP = [
    "......##........",
    "....######......",
    "#.##########2...",
    "################",
    "#.##########....",
    "....######......",
    "......##........",
]
LANDER = [
    "..####..",
    ".######.",
    "##.##.##",
    "########",
    ".#.##.#.",
    "#..##..#",
    "#......#",
]
HUMAN = [
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
]
# '.' transparent; art chars 1/2 keep their meaning (1 light, 2 dark, # black)
SHADE = {'1': 2, '2': 2, '#': 3}

FONT = {
 'A': ["01110", "10001", "10001", "11111", "10001", "10001", "10001"],
 'B': ["11110", "10001", "10001", "11110", "10001", "10001", "11110"],
 'C': ["01110", "10001", "10000", "10000", "10000", "10001", "01110"],
 'E': ["11111", "10000", "10000", "11110", "10000", "10000", "11111"],
 'G': ["01110", "10001", "10000", "10011", "10001", "10001", "01111"],
 'H': ["10001", "10001", "10001", "11111", "10001", "10001", "10001"],
 'M': ["10001", "11011", "10101", "10101", "10001", "10001", "10001"],
 'N': ["10001", "11001", "10101", "10011", "10001", "10001", "10001"],
 'O': ["01110", "10001", "10001", "10001", "10001", "10001", "01110"],
 'P': ["11110", "10001", "10001", "11110", "10000", "10000", "10000"],
 'R': ["11110", "10001", "10001", "11110", "10100", "10010", "10001"],
 'S': ["01111", "10000", "10000", "01110", "00001", "00001", "11110"],
 'T': ["11111", "00100", "00100", "00100", "00100", "00100", "00100"],
 'U': ["10001", "10001", "10001", "10001", "10001", "10001", "01110"],
 'V': ["10001", "10001", "10001", "10001", "10001", "01010", "00100"],
 ' ': ["000"] * 7,
}


def text(s, y, scale, fg, shadow):
    w = sum(len(FONT[ch][0]) * scale + scale for ch in s) - scale
    x = (W - w) // 2
    for ch in s:
        g = FONT[ch]
        for pass_n, col in ((0, shadow), (1, fg)):
            for r, row in enumerate(g):
                for c, bit in enumerate(row):
                    if bit == '1':
                        rect(x + c * scale + (scale if pass_n == 0 else 0),
                             y + r * scale + (scale if pass_n == 0 else 0),
                             x + (c + 1) * scale + (scale if pass_n == 0 else 0),
                             y + (r + 1) * scale + (scale if pass_n == 0 else 0),
                             col)
        x += len(g[0]) * scale + scale


# ------------------------------------------------------------------ scene
# Stars in the sky.
for sx, sy, v in [(14, 6, 2), (40, 14, 3), (68, 4, 2), (96, 10, 3), (124, 6, 2),
                  (148, 16, 3), (8, 30, 3), (150, 40, 2), (26, 44, 2),
                  (134, 28, 3)]:
    PIX[sy][sx] = v

text("GB PROTECTOR", 8, 2, 3, 2)
text("SAVE THE HUMANS", 30, 1, 2, 0)

# Big ship, centred, flying right -- the game's own hull, scaled, with panel
# seams and a dome glint so 25 px of solid black reads as a ship.
blit(SHIP, 40, 52, 5, SHADE)
rect(45, 67, 115, 69, 2)                # hull seam (row 3 is the widest)
rect(60, 82, 92, 84, 2)                 # lower seam, inside the narrowing hull
rect(70, 53, 75, 57, 1)                 # dome glint
for ex in range(20, 38, 6):             # exhaust puffs behind the tail
    rect(ex, 70, ex + 3, 72, 2)

# A lander at work on the right, its beam holding a human mid-lift.
blit(LANDER, 122, 34, 3, SHADE)
for bx in (128, 133, 138):              # dithered tractor beam
    for y in range(55, 112):
        if (y & 3) != 3:
            PIX[y][bx] = 2 if (bx + y) & 1 else 3
blit(HUMAN, 124, 80, 2, SHADE)

# Terrain, same checker language as the in-game ground.
dither(0, 111, W, 112, 0, 2)
for x in range(W):
    PIX[112][x] = 3 if x % 2 == 0 else 2
dither(0, 113, W, H, 2, 3)

text("PRESS START", 124, 1, 0, 2)

# ------------------------------------------------------------------ emit
def enc(t):                              # 8x8 values -> 16 bytes
    out = []
    for r in range(8):
        lo = hi = 0
        for c in range(8):
            v = t[r][c]
            lo = (lo << 1) | (v & 1)
            hi = (hi << 1) | (v >> 1)
        out += [lo, hi]
    return out


tiles, tmap, seen = [], [], {}
for ty in range(H // 8):
    for tx in range(W // 8):
        t = [PIX[ty * 8 + r][tx * 8:(tx + 1) * 8] for r in range(8)]
        key = tuple(map(tuple, t))
        if key not in seen:
            seen[key] = len(tiles)
            tiles.append(enc(t))
        tmap.append(seen[key])

lines = [
    "/* Generated by tools/mktitle.py - do not edit.  Run `make title`. */",
    "#ifndef TITLE_H",
    "#define TITLE_H",
    "",
    "#define TITLE_TILE_COUNT %d" % len(tiles),
    "",
    "static const unsigned char title_tiles[] = {",
]
for i, t in enumerate(tiles):
    lines.append("    " + ",".join("0x%02X" % b for b in t) + ",  /* %d */" % i)
lines.append("};")
lines.append("")
lines.append("static const unsigned char title_map[] = {  /* 20x18 */")
for ty in range(H // 8):
    row = tmap[ty * 20:(ty + 1) * 20]
    lines.append("    " + ",".join("%3d" % v for v in row) + ",")
lines += ["};", "", "#endif", ""]

with open("title.h", "w") as f:
    f.write("\n".join(lines))
print("title.h: %d tiles (%d bytes)" % (len(tiles), len(tiles) * 16))
assert len(tiles) <= 256, "over VRAM budget"