# Changelog

Changes to the kernel (`source/`) since EZ-Flash's official 1.06 source
(commit `12cb68b`), newest first. Tooling, tests and the emulator are in the
git log, not here.

## Unreleased

- **Fixed:** save types for Doom, Doom II, Gauntlet - Dark Legacy and the
  Classic NES Series and Famicom Mini games (`b9c9245`). The first three are
  EEPROM 512, not 8K (ez-flash/omega-de-kernel#3); the NES games are EEPROM
  (512, or 8K for the Zelda games and a few others), not SRAM.
- **Fixed:** too many cheats no longer overrun the patch area (`d57c89f`).
  The table is capped at 600 entries, which fit after the in-game handler in
  the 0x2000 bytes appended to the ROM; it also frees 19 KB of EWRAM.
- **Changed:** the GAME RTC setting becomes a CART row with separate CLOCK
  and RUMBLE checkboxes (`6d39f85`, `e7aaa36`). Turning the clock off no
  longer turns off rumble (ez-flash/omega-de-kernel#5): Drill Dozer and
  WarioWare: Twisted!, the only carts with a rumble motor, follow RUMBLE,
  and every other game follows CLOCK.
- **Changed:** GB, GBC and NES games start as soon as you open them
  (`7489a1b`). Their boot menu only offered CLEAN BOOT
  (ez-flash/omega-de-kernel#12); hold L for a hard reset as before.
- **Changed:** the SD listing sorts names ignoring case (`792f94e`), with a
  merge sort in place of a bubble sort that copied whole entries. Listing a
  folder past its 256 folders or 512 files no longer writes past the list.
- **Fixed:** the recently played list at its 10-entry limit (`aa88d7c`).
  Relaunching a listed game or adding to a full list wrote a stray 11th row,
  and the file was never truncated, so old rows lingered.
- **Fixed:** games start with EWRAM fully clear (`b6045f7`). The launch
  reset already cleared it, but the kernel then ran its last reset step from
  EWRAM and left 64 bytes there; that step now runs from IWRAM.

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
