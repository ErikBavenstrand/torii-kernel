# Torii

Torii is a custom kernel for the EZ-Flash Omega DE, forked from EZ-Flash's official 1.06 source. `README.md` explains the project; `just --list` lists the commands.

## Kernel source

- To release, rename `## Unreleased` in `CHANGELOG.md` to the version, set `TORII_VERSION` in `source/version.h` to it (Info's About shows it and every boot logs it), commit, and push a tag like `v0.1.0`: CI publishes a GitHub Release with `ezkernelnew.bin` and that section as its notes.
- **Mandatory:** log every change to `source/` in `CHANGELOG.md`, under Unreleased: what changed and why, with the commit's short hash, in two or three lines. Commit the change first, then add the entry with its hash (one follow-up commit can log several changes).
- Every screen draws through the tile engine (`source/engine`, `source/ui`); there is no bitmap mode. Code outside a screen (the boot, launches, the firmware update) shows messages, questions and progress with `source/ui/dialog.h`, which starts an engine session when none is up. A screen that ends its session calls `ui_shutdown()`, then `dialog_blank()`.
- Every string the menu shows has a key in `lang/en.ini`, the source of truth (`tools/lang.py`'s top documents the format). Code reads one as `S(STR_...)` (`source/lang.h`), fills in its `{1}`-`{9}` with `SF` or `lang_fill` and typed arguments (`LN`, `LS`, `LG`...), never a printf format, and picks a count's `.one`/`.other` form with `SN` or `STR_N`. A list indexed by an enum keeps the enum's order in en.ini, with a `_Static_assert` beside its use.
  `just lang` generates `source/lang_ids.h` and `source/lang_en.c` from it: committed, never edited by hand, and `tests/lang_test.py` fails when they are stale. `just build` compiles each `lang/<code>.ini`, and the test pseudo-locale `en-XA`, into `dist/lang/<code>.lng`; on the card packs sit in `/torii/lang/`, Settings' Language row lists them, and any string a pack lacks shows in English.
  Log lines and Diagnostics' checklist stay English, with no ids. A string a launch shows after the game fills PSRAM (the in-game menu's, the launch's dialogs) belongs in one of en.ini's launch sections (`[meta]` lists them): `lang_before_launch` keeps those in RAM.
- File names are UTF-8: FatFs uses its UTF-8 API (`FF_LFN_UNICODE 2`), so every name lists and opens as it is; `FF_CODE_PAGE` matters only for 8.3-only names. The menu's tile fonts (`source/engine/font.h`) cover Latin-1, and `charset_next_latin1()` in `source/charset.c` picks a glyph per character.
- `just font` generates the tile fonts into `source/engine/font_body.c` and `font_heading.c` from `assets/fonts`; they are committed, never edited by hand, and `tests/font_test.py` fails when they are stale.
- `just assets` generates the menu's art (`source/ui/art_data.c`, `art_ids.h`) from the PNGs in `assets/ui`, the same way: committed, never edited by hand, and `tests/asset_test.py` fails when stale. Screens take art by id through `source/ui/art.h`.
- Our code builds with `-Werror` and the extra warnings in the Makefile's `WARNFLAGS`; the vendored `source/ff15` is exempt.
  Make a function used in one file `static`, and declare the rest in a header.
  Keep `IWRAM_CODE` functions extern: GCC inlines a static one into its ROM callers, and the code then runs from ROM while the ROM page changes.
  Don't build with `-fno-inline-functions-called-once`: libgba's inline `dmaCopy` then runs from ROM inside IWRAM code such as the PSRAM loaders.
- Log from kernel code with `tlog()`, `tlog_error()`, `tlog_debug()` and `TLOG_ASSERT()` from `source/tlog.h`. Lines print as `kernel: ...` (errors as `kernel error: ...`) from `mgba-shot` and as `GBA Debug: ...` from `just play`. A failed assert fails `just compat`.
  On a real cart, lines at the LOGGING setting's level wait in a RAM ring and `tlog_flush()` writes them to `/torii/logs`; with LOGGING off nothing touches the card. Call `tlog_flush()` only where the kernel's ROM is mapped and no FatFs call is under way. `just logs PATH` reads a card's logs.
- **Power-cut tests:** put `cutpoint("what just happened")` (`source/cutpoint.h`) after each FatFs step of an operation that must survive a cut (it logs `cutpoint N: ...`; nothing else). `mgba-shot -X N` cuts the power at cutpoint N (status 75): the SD image holds the sectors written so far, never what FatFs still has in its window or file buffer. `cut_sweep SCENARIO CHECK` in `tests/cutsweep.sh` reruns a scenario cut at each one in turn; `tests/cutpoint.sh` is the example.
- Give `f_write` and `f_read` word-aligned buffers: FatFs hands whole sectors straight to `Write_SD_sectors`, whose 16-bit DMA rounds an odd address down.
- **Kernel NOR is written only through `Save_info`, within window offsets `[0x780000, 0x7C0000)`** (`kernel_nor_record_ok` in `source/bootloader.h`). From `0x7C0000` the kernel's window wraps onto EZ-Flash's boot loader, and erasing it bricks the cart, as v0.2.1 and v0.2.2 did. Nothing but Diagnostics' read-only loader check sets ROM page `0x8000`.
  The emulator enforces it: a write to the loader stops the run with status 76, and `tests/loader_safety.sh` runs every path that writes kernel NOR. Add any new one to that test.
- Cart SRAM pages from `0x40` hold save states and page `0xF0` the kernel's crash dump and session state (`source/cartlog.h`); pages `0x00`-`0x1F` are the game's save. Leave the page at 0 when done.
- `just size` checks the kernel, and the test kernel (`tests/kernel` linked in), against the 1 MB kernel area, IWRAM and EWRAM. EWRAM has about 11 KB left (11,564 bytes at v0.9's strings; the list row cache, `ROW_CACHE_BYTES`, takes 24 KB, and the pages' states share one block, `source/screens/page_state.h`); past its end, data wraps onto its start. `just stack` lists the largest frames and estimates the deepest call chain at about 2.8 KB (2,784 bytes at v0.7), so keep IWRAM free space well above that (6,848 bytes at v0.7, so about 4 KB of margin). Every kernel links the unit tests, for Diagnostics, so their static buffers go in EWRAM, or borrow `pReadCache`.
  `pReadCache` (128 KB) is a scratch buffer that holds nothing between menu passes: an operation borrows it and is done with it when it returns. Its owners and their offsets are listed in `source/ez_define.h`; add any new one there.
- Build only with `just build`. It pins devkitARM r54 because newer GCC rejects the unmodified source.
- The build is reproducible; `just repro` checks it, and CI runs it. Keep it so: sort any `$(wildcard)` or file listing that sets an order, and keep paths and dates out of the binary.
  `tools/build_id.py` writes the build ID (`source/build_id.h`, a CRC32 of the ROM) after linking, and `just logs` uses the ID to pick symbols.
- Builds guard against stale objects themselves (`tools/fresh.py`): the colima VM's virtiofs mount shows edits to containers up to a second late, so make alone can reuse old objects. Build through the `just` recipes, never `make` directly.

## Formatting

- Text files are UTF-8 with LF line endings; `just lint` checks. Only `*.bat` keep EZ-Flash's encoding and CRLF, and the emulator patches carry upstream's CRLF lines.
- Run `just format` after editing C files; `just lint` fails on unformatted ones.
  It formats every C file with `.clang-format`, except the vendored ones `.clang-format-ignore` lists.
- `.git-blame-ignore-revs` lists the commit that reformatted the official source; run `git config blame.ignoreRevsFile .git-blame-ignore-revs` once so `git blame` skips it.

## Checking changes

Check in tiers; each costs more, so run it only when it applies (timings in `docs/research/dev-loop-speed.md`):

- After each edit: `just build`, or `just test-kernel` / `just test-host` for the code you touched.
- Before each commit and before handing work back: `just check`. It reads the files changed against main (committed or not, plus edits in `emu/mgba` not yet saved to `emu/patches`; `--staged` for staged ones), prints the tiers each needs and why, and runs them: docs need `lint`; `tests/host/` needs `test-host`; kernel C adds `test`, `size` and `analyze`; and anything that can change launching, saves, patching or the cart model adds `compat`. Every file in `source/` and `tests/kernel/` counts as that, except the menu-only files in `NO_COMPAT`. The rules are in `tools/check.py`; `--dry-run` shows the plan.
  The tiers run side by side after one build (test, compat and repro one after another, as each fills the VM), each into `dist/.check/TIER.log`, printed whole as it ends. A tier that passed before with the same inputs is skipped, so re-running after a commit or a docs-only edit is instant; `--no-cache` runs everything.
  `test` runs only the jobs a change touches: each run records which kernel functions every job of `tests/all.sh` and every golden scenario executes (`dist/.check/testmap.json`), and a job runs again when one of them changed (by fingerprint, `tools/testmap.py`), when its script or the shared test files changed, or when it never passed. `just analyze` reuses each file's results until the file or a header it includes changes.
- Once per branch, before merge: `just check --full`, which runs every test job and `compat --all` instead of `compat`. It catches what selection can't: a change that breaks code it didn't touch, such as data moving in EWRAM.

Reviews: one round of at most two agents. Use a code reviewer, plus an adversarial tester only for launching, saves, the cart model or patching. Give them your test and compat results (`dist/compat/runs/*.json`) so they don't re-run them, and a time budget (about 10 minutes). Re-check your own fixes instead of starting another round, unless a fix changed behaviour.

1. `just test` (run by `just check`): every line should start with `PASS`.
   Test scripts keep their carts on the container's disk: source `tests/carts.sh` and call `local_carts dist/cart-NAME` (on `dist/`, each power-on costs 0.5 s more).
   Unit-test kernel functions in `tests/kernel/` (`CHECK`, `CHECK_EQ` from `torii_test.h`; declare each test in `tests.h` and list it in `run.c`). They run inside a test build of the kernel, so they can call any kernel function.
   Test hardware-free code in `tests/host/` too: it builds for the host under ASan and UBSan (`just test-host`), with `tests/host/gba_base.h` standing in for libgba's. Keep such code in its own file, as `source/sort.c` and `source/cheat.c` are.
   For changes to launching, saves or the cart model, also run `just compat` (games you own, listed in `roms/games.toml`; missing ones are skipped).
   For changes to C code, also run `just analyze`. It fails only on findings missing from `tools/analyze-baseline.txt`; fix them, or run `just analyze-baseline` for false alarms and fixed findings.
2. For anything visible, run `just shot FRAME KEYS...` and read `dist/shots/shot.png`.
   The menu appears around frame 100. Press a key as `N:A`, release it as `N+6:-`, and leave about 8 frames between presses.
   `just test` also compares the golden screens (`tests/shots.py`; `just shots NAME` runs one). Each `tests/shots/NAME.opts` waits for the kernel's `screen ready: SCREEN` lines with `-w`, presses keys at `@N` frames after them, and names key frames with `-s @N:KEY.png`; `-H @A:@B` hashes every frame of an animation. Log `tlog_debug("screen ready: SCREEN")` where a new screen finishes drawing.
   A changed screen fails, with actual, expected and diff PNGs in `dist/shots-failed/`. When the change is intended, run `just shots-update [NAME]`, look at the PNGs it prints as changed, and commit `tests/shots/`.
   `just test` also checks the kernel's `frame-cycles: NAME N` lines against `tests/perf/budget.txt`: over 280,896 cycles or 10% over the recorded figure fails. Run `just perf-update` to re-record it.
3. To debug headlessly, start `just debug-shot -f FRAMES [mgba-shot options]` in the background and drive it with `just gdb -batch -ex 'break FUNC' -ex continue -ex bt ...`. mGBA's own debugger commands can also be piped into `just debug-cli`, which opens a window.
4. To probe a run, use `just run` with `mgba-shot` options (listed at the top of `emu/mgba-shot.c`): `-k FRAME:KEYS` presses keys, `-u TEXT` stops once the kernel logs TEXT, `-m FRAME:ADDR:LEN` dumps memory, `-r FRAME` prints registers, `-h FRAME` prints a screen hash, `-w TEXT` waits for a log line (later frames given as `@N` count from it), `-c EPOCH` pins the clock (`-C EPOCH` freezes it), `-p FRAME:ADDR:VALUE` writes memory, and `-i FILE` reads options from a file, `-x FRAME:PATH` saves a state and `-l PATH` starts from one. Set `OMEGA_TRACE=1` for the cart trace.
5. For an interactive session, start `just remote` in the background and send `tools/remote.lua` commands with `nc 127.0.0.1 8888` (`press KEYS`, `wait FRAMES`, `read ADDR LEN`, `screenshot PATH`, `save PATH`); paths are relative to the repo. Stop it with `docker rm -f torii-remote`.
6. For SD images with exFAT or UTF-8 names (mtools can't write exFAT), use `tools/sdimg.sh` inside the `torii-emu` container, as `tests/names.sh` does.
7. To check the cart's state, read the files in `dist/cart/` (`sram.bin`, `kernel-nor.bin`) or the SD image with `mtools` inside the `torii-emu` container.

## Built-in emulators

- GB and GBC games run on Jagoomba Color, NES games on PocketNES, both built into the kernel (`source/emulators.s`). Torii plays GBA, GB, GBC and NES only: no plugins.
- `emulators/fetch.sh` pins each one's upstream commit and applies `emulators/patches/NAME.patch` into the git-ignored `emulators/NAME`; `just emulators` builds them into `emulators/out/`, each with the devkitARM of its source's time (the justfile's `*_image`), and `just build` runs it first.
- To change one, edit `emulators/NAME` and run `just emulators-save-patch NAME`, or the change is lost on the next fetch. Mark each change with a `Torii:` comment.

## Emulated cart

- The cart model lives in `emu/mgba/src/gba/cart/omega.c`. `emu/mgba` is a git-ignored mGBA checkout made by `emu/fetch.sh`.
- `kernel-nor.bin` holds the 8 MB kernel NOR in the kernel's window order: offsets are window offsets under ROM page `0x8002`, and the boot loader (chip 0) is at `0x7C0000`.
- After editing it, run `just emu-save-patch`, or the change is lost on the next fetch.
- `docs/research/omega-de-hardware.md` documents the registers, memory map, launch flow and save types. Update it when you learn something new about the hardware.

## Environment

- On macOS, the recipes run containers in a colima VM of their own, `torii` (virtiofs, Rosetta), started on demand; your default colima VM and Docker context are left alone. Run raw `docker` commands for this repo with `DOCKER_CONTEXT=colima-torii`. Containers only see paths under your home folder, so keep worktrees and temporary build folders inside the repo, for example in `dist/`.
- Test with the homebrew ROMs in `tests/roms/`. Commercial ROMs are the user's to supply: `just game-add PATH...` copies them into the git-ignored `roms/` and lists them in `roms/games.toml` (git-ignored, never committed; `roms/games.example.toml` documents its fields). Tests read only `roms/`. The user's own library, `~/Documents/Files/Nintendo`, is theirs: never write to it or use its saves. `just compat` tests one game per set of requirements; `--all` tests every game.
