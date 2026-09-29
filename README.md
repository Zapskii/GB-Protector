# GB-Protector

A Defender-style side-scrolling shooter for the Game Boy (DMG), written in
GBDK-2020 C with no game engine.

Playable ROM: **[protector.gb](https://github.com/Zapskii/GB-Protector/releases/latest)**
(latest release).

Protect the humans on the ground. Landers descend, abduct a human, and carry
it to the top of the screen, where the pair becomes a fast, shooting mutant.
Lose every human and it is game over.

Shoot a lander while it is **carrying** and the human is dropped -- saved if it
fell less than 40 px, killed by the fall if it was already high. Catch a
**falling** human by flying into it and it rides under your hull: take it down
to the deck and it walks away, for 500 points.

## Controls

| Button     | Action                                                 |
|------------|--------------------------------------------------------|
| Left/Right | Thrust (with momentum); also turns the ship            |
| Up/Down    | Move vertically                                        |
| A          | Fire (hold for autofire)                               |
| B          | Smart bomb (destroys everything on screen, 3 per life) |
| START      | Start from the title / leave the game over screen early |

Fly **into** a falling human to catch it; it hangs under your hull until you
take it down to the deck, where it is released the moment your hull reaches the
ground -- there is no button for it. Losing the ship while carrying one drops it
from wherever you were.

The scanner along the bottom shows the whole 1024 px world around you: large
dots are enemies (and you, in the centre), small dots are humans.

Your best score is saved to the cartridge, and shows on the title screen above
PRESS START. Beat it and the game over screen says so; otherwise it shows the
record you have to catch.

Game over holds for five seconds and then puts the title back up on its own, so
the score you just banked is already showing on it. START skips the wait and
goes straight into a new game.

## Build

    make          # protector.gb  (uses GBDK_HOME if set, else Docker "gbdk-dev")
    make image    # build the Docker toolchain image (once; picks the host arch)
    make test     # host unit tests for sim.h, plain gcc, no emulator
    make probe    # headless play-test: drives the ROM and asserts the rules
    make fps      # headless frame-rate check: render() must fit in vblank
    make sym      # build with -debug so the map carries every symbol (debuggers)
    make gfx      # regenerate gfx.h from mkgfx.py (gfx.h is committed)
    make title    # regenerate title.h (title image) from tools/mktitle.py
    make border   # regenerate border_data.c (SGB border) from art/border_sgb.png
    make usage    # ROM / RAM headroom
    make clean

The saved high score lives beside the ROM: `protector.gb.ram` for PyBoy,
`protector.sav` for most other emulators. `make clean` deletes it, so a build
never inherits somebody else's record.

Building a ROM to hand out? `make probe`, `make fps` and `make sym` leave a
`-debug` build in `protector.gb`; plain `make` does not, and `make` alone will
not notice, because the file is already newer than its sources.

`make test` is the real test when you change gameplay: it compiles `sim.h` with
plain gcc, so a broken seam collision or a wrong abduction edge fails in a
second instead of being squinted at in an emulator. `make probe` catches what
`sim.h` cannot reach -- the wiring in `main.c` -- by reading the game's own
variables out of PyBoy (`PY=.venv/bin/python make probe` if PyBoy is in a venv).

## Layout

| File               | What it is                                                   |
|--------------------|--------------------------------------------------------------|
| `main.c`           | The game: loop, camera, terrain streaming, entities, render  |
| `sim.h`            | Pure logic (torus math, terrain, lander state machine); no GBDK |
| `tests/test_sim.c` | Host tests for `sim.h`                                       |
| `mkgfx.py`         | Procedural tiles and sprites, drawn as ASCII -> `gfx.h`      |
| `gfx.h`            | Generated, committed so a plain `make` needs no Python       |
| `tools/mktitle.py` | Title-screen image -> `title.h` (tiles + map, deduped)       |
| `title.h`          | Generated title image; `title_screen()` swaps the BG tile bank to it, `new_game()` swaps back |
| `art/border_sgb.png` | The Super Game Boy border art, 256x224 with the game area transparent |
| `tools/mkborder.py` | Rewords that art in place, in the art's own font, then `make border` |
| `sgb_border.c`     | `set_sgb_border()`: the CHR_TRN/PCT_TRN transfer that installs it |
| `border_data.c`    | Generated border tiles/map/palettes, committed so `make` needs no Python |
| `tools/probe.py`   | Headless play-test harness (dev only)                        |
| `tools/shot.py`    | Headless screenshot + scripted input (dev only)              |
| `tools/fps.py`     | Headless frame-rate check via the game's own frame counter (dev only) |

## How it works

* **World** - a 1024 px torus. `torus_dx()` gives the signed shortest distance
  between two x positions, and every collision goes through it, so nothing
  breaks across the seam. Tested exhaustively against a brute-force check.
* **Background** - the hardware map is only 32 tiles wide, so one 15-tile
  terrain column is streamed in per 8 px of camera movement.
* **HUD** - the window layer sits at the bottom (`WY=120`), so it needs no
  scanline interrupt. Scanner blips are sprites drawn over it.
* **Camera** - the ship eases toward the trailing side of the screen so you
  see further in the direction you are facing.
* **High score** - five bytes at the base of cartridge SRAM: two magic bytes,
  the score, and a checksum over all of it, written only when a run beats it.
  The cart type has to be one that *has* SRAM (`-Wl-yt0x1B -Wl-ya4` in the
  Makefile): get that wrong and the game still runs, still writes, and still
  looks like it is saving, but nothing that runs it ever writes the .sav back.
  `sim.h` owns the record format and `make test` covers it against garbage and
  half-written records; `make probe` boots a second emulator to check the score
  actually comes back. The title screen draws it as sprites, above the title
  image's own tile range -- sprite and BG tiles are the same VRAM, so a glyph
  loaded into a tile the image uses redraws part of the artwork.
* **Super Game Boy border** - `art/border_sgb.png` goes through `png2asset`
  into `border_data.c` (tiles, map, palettes), and `set_sgb_border()` ships it
  in the `CHR_TRN`/`PCT_TRN` transfers the SGB reads off the *rendered screen* --
  which is why it runs once at boot, after `DISPLAY_ON`, and why it has to
  reload the GB tile bank afterwards: the transfer goes through VRAM and leaves
  it full of border. Everything is behind `sgb_check()`, so a DMG boots exactly
  as it did before (`make` output is pixel-identical either way).
* **Sound** - SFX only, on channels the game keeps disjoint: noise (ch4) for
  shoot/boom, square (ch1, sweep) for the rescue catch/set-down (all in
  `sfx_*()`); no music.
* **Sprites** - 8x16 mode, 40 OAM entries, 10 per scanline. When a scanline
  band is over budget the DMG drops the highest OAM indices, so the draw order
  of the four world lists rotates each frame (`render()`): crowded entities
  flicker evenly instead of the same ones vanishing for good, and the ship is
  always drawn first and never flickers. `make fps` guards the frame budget --
  render runs before `wait_vbl_done()`, so an overrun silently halves the game.

## Not done yet

* Music - sound is SFX only.

## License

MIT
