# Torii

Torii is a custom kernel for the EZ-Flash Omega DE, forked from EZ-Flash's official 1.06 source. `README.md` explains the project; `just --list` lists the commands.

## Kernel source

- To release, rename `## Unreleased` in `CHANGELOG.md` to the version, commit, and push a tag like `v0.1.0`: CI publishes a GitHub Release with `ezkernelnew.bin` and that section as its notes.
- **Mandatory:** log every change to `source/` in `CHANGELOG.md`, under Unreleased: what changed and why, with the commit's short hash, in two or three lines. Commit the change first, then add the entry with its hash (one follow-up commit can log several changes).
- Language strings live in `source/lang.c` as `en_*` strings, wired up in `LoadEnglish`. `lang_packs` lists the languages SETTINGS offers; English is the only one until v0.9.
- File names are UTF-8: FatFs uses its UTF-8 API (`FF_LFN_UNICODE 2`), so every name lists and opens as it is; `FF_CODE_PAGE` matters only for 8.3-only names. The menu font has ASCII glyphs only, and `charset_next()` in `source/charset.c` picks one per character.
- Our code builds with `-Werror` and the extra warnings in the Makefile's `WARNFLAGS`; the vendored `source/ff15` is exempt.
  Make a function used in one file `static`, and declare the rest in a header.
  Keep `IWRAM_CODE` functions extern: GCC inlines a static one into its ROM callers, and the code then runs from ROM while the ROM page changes.
  Don't build with `-fno-inline-functions-called-once`: libgba's inline `dmaCopy` then runs from ROM inside IWRAM drawing code.
- Log from kernel code with `tlog()` and `TLOG_ASSERT()` from `source/tlog.h`. Lines print as `kernel: ...` from `mgba-shot` and as `GBA Debug: ...` from `just play`; on real hardware they cost nothing. A failed assert fails `just compat`.
- `just size` checks the kernel against the 1 MB kernel area, IWRAM and EWRAM. EWRAM has about 17 KB left; past its end, data wraps onto its start. `just stack` lists the largest frames and estimates the deepest call chain at about 2.5 KB, so keep IWRAM free space well above that.
- Build only with `just build`. It pins devkitARM r54 because newer GCC rejects the unmodified source.
- Builds guard against stale objects themselves (`tools/fresh.py`): the colima VM's virtiofs mount shows edits to containers up to a second late, so make alone can reuse old objects. Build through the `just` recipes, never `make` directly.

## Formatting

- Text files are UTF-8 with LF line endings; `just lint` checks. Only `emusrc/` and `*.bat` keep EZ-Flash's encoding and CRLF.
- Run `just format` after editing C files; `just lint` fails on unformatted ones.
  It formats every C file with `.clang-format`, except the vendored ones `.clang-format-ignore` lists.
- `.git-blame-ignore-revs` lists the commit that reformatted the official source; run `git config blame.ignoreRevsFile .git-blame-ignore-revs` once so `git blame` skips it.

## Checking changes

1. Run `just test`. Every line should start with `PASS`.
   Unit-test kernel functions in `tests/kernel/` (`CHECK`, `CHECK_EQ` from `torii_test.h`; declare each test in `tests.h` and list it in `run.c`). They run inside a test build of the kernel, so they can call any kernel function.
   Test hardware-free code in `tests/host/` too: it builds for the host under ASan and UBSan (`just test-host`), with `tests/host/gba_base.h` standing in for libgba's. Keep such code in its own file, as `source/sort.c` and `source/cheat.c` are.
   For changes to launching, saves or the cart model, also run `just compat` (games you own, listed in `roms/games.toml`; missing ones are skipped).
   For changes to C code, also run `just analyze`. It fails only on findings missing from `tools/analyze-baseline.txt`; fix them, or run `just analyze-baseline` for false alarms and fixed findings.
2. For anything visible, run `just shot FRAME KEYS...` and read `dist/shots/shot.png`.
   The menu appears around frame 100. Press a key as `N:A`, release it as `N+6:-`, and leave about 8 frames between presses.
3. To debug headlessly, start `just debug-shot -f FRAMES [mgba-shot options]` in the background and drive it with `just gdb -batch -ex 'break FUNC' -ex continue -ex bt ...`. mGBA's own debugger commands can also be piped into `just debug-cli`, which opens a window.
4. To probe a run, use `just run` with `mgba-shot` options (listed at the top of `emu/mgba-shot.c`): `-k FRAME:KEYS` presses keys, `-u TEXT` stops once the kernel logs TEXT, `-m FRAME:ADDR:LEN` dumps memory, `-r FRAME` prints registers, `-h FRAME` prints a screen hash, `-p FRAME:ADDR:VALUE` writes memory, and `-i FILE` reads options from a file, `-x FRAME:PATH` saves a state and `-l PATH` starts from one. Set `OMEGA_TRACE=1` for the cart trace.
5. For an interactive session, start `just remote` in the background and send `tools/remote.lua` commands with `nc 127.0.0.1 8888` (`press KEYS`, `wait FRAMES`, `read ADDR LEN`, `screenshot PATH`, `save PATH`); paths are relative to the repo. Stop it with `docker rm -f torii-remote`.
6. For SD images with exFAT or UTF-8 names (mtools can't write exFAT), use `tools/sdimg.sh` inside the `torii-emu` container, as `tests/names.sh` does.
7. To check the cart's state, read the files in `dist/cart/` (`sram.bin`, `kernel-nor.bin`) or the SD image with `mtools` inside the `torii-emu` container.

## Emulated cart

- The cart model lives in `emu/mgba/src/gba/cart/omega.c`. `emu/mgba` is a git-ignored mGBA checkout made by `emu/fetch.sh`.
- After editing it, run `just emu-save-patch`, or the change is lost on the next fetch.
- `docs/research/omega-de-hardware.md` documents the registers, memory map, launch flow and save types. Update it when you learn something new about the hardware.

## Environment

- On macOS, the recipes run containers in a colima VM of their own, `torii` (virtiofs, Rosetta), started on demand; your default colima VM and Docker context are left alone. Run raw `docker` commands for this repo with `DOCKER_CONTEXT=colima-torii`. Containers only see paths under your home folder, so keep worktrees and temporary build folders inside the repo, for example in `dist/`.
- Test with the homebrew ROMs in `tests/roms/`. Commercial ROMs are the user's to supply: `just game-add PATH...` copies them into the git-ignored `roms/` and lists them in `roms/games.toml` (git-ignored, never committed; `roms/games.example.toml` documents its fields). Tests read only `roms/`. The user's own library, `~/Documents/Files/Nintendo`, is theirs: never write to it or use its saves. `just compat` tests one game per set of requirements; `--all` tests every game.
