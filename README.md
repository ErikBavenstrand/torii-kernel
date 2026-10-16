# Torii

A custom kernel for the EZ-Flash Omega Definitive Edition, in the making.
Named after the shrine gate you pass through into another world, like every
game you launch. Not affiliated with EZ-Flash.

Right now the kernel is EZ-Flash's official 1.06, unmodified. What this repo
adds so far is the groundwork: a reproducible build, an emulated cart to run
the kernel on, and tests.

## Setup

- Docker and [just](https://github.com/casey/just). On macOS, install
  [colima](https://github.com/abiosoft/colima): the recipes start a VM of
  their own, `torii`, that shares the repo over virtiofs, so containers see
  edits at once (colima's default sshfs shows them up to 20 seconds late).
- To run the emulator: CMake, pkg-config and SDL2
  (`brew install cmake pkgconf sdl2 lua@5.4`, or `apt install cmake pkg-config libsdl2-dev liblua5.4-dev libedit-dev`).

## Use

| Command | Result |
|---|---|
| `just build` | `dist/ezkernelnew.bin`, the file you flash to the cart |
| `just sd /Volumes/OMEGA [game.gba...]` | copies the kernel (and games) to a real SD card and ejects it; hold R while switching on to install |
| `just play [game.gba...]` | runs the kernel on an emulated cart, first copying any games given onto its SD card unless they are already there |
| `just dev` | like `play`, rebuilding whenever `source/` changes |
| `just test` | kernel unit tests, then homebrew games through the kernel: boot, every save type across power cycles, crash reports and save states |
| `just cart-add game.gba` | copies a file onto the emulated SD card |
| `just game-add PATH...` | lists games you own (files or whole folders, left in place) in `roms/games.toml` |
| `just test-kernel` | builds a test kernel that runs the unit tests in `tests/kernel` at boot (also part of `just test`) |
| `just compat` | plays each listed game through the kernel and checks it boots, gets the right save type and clock, and keeps its saves; prints a table and writes `dist/compat/index.html` with screenshots |
| `just size` | kernel size against the 1 MB kernel area, IWRAM and EWRAM, plus the largest symbols; fails if anything is over |
| `just compile-commands` | writes `compile_commands.json` so clangd (editors, agents) can jump to definitions and show errors |

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

Kernel code can log with `tlog("...")` (`source/tlog.h`), which writes to
mGBA's debug console: the lines show in the terminal under `just play` and as
`kernel: ...` from the headless runner.

Run with `OMEGA_TRACE=1` to log what the kernel and games do with the cart
(register writes, launches with their save type, SD reads and writes, mode
switches, clock use) to stderr.

`just compat` tests the games in `roms/games.toml`: one game per set of
requirements (system, save chip, kernel save mode, ROM size, clock, other
hardware), since Ruby and Sapphire, say, need the same of the kernel; `just
compat --all` tests every game. Each game boots through the kernel while the
test presses START and A; the screen must change, the save type must match
the ROM, a clock game must find the clock on, and the save must reach
`SAVER/` and come back on relaunch (a test pattern stands in when the game
doesn't save on its own). `dist/compat/index.html` shows the results, what
changed since each game's previous run, coverage by requirement, and
screenshots. List your games with `just game-add ~/path/to/roms` (folders
stay where they are); ROM hacks and kernel plugins are marked `skip`.

When code running headlessly hits an illegal opcode or jumps somewhere that
can't run code, `mgba-shot` stops with exit status 3 and prints a crash report:
the registers and the code addresses on the stack, named from
`dist/ezkernelnew.sym` (or the ROM's own `.sym`). `just compat` counts a
crash as a failure.

To step through the kernel:

- `just debug` opens the window and waits; `just gdb` in a second terminal
  attaches gdb with the kernel's symbols and source (`break main`,
  `continue`, `next`, `bt`, `watch`).
- `just debug-shot -f 600` does the same headlessly, taking `mgba-shot`
  options (keys, screenshots); `just gdb -batch -ex 'break tlog' -ex
  continue -ex bt` scripts a session.
- `just debug-cli` uses mGBA's own debugger in the terminal instead
  (`break main`, `watch/w ADDR`, `x/4 ADDR`, `trace 20`, `help`). `just
  build` writes `dist/ezkernelnew.sym`, which mGBA loads for symbol names.

Save states include the cart: its registers, data port, PSRAM and SRAM (the
NOR chips and SD card are left as they are, like disks). With `mgba-shot`,
`-x FRAME:PATH` saves one and `-l PATH` starts from one, so a test can jump
straight to a point in a game.

To drive the emulator from a script or an AI agent, `just play-remote` (window)
and `just remote` (headless, at real speed) load `tools/remote.lua`, which
takes one command per line on `127.0.0.1:8888`: `press A`, `wait 60`,
`r32 0x02000000`, `screenshot dist/s.png`, `save dist/s.ss` and more (listed
at the top of the script). For example:

```bash
printf 'press START\nwait 30\nscreenshot dist/s.png\n' | nc 127.0.0.1 8888
```

Any mGBA Lua script also runs with `MGBA_SCRIPT=script.lua` in the window or
`mgba-shot -L script.lua` headlessly.

Not modelled yet: the in-game menu's real-time saves, and firmware updates.

## Research

`docs/research/omega-de-hardware.md` describes how the cart works: the
upgrade file, the FPGA firmware, the cart registers, game loading and saves.
