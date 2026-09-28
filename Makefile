# GB-Protector build.
#   make          build protector.gb   (GBDK if GBDK_HOME is set, else Docker)
#   make test     host unit tests for sim.h (plain gcc, no emulator)
#   make gfx      regenerate gfx.h from mkgfx.py
#   make usage    ROM/RAM headroom
#   make image    build the gbdk-dev Docker image used when GBDK_HOME is absent
#   make clean

ROM  = protector.gb
SRCS = main.c sim.h gfx.h
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
else
  # GBDK is not installed on this host, so run the toolchain out of the image.
  # lcc must be the FULL PATH: it is not on PATH inside gbdk-dev, and a bare
  # `lcc` fails with "executable file not found".
  # -u keeps build artefacts owned by the user rather than root.
  RUN   := docker run --rm -u $(shell id -u):$(shell id -g) -v "$(CURDIR)":/work -w /work gbdk-dev
  LCC   := /opt/gbdk/bin/lcc
  USAGE := /opt/gbdk/bin/romusage
endif

# -Wm-ys : Super Game Boy flag in the header, from day one
# -Wl-m -Wl-j : linker map + NoICE symbols for romusage / debugging
LCCFLAGS = -Wm-ys -Wm-yn"PROTECTOR" -Wl-m -Wl-j

all: $(ROM)

$(ROM): $(SRCS)
	$(RUN) $(LCC) $(LCCFLAGS) -o $@ main.c

# A `-debug` build: the linker map then carries EVERY symbol, not just the
# globals, which is what tools/probe.py reads the game's state through -- and
# what emulator debuggers want. Not the default, because it is a bigger ROM.
sym: $(SRCS)
	$(RUN) $(LCC) $(LCCFLAGS) -debug -o $(ROM) main.c

# Headless play-test with PyBoy: drives the ROM and asserts the rules that live
# in main.c rather than sim.h -- abduction, the rescue fall, scoring. Needs the
# symbol map, hence the `sym` dependency. Dev-only; nothing else needs it.
probe: sym
	$(PY) tools/probe.py $(ROM) $(ROM:.gb=.map)

test: tests/test_sim
	./tests/test_sim

tests/test_sim: tests/test_sim.c sim.h
	gcc -std=c99 -Wall -Wextra -o $@ tests/test_sim.c

gfx:
	python3 mkgfx.py

usage: $(ROM)
	$(RUN) $(USAGE) $(ROM:.gb=.map) -g

image:
	docker build -t gbdk-dev .

clean:
	rm -f $(ROM) *.map *.noi *.o *.lst *.sym *.ihx *.asm *.adb tests/test_sim

.PHONY: all test gfx usage image clean sym probe
