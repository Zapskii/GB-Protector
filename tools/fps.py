#!/usr/bin/env python3
"""Frame-rate check for GB PROTECTOR.

The game must update once per vblank (60 fps).  render() runs BEFORE
wait_vbl_done(), so anything that pushes update+render past the frame budget
silently halves the game -- it did exactly that once, and make test / make
probe could not see it.  This reads the game's own frame counter (incremented
once per update) out of RAM each emulated frame: 1.0 frames/tick is a green
tree, 0.5 means every frame slips.

    make fps               # or: tools/fps.py protector.gb protector.map

DEV TOOL.  Needs the -debug map, hence the `sym` dependency.
"""
import re
import sys

from pyboy import PyBoy


def parse_frame_addr(path):
    for line in open(path):
        m = re.match(r"\s*([0-9A-Fa-f]{8})\s+Fmain\$frame\$", line)
        if m:
            return int(m.group(1), 16)
    sys.exit("fps: `frame` missing from %s -- run `make sym`" % path)


rom, mapfile = sys.argv[1], sys.argv[2]
addr = parse_frame_addr(mapfile)
p = PyBoy(rom, window="null", sound_emulated=False)

prev, incs, ticks = None, 0, 0
for t in range(700):                        # ~12 s: a few spawns, some action
    p.tick(1, False)
    f = p.memory[addr]
    if t >= 120:                            # let new_game() settle
        if prev is not None:
            incs += (f - prev) & 255
        ticks += 1
    prev = f

rate = incs / ticks
print("%.3f frames/tick => ~%.0f fps" % (rate, rate * 60))
if rate < 0.98:
    sys.exit("fps: frame budget blown -- update+render no longer fits in vblank")