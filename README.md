# Torii

A custom kernel for the EZ-Flash Omega Definitive Edition, in the making.
Named after the shrine gate you pass through into another world, like every
game you launch. Not affiliated with EZ-Flash.

Right now the kernel is EZ-Flash's official 1.06, unmodified. What this repo
adds so far is the groundwork: a reproducible build, an emulated cart to run
the kernel on, and tests.

## Setup

- Docker and [just](https://github.com/casey/just). On macOS, start Docker
  with `colima start`.
- To run the emulator: CMake, pkg-config and SDL2
  (`brew install cmake pkgconf sdl2`, or `apt install cmake pkg-config libsdl2-dev`).

## Use

| Command | Result |
|---|---|
| `just build` | `dist/ezkernelnew.bin`, the file you flash to the cart |
| `just play` | runs the kernel on an emulated cart |
| `just dev` | like `play`, rebuilding whenever `source/` changes |
| `just test` | checks games boot and every save type survives power cycles |
| `just cart-add game.gba` | copies a file onto the emulated SD card |

`just --list` shows the rest.

The build uses devkitARM r54, the closest available release to the r53
EZ-Flash used, so the compiled kernel isn't bit-for-bit their file. To flash
exactly the official kernel, use the `ezkernelnew.bin` in the repo root.

## The emulated cart

`emu/` adds a model of the Omega DE cart to the mGBA emulator: SD card, NOR,
PSRAM, game loading, saves (SRAM, Flash and EEPROM) and the clock. The real
kernel boots on it, launches games and writes saves back to SD. The cart's SD
card, NOR and SRAM live in `dist/cart/` and persist between runs.

Keys: arrows, X = A, Z = B, A = L, S = R, Enter = Start, Backspace = Select.
The window opens at 4×; `just scale=3 play` changes it.

Not modelled yet: the in-game menu's real-time saves, and firmware updates.

## Research

`docs/research/omega-de-hardware.md` describes how the cart works: the
upgrade file, the FPGA firmware, the cart registers, game loading and saves.
