#!/usr/bin/env python3
"""Headless play-testing harness for GB PROTECTOR.

WHY THIS EXISTS: tools/shot.py takes a picture, which only tells you the screen
is not blank. This drives the game and reads the game's OWN variables out of the
emulator, so a rule can be asserted -- "a lander that grabs a human marks it
HELD", "shooting the carrier drops it, and a long drop kills it" -- instead of
eyeballed. It is the emulator counterpart to tests/test_sim.c: that one covers
the maths in sim.h, this one covers the wiring between the maths and main.c.

The variable addresses are PARSED FROM protector.map rather than hardcoded, so
adding or reordering a global cannot silently make this read the wrong byte.
Run `make sym` first (it is also what the debuggers want).

    make probe            # or: tools/probe.py protector.gb protector.map

This is a DEV TOOL. Nothing in the build depends on it.
"""
import re
import sys

from pyboy import PyBoy

KEYS = {"R": "right", "L": "left", "U": "up", "D": "down",
        "A": "a", "B": "b", "S": "start", "T": "select"}

# Layout constants, straight out of sim.h / main.c.
WORLD_W = 1024                          # torus width
MAX_ENEMY = 8
MAX_HUMAN = 6
EN_STRIDE = 5                           # Lander: x(u16), y, state, tick
HUM_STRIDE = 5                          # Target: x(u16), y, state, aux
FALL_SAFE = 40                          # a longer drop kills, in sim.h
RESCUE_BONUS = 500                      # paid for setting a caught human down

EN_SEEK, EN_GRAB, EN_CARRY, EN_MUTANT = 0, 1, 2, 3
HUM_GROUND, HUM_HELD, HUM_FALL, HUM_DEAD, HUM_CARRIED = 0, 1, 2, 3, 4
ST_PLAY, ST_DYING, ST_OVER, ST_TITLE = 0, 1, 2, 3

# The game seeds its RNG from DIV_REG -- elapsed cycles -- so the whole random
# stream moves whenever main.c changes at all, even in code play cannot reach.
# Sections that need luck (a human caught AND carried down) then pass or fail on
# whether that build happened to draw a kind stream, which is a gate that goes
# red for no reason.  start() pins the seed instead, so what is tested is the
# rule, not the luck.  This one is main.c's own fallback constant; it is here
# because it is known to produce a catch and a set-down.
RNG_SEED = 0xACE1


def parse_map(path):
    """`     0000C0B6  Fmain$player_x$0$0   main$` -> {'player_x': 0xC0B6}.

    Only the file-scope objects of main (Fmain$...), which is all this needs."""
    out = {}
    for line in open(path):
        m = re.match(r"\s*([0-9A-Fa-f]{8})\s+Fmain\$(\w+?)(?:\$\d+_\d+)?\$\d+", line)
        if m:
            out[m.group(2)] = int(m.group(1), 16)
    return out


def torus_dx(a, b):
    """Signed shortest distance a -> b around the 1024 px world."""
    d = (b - a) & (WORLD_W - 1)
    return d - WORLD_W if d > WORLD_W // 2 else d


