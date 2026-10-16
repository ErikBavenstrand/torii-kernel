# Torii

Torii is a custom kernel for the EZ-Flash Omega DE, forked from EZ-Flash's official 1.06 source. `README.md` explains the project; `just --list` lists the commands.

## Kernel source

- To release, rename `## Unreleased` in `CHANGELOG.md` to the version, commit, and push a tag like `v0.1.0`: CI publishes a GitHub Release with `ezkernelnew.bin` and that section as its notes.
- **Mandatory:** log every change to `source/` in `CHANGELOG.md`, under Unreleased: what changed and why, with the commit's short hash, in two or three lines. Commit the change first, then add the entry with its hash (one follow-up commit can log several changes).
- Files from the official repo (commit `12cb68b`) keep EZ-Flash's formatting: CRLF line endings, tabs.
- `source/ezkernelnew.c`, `source/lang.c`, `source/GBApatch.c` and `source/gba_rts_patch.s` contain GBK-encoded Chinese strings.
  Edit them as bytes, decoding and encoding with `gbk`; a UTF-8 round trip corrupts the strings.
- Language strings live in `source/lang.c` as `en_*` and `zh_*` pairs, wired up in `LoadEnglish` and `LoadChinese`. Add both.
- Log from kernel code with `tlog()` and `TLOG_ASSERT()` from `source/tlog.h`. Lines print as `kernel: ...` from `mgba-shot` and as `GBA Debug: ...` from `just play`; on real hardware they cost nothing. A failed assert fails `just compat`.
- `just size` checks the kernel against the 1 MB kernel area, IWRAM and EWRAM. EWRAM has about 400 bytes left; past its end, data wraps onto its start. The deepest stack measured is about 1.7 KB, so keep IWRAM free space well above that.
- Build only with `just build`. It pins devkitARM r54 because newer GCC rejects the unmodified source.
- Builds guard against stale objects themselves (`tools/fresh.py`): colima's sshfs mount shows edits to containers up to a second late, with whole-second timestamps, so make alone can reuse old objects. Build through the `just` recipes, never `make` directly.

## Checking changes

1. Run `just test`. Every line should start with `PASS`.
   Unit-test kernel functions in `tests/kernel/` (`CHECK`, `CHECK_EQ` from `torii_test.h`; list each test in `run.c`). They run inside a test build of the kernel, so they can call any kernel function.
   For changes to launching, saves or the cart model, also run `just compat` (games you own, listed in `roms/games.toml`; missing ones are skipped).
2. For anything visible, run `just shot FRAME KEYS...` and read `dist/shots/shot.png`.
   The menu appears around frame 100. Press a key as `N:A`, release it as `N+6:-`, and leave about 8 frames between presses.
3. To debug headlessly, start `just debug-shot -f FRAMES [mgba-shot options]` in the background and drive it with `just gdb -batch -ex 'break FUNC' -ex continue -ex bt ...`. mGBA's own debugger commands can also be piped into `just debug-cli`, which opens a window.
4. To probe a run, use `just run` with `mgba-shot` options (listed at the top of `emu/mgba-shot.c`): `-k FRAME:KEYS` presses keys, `-u TEXT` stops once the kernel logs TEXT, `-m FRAME:ADDR:LEN` dumps memory, `-r FRAME` prints registers, `-h FRAME` prints a screen hash, `-p FRAME:ADDR:VALUE` writes memory, and `-i FILE` reads options from a file, `-x FRAME:PATH` saves a state and `-l PATH` starts from one. Set `OMEGA_TRACE=1` for the cart trace.
5. For an interactive session, start `just remote` in the background and send `tools/remote.lua` commands with `nc 127.0.0.1 8888` (`press KEYS`, `wait FRAMES`, `read ADDR LEN`, `screenshot PATH`, `save PATH`); paths are relative to the repo. Stop it with `docker rm -f torii-remote`.
6. To check the cart's state, read the files in `dist/cart/` (`sram.bin`, `kernel-nor.bin`) or the SD image with `mtools` inside the `torii-emu` container.

## Emulated cart

- The cart model lives in `emu/mgba/src/gba/cart/omega.c`. `emu/mgba` is a git-ignored mGBA checkout made by `emu/fetch.sh`.
- After editing it, run `just emu-save-patch`, or the change is lost on the next fetch.
- `docs/research/omega-de-hardware.md` documents the registers, memory map, launch flow and save types. Update it when you learn something new about the hardware.

## Environment

- On macOS, the recipes run containers in a colima VM of their own, `torii` (virtiofs, Rosetta), started on demand; your default colima VM and Docker context are left alone. Run raw `docker` commands for this repo with `DOCKER_CONTEXT=colima-torii`. Containers only see paths under your home folder, so keep worktrees and temporary build folders inside the repo, for example in `dist/`.
- Test with the homebrew ROMs in `tests/roms/`. Commercial ROMs are the user's to supply: `just game-add PATH...` copies them into the git-ignored `roms/` and lists them in `roms/games.toml` (git-ignored, never committed; `roms/games.example.toml` documents its fields). Tests read only `roms/`. The user's own library, `~/Documents/Files/Nintendo`, is theirs: never write to it or use its saves. `just compat` tests one game per set of requirements; `--all` tests every game.
