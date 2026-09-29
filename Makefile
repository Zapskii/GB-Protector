# GB-Protector build.
#   make          build protector.gb   (GBDK if GBDK_HOME is set, else Docker)
#   make test     host unit tests for sim.h (plain gcc, no emulator)
#   make probe    headless PyBoy play-test of the main.c rules
#   make fps      headless frame-rate check (render must fit in vblank)
#   make gfx      regenerate gfx.h from mkgfx.py
#   make title    regenerate title.h (title image) from tools/mktitle.py
#   make border   regenerate border_data.c from art/border_sgb.png (SGB border)
#   make usage    ROM/RAM headroom
#   make image    build the gbdk-dev Docker image used when GBDK_HOME is absent
#   make clean

ROM  = protector.gb
CFILES = main.c sgb_border.c border_data.c
SRCS = $(CFILES) sim.h gfx.h title.h sgb_border.h border_data.h
# The probe needs PyBoy. Prefer the project venv, fall back to whatever python3
# is on PATH: make probe PY=python3
ifneq ($(wildcard .venv/bin/python),)
  PY ?= .venv/bin/python
else
  PY ?= python3
endif

ifneq ($(wildcard $(GBDK_HOME)/bin/lcc),)
  RUN   :=
  LCC   := $(GBDK_HOME)/bin/lcc
  USAGE := $(GBDK_HOME)/bin/romusage
  P2A   := $(GBDK_HOME)/bin/png2asset
else
  # GBDK is not installed on this host, so run the toolchain out of the image.
  # lcc must be the FULL PATH: it is not on PATH inside gbdk-dev, and a bare
  # `lcc` fails with "executable file not found".
  # -u keeps build artefacts owned by the user rather than root.
  RUN   := docker run --rm -u $(shell id -u):$(shell id -g) -v "$(CURDIR)":/work -w /work gbdk-dev
  LCC   := /opt/gbdk/bin/lcc
  USAGE := /opt/gbdk/bin/romusage
  P2A   := /opt/gbdk/bin/png2asset
endif

# -Wm-ys : Super Game Boy flag in the header, from day one
# -Wl-m -Wl-j : linker map + NoICE symbols for romusage / debugging
# -Wl-yt0x1B : MBC5 + RAM + battery. The high score lives in cartridge SRAM
#              (hs_sram in main.c), which needs a cart type that HAS SRAM.
# -Wl-ya4 : 4 RAM banks (32 KB). This is the header's SRAM-size byte; get it
#           wrong and the game still runs, but emulators and flash carts see a
#           cartridge with no RAM to save to, so the score never comes back.
LCCFLAGS = -Wm-ys -Wm-yn"PROTECTOR" -Wl-m -Wl-j -Wl-yt0x1B -Wl-ya4

all: $(ROM)

$(ROM): $(SRCS)
	$(RUN) $(LCC) $(LCCFLAGS) -o $@ $(CFILES)

# A `-debug` build: the linker map then carries EVERY symbol, not just the
# globals, which is what tools/probe.py reads the game's state through -- and
# what emulator debuggers want. Not the default, because it is a bigger ROM.
sym: $(SRCS)
	$(RUN) $(LCC) $(LCCFLAGS) -debug -o $(ROM) $(CFILES)

# Headless play-test with PyBoy: drives the ROM and asserts the rules that live
# in main.c rather than sim.h -- abduction, the rescue fall, scoring. Needs the
# symbol map, hence the `sym` dependency. Dev-only; nothing else needs it.
probe: sym
	$(PY) tools/probe.py $(ROM) $(ROM:.gb=.map)

# Headless frame-rate check: render() runs before wait_vbl_done(), so anything
# that overruns the frame budget silently halves the game and neither `test`
# nor `probe` notices. Reads the game's own frame counter per emulated frame.
# Dev-only; nothing else needs it.
fps: sym
	$(PY) tools/fps.py $(ROM) $(ROM:.gb=.map)

test: tests/test_sim
	./tests/test_sim

tests/test_sim: tests/test_sim.c sim.h
	gcc -std=c99 -Wall -Wextra -o $@ tests/test_sim.c

gfx:
	python3 mkgfx.py

title:
	python3 tools/mktitle.py

# The Super Game Boy border: art/border_sgb.png (256x224; the 160x144 game area
# at x=48,y=40 is transparent) -> border_data.c/.h, committed like gfx.h so a
# plain `make` needs no Python. -pack_mode sgb is what gets the SGB layout --
# 4bpp tiles, a 256x224 map, one attribute byte per cell -- instead of a GB
# screen; -use_map_attributes keeps that byte, which is the per-cell palette.
border:
	$(RUN) $(P2A) art/border_sgb.png -map -bpp 4 -max_palettes 4 \
	      -pack_mode sgb -use_map_attributes -c border_data.c

usage: $(ROM)
	$(RUN) $(USAGE) $(ROM:.gb=.map) -g

image:
	docker build -t gbdk-dev .

clean:
	rm -f $(ROM) *.map *.noi *.o *.lst *.sym *.ihx *.asm *.adb tests/test_sim \
	      $(ROM:.gb=.sav) $(ROM).ram

.PHONY: all test gfx title border usage image clean sym probe fps