class Game:
    def __init__(self, rom, mapfile="protector.map"):
        self.p = PyBoy(rom, window="null", sound_emulated=False)
        self.addr = parse_map(mapfile)
        for need in ("state", "lives", "wave", "score", "player_x", "player_y",
                     "hum", "en", "en_on", "en_tgt", "hi_score", "rng_s"):
            if need not in self.addr:
                sys.exit("probe: %s missing from %s -- run `make sym`" % (need, mapfile))

    def var(self, name, n=1):
        a = self.addr[name]
        b = self.p.memory[a:a + n]
        return b[0] if n == 1 else list(b)

    def u16(self, a):
        b = self.p.memory[a:a + 2]
        return b[0] | (b[1] << 8)

    @property
    def humans(self):
        """[(x, y, state, aux)] for all six, in slot order -- dead ones included,
        because a slot is never reused and the index is the identity."""
        base = self.addr["hum"]
        out = []
        for i in range(MAX_HUMAN):
            a = base + i * HUM_STRIDE
            out.append((self.u16(a), self.p.memory[a + 2], self.p.memory[a + 3],
                        self.p.memory[a + 4]))
        return out

    @property
    def humans_alive(self):
        """How main.c counts them: anything not DEAD. A held human is still
        alive -- that is the whole point of being able to shoot it back."""
        return sum(1 for h in self.humans if h[2] != HUM_DEAD)

    @property
    def enemies(self):
        """[(slot, x, y, state, target_human)] for the live ones. The slot is
        carried because en_on[]/en_tgt[]/en_cd[] are parallel to it, not to this
        list."""
        out = []
        en, on, tgt = self.addr["en"], self.addr["en_on"], self.addr["en_tgt"]
        for i in range(MAX_ENEMY):
            if not self.p.memory[on + i]:
                continue
            a = en + i * EN_STRIDE
            out.append((i, self.u16(a), self.p.memory[a + 2], self.p.memory[a + 3],
                        self.p.memory[tgt + i]))
        return out

    @property
    def ship(self):
        return (self.u16(self.addr["player_x"]), self.var("player_y"))

    @property
    def score(self):
        return self.u16(self.addr["score"])

    def run(self, frames, buttons=""):
        ks = [KEYS[c] for c in buttons if c in KEYS]
        for _ in range(frames):
            for k in ks:
                self.p.button_press(k)
            self.p.tick(1, True)
            for k in ks:
                self.p.button_release(k)

    def start(self, boot=150, settle=120):
        """Boots to the title screen, presses START, then drains the 90-frame
        spawn invulnerability. START is held 20 frames because PyBoy swallows
        input for several ticks after its own boot splash. Pass settle=0 to
        inspect the opening position before any lander has had time to move."""
        self.run(boot)                      # past PyBoy's own boot splash
        self.run(20, "S")                   # title screen -> new_game()
        self.run(settle)
        self.seed_rng(RNG_SEED)

    def seed_rng(self, v):
        """Pin the game's RNG. See RNG_SEED for why this is worth a step."""
        a = self.addr["rng_s"]
        self.p.memory[a] = v & 0xFF
        self.p.memory[a + 1] = (v >> 8) & 0xFF

    def autopilot(self, frames, fire=True, catch=False):
        """Steer toward the nearest live enemy, one decision per frame. Crude,
        but it flies the ship into things, which is what a test needs -- and
        over a few thousand frames it racks up kills on carriers as readily as
        on anything else, which is where the rescue evidence comes from.

        `catch` goes for a falling human instead, and holds fire while doing
        it. The catch window is short -- a human drops FALL_STEP px/frame -- so
        this takes whatever is falling rather than picking a target."""
        for _ in range(frames):
            sx, sy = self.ship
            aim = None
            shooting = fire
            if catch:
                best = None
                for h in self.humans:
                    if h[2] != HUM_FALL:
                        continue
                    d = abs(torus_dx(sx + 8, h[0] + 4))
                    if best is None or d < best[0]:
                        best = (d, h[0] + 4, h[1])
                if best is not None:
                    aim = (best[1], best[2])
                    shooting = False
            if aim is None:
                best = None
                for (_slot, x, y, _st, _t) in self.enemies:
                    d = abs(torus_dx(sx + 8, x + 8))
                    if best is None or d < best[0]:
                        best = (d, x + 8, y)
                if best is not None:
                    aim = (best[1], best[2])
            b = "A" if shooting else ""
            if aim is not None:
                dx = torus_dx(sx + 8, aim[0])
                if dx > 4:
                    b += "R"
                elif dx < -4:
                    b += "L"
                if aim[1] > sy + 2:
                    b += "D"
                elif aim[1] < sy - 2:
                    b += "U"
            self.run(1, b)

    def idle_until_hold(self, limit=3000):
        """Do nothing until a lander has actually taken a human (state HELD).
        Returns its slot index, or None. Seeking landers ignore the ship, so
        standing still is safe until the first mutant turns up."""
        for _ in range(limit):
            if self.var("state") == ST_OVER:
                return None
            for i, h in enumerate(self.humans):
                if h[2] == HUM_HELD:
                    return i
            self.run(1)
        return None


