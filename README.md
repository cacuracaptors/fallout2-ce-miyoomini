# Fallout 2 (1998) — Miyoo Mini Plus / OnionOS Port

An unofficial port of [fallout2-ce](https://github.com/alexbatalov/fallout2-ce) (the community
re-implementation of Fallout 2, with Sfall integration) to run as a standalone OnionOS Port on
the **Miyoo Mini Plus** — a handheld with no 3D graphics acceleration.

This is the sequel to [fallout1-ce-miyoomini](https://github.com/cacuracaptors/fallout1-ce-miyoomini)
— if you're curious how this port came together, that repository documents the full journey.

Built on top of the work of [Alexander Batalov](https://github.com/alexbatalov/fallout2-ce) and
the SDL2 port for this hardware by [steward-fu](https://github.com/steward-fu/sdl2).

## ⚠️ You need your own game files

This repository does **not** include and will **never** include the Fallout 2 data files
(`MASTER.DAT`, `CRITTER.DAT`, the `data/` folder) — they are the property of
Interplay/Bethesda. You need a legitimate copy of the game (GOG or Steam) and must copy those
files yourself. See [Installation](#installation) below.

## Features

- Software rendering (the Miyoo Mini Plus has no 3D GPU)
- The same control scheme as the [Fallout 1 port](https://github.com/cacuracaptors/fallout1-ce-miyoomini),
  adapted for the Miyoo Mini Plus' hardware, which has no analog sticks (see [Controls](#controls))
- The D-pad acts as a mouse cursor
- The same on-device text entry system (D-pad + buttons) for naming your character, save games,
  etc., since the device has no physical keyboard
- Working audio and video, including cutscenes (which play full-screen here, unlike the Fallout 1
  port)

## Controls

| Button | Without Select | With Select held |
|---|---|---|
| D-pad | Moves the mouse cursor | Scrolls the camera/map |
| A | Attack | Skilldex |
| B | End Turn | Character screen |
| X | Slow mouse (hold) | Inventory |
| Y | End Combat | Pip-Boy |
| L1 | Right click | Quickload (F7) |
| R1 | Left click | Quicksave (F6) |
| L2 | Switch active item | Map (Automap) |
| R2 | Switch active item's mode | Center screen on player |
| Start | Enter / confirm | — |
| Select | (modifier) | — |
| Menu Key (Function) | Esc / Menu/Return/Exit | — |

Quicksave and Quickload have a short cooldown after firing (to avoid the underlying hardware's
key-repeat behavior from spamming save/load repeatedly). All other Select-combo actions can be
used again almost immediately.

### Typing text (character name, save names, etc.)

Since the device has no keyboard, text entry works by cycling through letters directly in the
game's own text field:

- **D-pad Up/Down**: cycles through the alphabet/numbers/space at the current position
- **D-pad Left**: toggles uppercase/lowercase for the current letter
- **D-pad Right**: inserts a space directly and moves to the next position
- **A**: confirms the current letter and moves to the next position
- **B**: deletes the last confirmed letter
- **Start**: confirms the whole text entry (Enter)
- **Menu Key (Function)**: cancels the whole text entry (Esc)

## Installation

1. Download the latest `.zip` from the [Releases](../../releases) tab of this repository.
2. Extract its contents to the root of your OnionOS SD card.
3. Copy the following files from your legitimate Fallout 2 installation (GOG/Steam) into
   `Roms/PORTS/Games/Fallout2/`:
   - `MASTER.DAT`
   - `CRITTER.DAT`
   - the `data/` folder
   - `fallout2.cfg` (if included with your installation)
4. On the device, open the **Ports** menu — "Fallout 2" should appear in the list. If not, use
   "refresh roms" at the bottom of the list.

## Building from source

This port requires cross-compiling for ARMv7 hard-float using a Docker-based toolchain. Tested
on Windows + WSL2 + Docker Desktop.

### Prerequisites

- WSL2 with Ubuntu, and Docker Desktop with WSL integration enabled.

### Steps

```bash
mkdir -p ~/fallout-miyoo && cd ~/fallout-miyoo

# Cross toolchain
git clone https://github.com/shauninman/union-miyoomini-toolchain.git

# SDL2 ported for the Miyoo Mini (Plus)
git clone https://github.com/steward-fu/sdl2.git sdl2-miyoo

# This repository (already patched)
git clone https://github.com/cacuracaptors/fallout2-ce-miyoomini.git fallout2-ce
```

**1) Build the Miyoo Mini SDL2** (inside `sdl2-miyoo`, via Docker — see the
[steward-fu/sdl2](https://github.com/steward-fu/sdl2) instructions for the full
`make cfg && make gpu && make sdl2` process). No patches are needed here — this port builds
against a completely unmodified copy of `steward-fu/sdl2`.

**2) Build fallout2-ce** using the cross toolchain:

```bash
cd union-miyoomini-toolchain
make shell
```

Inside the container:

```bash
cd ~/workspace/fallout2-ce

cmake -B build \
  -DCMAKE_TOOLCHAIN_FILE=toolchain-miyoomini.cmake \
  -DCMAKE_MODULE_PATH=$(pwd)/cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DFALLOUT_VENDORED=OFF \
  -DSDL2_INCLUDE_DIR=/root/workspace/sdl2-miyoo/sdl2/include \
  -DSDL2_LIBRARY=/root/workspace/sdl2-miyoo/sdl2/build/.libs/libSDL2.so \
  -DCMAKE_EXE_LINKER_FLAGS="-Wl,--allow-shlib-undefined"

cmake --build build -j4
```

> **Note:** unlike fallout1-ce, this project fetches and builds its own copy of SDL2 by default.
> `-DFALLOUT_VENDORED=OFF` is required to make it use our external, hardware-specific SDL2
> instead.

The final ARM (armhf) binary `fallout2-ce` will be in `build/`.

### What this fork changes (compared to upstream fallout2-ce)

- **`src/dinput.cc`** — makes mouse "relative mode" initialization non-fatal (this device's SDL2
  build doesn't implement it) and adds D-pad-as-mouse-cursor movement.
- **`src/input.cc`** — the full physical-button-to-game-action remapping, the Select-modifier
  layer, the on-device text entry system, and several fixes for this hardware's quirky
  key-repeat/key-up event delivery (including a bug where reusing a mutated struct across a
  synthetic press+release pair could leave a key permanently "stuck" in the auto-repeat system).

## Known issues

- **Audio has a noticeable, constant latency** (roughly consistent regardless of CPU clock
  speed), same as observed in the Fallout 1 port. Traced to the underlying MI_AO (SigmaStar
  audio output) driver — a closed-source binary blob we don't have source access to. We were
  unable to fully resolve this; contributions welcome.
- The mouse cursor moves noticeably slower on screens with an open text field (character
  creation, save/load naming).

## Credits

- [Alexander Batalov](https://github.com/alexbatalov) — fallout2-ce
- [steward-fu](https://github.com/steward-fu) — SDL2 for the Miyoo Mini (Plus)
- [shauninman](https://github.com/shauninman) — union-miyoomini-toolchain
- Interplay Entertainment / Black Isle Studios — the original Fallout 2 (1998)

## License

This port's source code follows the same license as fallout2-ce: the **Sustainable Use License**
(see `LICENSE.md`). Use and distribution are free for non-commercial purposes, provided the
original copyright notices are kept intact.
