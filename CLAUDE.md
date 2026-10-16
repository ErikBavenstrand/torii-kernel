# Torii

Torii is a custom kernel for the EZ-Flash Omega DE, forked from EZ-Flash's official 1.06 source. `README.md` explains the project; `just --list` lists the commands.

## Kernel source

- Files from the official repo (commit `12cb68b`) keep EZ-Flash's formatting: CRLF line endings, tabs.
- `source/ezkernelnew.c`, `source/lang.c`, `source/GBApatch.c` and `source/gba_rts_patch.s` contain GBK-encoded Chinese strings.
  Edit them as bytes, decoding and encoding with `gbk`; a UTF-8 round trip corrupts the strings.
- Language strings live in `source/lang.c` as `en_*` and `zh_*` pairs, wired up in `LoadEnglish` and `LoadChinese`. Add both.
- Log from kernel code with `tlog()` and `TLOG_ASSERT()` from `source/tlog.h`. Lines print as `kernel: ...` from `mgba-shot` and as `GBA Debug: ...` from `just play`; on real hardware they cost nothing. A failed assert fails `just compat`.
- `just size` checks the kernel against the 1 MB kernel area, IWRAM and EWRAM. The official 1.06 source already overflows EWRAM by 616 bytes: `pNorFS` ends at `0x02040268` and wraps onto `SET_info_buffer`.
- Build only with `just build`. It pins devkitARM r54 because newer GCC rejects the unmodified source.
- After changing toolchains or seeing odd link sizes, run `just clean`; stale objects in `build/` get reused.

## Checking changes

1. Run `just test`. Every line should start with `PASS`.
   For changes to launching, saves or the cart model, also run `just compat` (games you own, listed in `roms/games.toml`; missing ones are skipped).
2. For anything visible, run `just shot FRAME KEYS...` and read `dist/shots/shot.png`.
   The menu appears around frame 100. Press a key as `N:A`, release it as `N+6:-`, and leave about 8 frames between presses.
3. To debug headlessly, start `just debug-shot -f FRAMES [mgba-shot options]` in the background and drive it with `just gdb -batch -ex 'break FUNC' -ex continue -ex bt ...`. mGBA's own debugger commands can also be piped into `just debug-cli`, which opens a window.
4. To probe a run, use `just run` with `mgba-shot` options (listed at the top of `emu/mgba-shot.c`): `-k FRAME:KEYS` presses keys, `-u TEXT` stops once the kernel logs TEXT, `-m FRAME:ADDR:LEN` dumps memory, `-r FRAME` prints registers, `-h FRAME` prints a screen hash, `-p FRAME:ADDR:VALUE` writes memory, and `-i FILE` reads options from a file. Set `OMEGA_TRACE=1` for the cart trace.
5. To check the cart's state, read the files in `dist/cart/` (`sram.bin`, `kernel-nor.bin`) or the SD image with `mtools` inside the `torii-emu` container.

## Emulated cart

- The cart model lives in `emu/mgba/src/gba/cart/omega.c`. `emu/mgba` is a git-ignored mGBA checkout made by `emu/fetch.sh`.
- After editing it, run `just emu-save-patch`, or the change is lost on the next fetch.
- `docs/research/omega-de-hardware.md` documents the registers, memory map, launch flow and save types. Update it when you learn something new about the hardware.

## Environment

- On macOS, Docker runs through colima (`colima start`). Its containers only see paths under your home folder, so keep worktrees and temporary build folders inside the repo, for example in `dist/`.
- Test with the homebrew ROMs in `tests/roms/`. Commercial ROMs are the user's to supply: they go in the git-ignored `roms/`, listed in `roms/games.toml` by `just game-add`.
