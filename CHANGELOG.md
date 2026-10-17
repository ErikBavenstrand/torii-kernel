# Changelog

Changes to the kernel (`source/`) since EZ-Flash's official 1.06 source
(commit `12cb68b`), newest first. Tooling, tests and the emulator are in the
git log, not here.

## Unreleased

- **Fixed:** cheat lines no longer make bogus writes (`2c274c8`): an entry
  without a value is dropped instead of reusing the last one, empty `;` and
  trailing `,` add nothing (a trailing `,` wrote 0 to the next address), and
  only hex digits count, so tabs, CRs and stray bytes are skipped.
- **Changed:** the cheat line parser moves unchanged from `showcht.c` to
  `cheat.c`, with the cheat table in `cheat.h` (`a0a46a4`), so it builds
  without FatFs or the hardware and the host tests run it under sanitizers.
- **Changed:** `sort.c` follows the new `.clang-format` (`14e1d5b`).
- **Changed:** the kernel builds with `-Wextra`, `-Wshadow`, `-Wundef`,
  `-Wformat=2`, `-Wstrict-prototypes`, `-Wmissing-prototypes` and `-Werror`
  (`d65cdb4`). The fixes keep behaviour: `(void)` prototypes, renamed
  shadowing locals, casts that keep the unsigned comparisons, `static` for
  file-local functions; `IWRAM_CODE` functions stay extern.

## v0.1.1 (2026-10-17)

- **Fixed:** opening an SD folder no longer builds the path with `sprintf`
  onto itself (`5193849`), which is undefined behaviour and could write past
  the 256-byte path; it appends with bounded `strncat`.
- **Changed:** the kernel builds without compiler warnings (`1e8dcee`):
  `Patch_somegame` gets a prototype in place of an implicit declaration, and
  the NOR erase poll drops an unused variable.
- **Fixed:** save types for Doom, Doom II, Gauntlet - Dark Legacy and the
  Classic NES Series and Famicom Mini games (`b9c9245`). The first three are
  EEPROM 512, not 8K (ez-flash/omega-de-kernel#3); the NES games are EEPROM
  (512, or 8K for the Zelda games and a few others), not SRAM.
- **Fixed:** too many cheats no longer overrun the patch area (`d57c89f`).
  The table is capped at 600 entries, which fit after the in-game handler in
  the 0x2000 bytes appended to the ROM; it also frees 19 KB of EWRAM.
- **Changed:** the GAME RTC setting becomes a CART row with separate CLOCK
  and RUMBLE checkboxes (`6d39f85`, `e7aaa36`, `a854cdc`). Turning the clock off
  no longer turns off rumble (ez-flash/omega-de-kernel#5). Each game gets
  what it uses: rumble for Drill Dozer, WarioWare: Twisted! and GB/GBC
  rumble carts in Goomba; the clock for other GBA games, GB/GBC clock carts
  and PocketNES.
- **Changed:** GB, GBC and NES games start as soon as you open them
  (`7489a1b`). Their boot menu only offered CLEAN BOOT
  (ez-flash/omega-de-kernel#12); hold L for a hard reset as before.
- **Changed:** the SD listing sorts names ignoring case (`792f94e`), with a
  merge sort in place of a bubble sort that copied whole entries. Listing a
  folder past its 256 folders or 512 files no longer writes past the list,
  and a full list of one kind no longer hides the other (`f16036c`).
- **Fixed:** the recently played list at its 10-entry limit (`aa88d7c`).
  Relaunching a listed game or adding to a full list wrote a stray 11th row,
  and the file was never truncated, so old rows lingered.
- **Fixed:** cheat files with LF line endings (`2ff3845`). Each value lost
  its last digit, so cheats wrote wrong values, and an empty value hung the
  kernel. Long values and section names can no longer overflow their buffers.
- **Fixed:** games start with EWRAM fully clear (`b6045f7`). The launch
  reset already cleared it, but the kernel then ran its last reset step from
  EWRAM and left 64 bytes there; that step now runs from IWRAM, with
  interrupts off before IWRAM is cleared (`972be9d`).
- **Changed:** the launch log names the clock and rumble a game may use
  (`63061c9`).

## v0.1.0 (2026-10-17)

- **Fixed:** the kernel builds on Linux (`c04ca5e`). `draw.c` included
  `hzk12.h`, but the file is `HZK12.h`; only case-insensitive file systems
  found it.
- **Fixed:** booting no longer overwrites settings and cheat memory
  (`b029fad`). `Read_NOR_info` copied twice the NOR game list's size, and
  the EWRAM data was 616 bytes over 256 KB. Both wrapped onto
  `SET_info_buffer` and the cheat buffers at the start of EWRAM. The copy now
  stops at the end of the list, and `SAV_info_buffer` moved to IWRAM.
- **Added:** `torii_run_tests()` call at boot (`2912147`). It does nothing in
  the normal kernel; the test kernel runs `tests/kernel` there.
- **Added:** `tlog` debug log (`3827cf7`). It logs boot and each game launch
  to mGBA's debug console and does nothing on real hardware.