def smoke(rom, mapfile):
    fails = []

    def check(cond, msg):
        print(("  ok   " if cond else "  FAIL ") + msg)
        if not cond:
            fails.append(msg)

    print("start")
    g = Game(rom, mapfile)
    g.start(settle=0)
    check(g.var("lives") == 3, "a new game has 3 lives (got %d)" % g.var("lives"))
    check(g.var("wave") == 1, "and starts on wave 1 (got %d)" % g.var("wave"))
    check(g.var("state") == ST_PLAY, "and is playing (got state %d)" % g.var("state"))
    check(g.humans_alive == MAX_HUMAN,
          "with all %d humans on the ground (got %d)" % (MAX_HUMAN, g.humans_alive))
    check(all(h[2] == HUM_GROUND for h in g.humans),
          "and every one of them standing, not falling or held")

    print("playing -- hunting")
    g = Game(rom, mapfile)
    g.start()
    best = 0
    for _ in range(30):
        g.autopilot(30)
        best = max(best, g.score)
        if g.var("state") == ST_OVER:
            break
    check(best > 0, "shooting a lander scores (got %d)" % best)

    # Abduction. Nothing later would fail if the grab edge were broken, so this
    # one is not optional: a lander has to reach a human and mark it HELD.
    print("abduction")
    g = Game(rom, mapfile)
    g.start()
    slot = g.idle_until_hold()
    check(slot is not None, "a lander reaches a human and takes it")
    if slot is not None:
        check(g.humans[slot][2] == HUM_HELD, "slot %d is marked HELD" % slot)
        ground = sum(1 for h in g.humans if h[2] == HUM_GROUND)
        check(ground < MAX_HUMAN,
              "and leaves the ground (still standing: %d)" % ground)
        # The carrier records which human it has in en_tgt[] -- that index is
        # what kill_enemy() reads to drop the right one, so it had better point
        # at exactly one carrier and not at a human somebody else is holding.
        holders = [e for e in g.enemies
                   if e[4] == slot and e[3] in (EN_GRAB, EN_CARRY)]
        check(len(holders) == 1,
              "exactly one carrier points at it (got %d)" % len(holders))

    # Rescue, and the fall rule that makes it more than a formality: a shot
    # carrier drops its human, and the drop only saves it if it was low enough.
    #
    # Rather than fly across the map to a chosen carrier -- which means flying
    # through three other landers and dying on the way -- just play, and watch
    # every human transition. Shooting a carrier is a thing this autopilot does
    # several times per run anyway; the probe only has to notice it happen and
    # check the arithmetic that follows. Both outcomes show up, which is what
    # makes this worth asserting rather than a coin flip.
    print("rescue")
    g = Game(rom, mapfile)
    g.start()
    prev = [h[2] for h in g.humans]
    open_drops = {}                     # slot -> (aux, frames_since_drop)
    outcomes = []                       # (aux, ground_y, state)
    for _ in range(9000):
        g.autopilot(1)
        for i, h in enumerate(g.humans):
            st, y, aux = h[2], h[1], h[3]
            if st == prev[i]:
                continue
            if prev[i] == HUM_HELD and st == HUM_FALL:
                open_drops[i] = (aux, 0)
            elif st in (HUM_GROUND, HUM_DEAD) and i in open_drops:
                start_aux, _n = open_drops.pop(i)
                outcomes.append((start_aux, y, st))
            elif st == HUM_CARRIED and i in open_drops:
                # The ship flew into it on the way down: the fall ended in a
                # rescue, not a landing, so the fall rule has nothing to say.
                open_drops.pop(i)
        prev = [h[2] for h in g.humans]
        for i in list(open_drops):
            open_drops[i] = (open_drops[i][0], open_drops[i][1] + 1)
        if g.var("state") == ST_OVER or len(outcomes) >= 3:
            break

    check(bool(outcomes),
          "a shot carrier drops the human it was holding (%d drops seen)" % len(outcomes))
    check(not open_drops,
          "and every drop resolves to standing or dead, never stuck falling")
    for aux, ground_y, st in outcomes:
        long_fall = (ground_y - aux) > FALL_SAFE
        check(st == (HUM_DEAD if long_fall else HUM_GROUND),
              "the fall rule holds: y=%d dropped to y=%d (%d px) -> %s"
              % (aux, ground_y, ground_y - aux,
                 "dead" if st == HUM_DEAD else "standing"))

    # The rescue: fly into a falling human, carry it down, set it on the deck.
    # The catch window is short -- FALL_STEP px/frame -- so this takes whatever
    # falls nearby rather than picking a target, and plays until it happens.
    print("rescue -- catch and carry")
    g = Game(rom, mapfile)
    g.start()
    prev = [h[2] for h in g.humans]
    prev_score = g.score
    caught = landed = dropped_into_death = 0
    bonus = None
    for _ in range(14000):
        g.autopilot(1, catch=True)
        now = [h[2] for h in g.humans]
        s = g.score
        for i in range(MAX_HUMAN):
            if now[i] == prev[i]:
                continue
            if now[i] == HUM_CARRIED:
                caught += 1
            elif prev[i] == HUM_CARRIED:
                if now[i] == HUM_GROUND:
                    landed += 1
                    if bonus is None:
                        bonus = s - prev_score
                elif now[i] == HUM_DEAD:
                    dropped_into_death += 1
        prev = now
        prev_score = s
        if g.var("state") == ST_OVER or (caught and landed):
            break

    check(caught > 0, "the ship catches a falling human (%d caught)" % caught)
    check(landed > 0,
          "and sets it back down on the ground (%d of %d set down)"
          % (landed, caught))
    check(dropped_into_death == 0,
          "a carried human never dies in your hands (%d did)" % dropped_into_death)
    if bonus is not None:
        check(bonus >= RESCUE_BONUS,
              "setting one down pays the rescue bonus (%d points)" % bonus)

    # The high score is the one piece of state that outlives the console, so it
    # is the one thing tests/test_sim.c cannot finish the job on: sim.h knows
    # the record format, but whether the cartridge is wired for SRAM at all is a
    # pair of linker flags (-Wl-yt/-ya), an address (0xA000) and a register
    # write. Bank a score, then boot a second emulator on the same ROM and watch
    # it come back -- that is the whole feature, and no part of the host tests
    # can see it.
    print("high score")
    g = Game(rom, mapfile)
    g.start()
    best = 0
    for _ in range(700):                    # 21000 frames; a game over takes ~7900
        g.autopilot(30)
        best = max(best, g.score)
        if g.var("state") == ST_OVER:
            break
    check(g.var("state") == ST_OVER,
          "a run reaches game over (state %d)" % g.var("state"))
    hi = g.u16(g.addr["hi_score"])
    check(hi >= best,
          "its score is banked as the high score (%d, scored %d)" % (hi, best))

    # The game over screen gives way to the title after five seconds, so a
    # cabinet-style attract loop comes back on its own -- START still cuts the
    # wait short. The break above can happen up to 30 frames after the screen
    # appeared, hence the margin on both sides of the 300-frame hold.
    g.run(240)
    check(g.var("state") == ST_OVER,
          "it sits on the game over screen for a while (state %d)" % g.var("state"))
    g.run(120)
    check(g.var("state") == ST_TITLE,
          "then returns to the title on its own (state %d)" % g.var("state"))
    g.run(20, "S")
    check(g.var("state") == ST_PLAY,
          "where START still starts a new game (state %d)" % g.var("state"))
    g.p.stop()                              # flushes cartridge RAM to <rom>.ram

    g = Game(rom, mapfile)
    g.run(240)                              # boot far enough for main() to read it
    reloaded = g.u16(g.addr["hi_score"])
    check(reloaded == hi,
          "and a later boot loads it back (banked %d, reloaded %d)" % (hi, reloaded))

    print("%d failures" % len(fails))
    return 1 if fails else 0


if __name__ == "__main__":
    sys.exit(smoke(sys.argv[1] if len(sys.argv) > 1 else "protector.gb",
                   sys.argv[2] if len(sys.argv) > 2 else "protector.map"))
