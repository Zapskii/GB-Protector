# GB-Protector

A Defender-style side-scrolling shooter for the Game Boy (DMG), written in
GBDK-2020 C with no game engine.

Protect the humans on the ground. Landers descend, abduct a human, and carry
it to the top of the screen, where the pair becomes a fast, shooting mutant.
Lose every human and it is game over.

Shoot a lander while it is **carrying** and the human is dropped -- saved if it
fell less than 40 px, killed by the fall if it was already high. Catch a
**falling** human by flying into it and it rides under your hull: take it down
to the deck and it walks away, for 500 points.

## Controls

| Button   | Action                                          |
|----------|-------------------------------------------------|
| Left/Right | Thrust (with momentum); also turns the ship   |
| Up/Down  | Move vertically                                 |
| A        | Fire (hold for autofire)                        |
| B        | Smart bomb (destroys everything on screen, 3 per life) |
| START    | Restart from the game over screen               |

Fly **into** a falling human to catch it; it hangs under your hull until you
take it down to the deck, where it is released the moment your hull reaches the
ground -- there is no button for it. Losing the ship while carrying one drops it
from wherever you were.

The scanner along the bottom shows the whole 1024 px world around you: large
dots are enemies (and you, in the centre), small dots are humans.

## Build

    make          # protector.gb  (uses GBDK_HOME if set, else Docker "gbdk-dev")
    make image    # build the Docker toolchain image (once; picks the host arch)
    make test     # host unit tests for sim.h, plain gcc, no emulator
    make probe    # headless play-test: drives the ROM and asserts the rules
    make sym      # build with -debug so the map carries every symbol (debuggers)
    make gfx      # regenerate gfx.h from mkgfx.py (gfx.h is committed)
    make title    # regenerate title.h (title image) from tools/mktitle.py
    make usage    # ROM / RAM headroom
    make clean

`make test` is the real test when you change gameplay: it compiles `sim.h` with
plain gcc, so a broken seam collision or a wrong abduction edge fails in a
second instead of being squinted at in an emulator. `make probe` catches what
`sim.h` cannot reach -- the wiring in `main.c` -- by reading the game's own
variables out of PyBoy (`PY=.venv/bin/python make probe` if PyBoy is in a venv).

## Layout

| File            | What it is                                                    |
|-----------------|---------------------------------------------------------------|
| `main.c`        | The game: loop, camera, terrain streaming, entities, render   |
| `sim.h`         | Pure logic (torus math, terrain, lander state machine); no GBDK |
| `tests/test_sim.c` | Host tests for `sim.h`                                     |
| `mkgfx.py`      | Procedural tiles and sprites, drawn as ASCII -> `gfx.h`       |
| `gfx.h`         | Generated, committed so a plain `make` needs no Python        |
| `tools/mktitle.py` | Title-screen image -> `title.h` (tiles + map, deduped)     |
| `title.h`       | Generated title image, committed; `title_screen()` swaps the BG tile bank to it and `new_game()` swaps back |
| `tools/probe.py`| Headless play-test harness (dev only)                         |
| `tools/shot.py` | Headless screenshot + scripted input (dev only)               |

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
* **Sprites** - 8x16 mode, 40 OAM entries, 10 per scanline. When a scanline
  band is over budget the DMG drops the highest OAM indices, so the draw order
  of the four world lists rotates each frame (`render()`): crowded entities
  flicker evenly instead of the same ones vanishing for good, and the ship is
  always drawn first and never flickers. `make fps` guards the frame budget --
  render runs before `wait_vbl_done()`, so an overrun silently halves the game.

## Not done yet

* Sound is limited to noise-channel SFX (all in `sfx_*()`); no music.
* No high scores or Super Game Boy border (`-Wm-ys` is already set).

## License

MIT
