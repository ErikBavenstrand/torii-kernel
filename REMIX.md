# Omega Remix

Tooling for the EZ-Flash Omega Definitive Edition kernel: a reproducible
build, an emulated cart to run it on, and tests. Not affiliated with EZ-Flash.

The kernel source in this repository is the official 1.06 release,
unmodified: every file from the official repository is byte-identical to
commit `12cb68b` (k1.06). Everything added lives in
new files: `justfile`, `docker/`, `emu/`, `tests/`, `tools/` and `docs/`.

## Setup

- Docker and [just](https://github.com/casey/just). On macOS, start Docker with
  `colima start` if it isn't running. The kernel is built in a container with
  devkitARM r54 (GCC 10.1), pinned by digest: the closest release to the r53
  EZ-Flash used that compiles the source unchanged. That image is amd64-only,
  so on Apple Silicon it runs under emulation (about 30 s per build).
- For `play` and `dev`: CMake, pkg-config and SDL2 to build the emulator
  (`brew install cmake pkgconf sdl2`, or `apt install cmake pkg-config libsdl2-dev`).

## Commands

| Command | Result |
|---|---|
| `just build` | `dist/ezkernelnew.bin`, the flashable upgrade file |
| `just play` | runs the kernel on an emulated Omega DE cart |
| `just dev` | like `play`, rebuilding and relaunching whenever `source/` changes |
| `just test` | boots test ROMs through the kernel and checks SRAM, Flash 64K/128K and EEPROM 512/8K saves survive power cycles |
| `just shot 300 100:A 106:-` | headless screenshot at frame 300, pressing A at frame 100 |
| `just cart-add game.gba` | copies files onto the emulated cart's SD card |
| `just cart-reset` | wipes the emulated cart (SD, NOR, SRAM) |

`just build` replaces `build r53-32.bat` and `Link_kernel_image.exe`: the
upgrade file is the kernel padded to `0x100000` followed by `image.bin`.
Relinking the official 1.06 kernel this way reproduces the shipped
`ezkernelnew.bin` byte for byte. The compiled kernel itself differs from the
shipped one, because r53 is no longer available.

## The emulated cart

`emu/` patches mGBA 0.10.5 with a model of the Omega DE cart
(`emu/patches/0001-omega-de-cart-model.patch`), built from how the kernel drives
the hardware: the register unlock sequence, kernel and game NOR (AMD command
set), 32 MB PSRAM with its paging window, the SD card, FPGA ROM loading from
the kernel's FAT cluster table, banked SRAM, the save chip the FPGA emulates
for each game (SRAM, Flash 64K/128K, EEPROM 512/8K, using mGBA's save
emulation over the cart SRAM) and the RTC. The real kernel boots on it,
launches games from SD and writes saves back to SD.

The cart lives in `dist/cart/`: `sd.img` (64 MB FAT32, first filled from
`emu/sd/`), `kernel-nor.bin` (kernel settings), `game-nor.bin` and `sram.bin`.
They persist between runs, like a real cart. Point any build of the patched
mGBA at a cart folder with `OMEGA_DIR=dist/cart` or `-C omega.dir=dist/cart`.

mGBA's SDL keys: arrows, X = A, Z = B, A = L, S = R, Enter = Start,
Backspace = Select.

Not modelled yet: the in-game menu's real-time saves (RTS) and firmware
updates. Flash and EEPROM data sit in cart SRAM in mGBA's layout (Flash bank 1
at `0x10000`, EEPROM block *n* at `n × 8`). For Flash this matches the real
cart, since the kernel saves SRAM pages 0 and 1 in order; for EEPROM it is
unverified. To change the model, edit `emu/mgba` and run `just emu-save-patch`.

## Research

`docs/research/omega-de-hardware.md` documents the upgrade file layout, the
FPGA bitstreams, the cart register interface and the game launch flow.
`tools/spartan6_bitstream.py` parses the bitstreams.

