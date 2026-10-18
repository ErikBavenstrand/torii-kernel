# Changelog

Changes to the kernel (`source/`) since EZ-Flash's official 1.06 source
(commit `12cb68b`), newest first. Tooling, tests and the emulator are in the
git log, not here.

## Unreleased

- **Changed:** about 17.4 KB of EWRAM freed for the v0.3 engine (2.3 KB free
  before, 19.7 KB after), with no visible change. The cheat screen's line and
  value buffers and the file list sort's work buffer now borrow `pReadCache`,
  which holds nothing between menu passes (its owners are listed in
  `ez_define.h`). Recently Played rows shrink from 512 to 356 bytes, the
  longest entry the kernel keeps (`b9d6f5e`, `8224c72`).
- **Fixed:** a cheat file over 2275 lines ran its list off `pReadCache` and
  overwrote the NOR game list in RAM. The list now stops at 2053 lines and
  logs that the rest were left out (`b9d6f5e`, `664670e`).
- **Added:** the v0.3 menu engine's motion and input layers, not yet used by
  the menu. Motion has eased tweens and critically damped springs (retargets
  keep their velocity) for scroll, sprites, blend and palette fades. Input
  repeats per key (15 frames, then every 4, every 2 after 1 s), pages with
  L/R (20, then 8), and wraps a list only on a fresh press or after a pause
  at its end (`8b073d0`, `ff18199`).
- **Added:** each menu screen logs `screen ready: NAME` at debug level once
  drawn, so the golden-screen tests (`tests/shots`) wait for screens rather
  than fixed frames (`65acf23`).
- **Added:** the v0.3 tile engine's core in `source/engine/`, not yet used by
  any screen: a 60 fps frame loop with idle detection, state-machine jobs
  within a cycle budget, shadow video registers, palettes and OAM committed
  in VBlank through a capped VRAM queue, allocators for VRAM, palette banks
  and affine matrices with per-view arenas, and a layout budget checker
  (`a5d6392`, `09a5d58`).
- **Changed:** Diagnostics' timings read a free-running TM2/TM3 cycle counter
  shared with the engine, instead of restarting the timers each time
  (`a5d6392`).

## v0.2.4 (2026-10-18)

Tested on a real Omega DE (FPGA firmware 7): everything in v0.2.3, plus the
rumble fix below.

- **Fixed:** Diagnostics' rumble step never rumbled. The Omega DE's motor
  isn't driven through GPIO pin 3 in kernel mode: as Jagoomba does it, 0xF1
  at 0x09E20000, then 2 and 0 at 0x08001000, which reach the motor only
  while the GPIO port is on and the ROM page isn't the kernel's (real cart).
  Diagnostics pulses it so under the PSRAM page, and a no is a failure
  (`a6c76ee`, `6adf948`).
- **Added:** the boot logs the answers of the rumble probe ROM
  (`tests/roms/rumble`), which tries the motor four ways in game mode
  (`1fab0fc`, `c2faa0e`).

## v0.2.3 (2026-10-18)

**Don't use v0.2.1 or v0.2.2:** both can erase EZ-Flash's boot loader, and the
cart then starts in safe mode (`No_Loader_found`). `docs/recovery.md` says
how to recover.

- **Fixed:** v0.2.2 bricked a cart at boot, and v0.2.1 on WRITE TO NOR and
  delete. Kernel NOR from 0x7C0000 wraps onto EZ-Flash's boot loader, and the
  NOR list's copy B was written there. Copy B now sits at 0x780000, and
  Save_info writes nothing outside 0x780000-0x7BFFFF (`79cdf75`), nor more
  than 8 KB in the settings sector, which is 8 KB boot sectors on a PL064J
  (`36931c8`). Save_info maps the kernel's ROM page itself before writing,
  so no other page can turn its offsets into the loader's (`b2ad489`).
- **Added:** Diagnostics reads the boot loader, without writing it, and
  logs its CRC32, whether it's blank, erased or overwritten (no GBA header)
  and whether it is EZ-Flash's DE.BIN; a damaged one is an error with a
  warning on screen (`36931c8`, `b2ad489`).

## v0.2.2 (2026-10-18)

- **Fixed:** the EEPROM probe's report counts a read that matches a write
  in bytes 0-6 (the block and the run's nonce) as that write, logs "except
  byte 7: wrote XX, read FF" and flags the run's last-byte loss, so its
  size checks still work; it logs how each write waited (`a517691`,
  `906a468`).
- **Fixed:** Diagnostics' EEPROM item passed on any probe line, even a
  failed run's; it now checks the verdict. A button step missing L or R
  says the hotkeys need them, and the boot after the deliberate crash test
  says it was that test, not "crashed in the menu" (`ce9e84d`).
- **Fixed:** a `.sav` smaller than its save type needs (512 bytes for
  EEPROM 8K) is grown with 0xFF and logged; blocks past its end were never
  loaded or written back, so they were lost at power-off. A grow cut short
  by a full card keeps the old size (`a1d6598`, `906a468`).
- **Fixed:** the in-game menu, sleep and RTS-only hotkeys match when the
  combo's keys are held, whatever else is; an extra or stuck key used to
  block them. The handler is 4 bytes smaller (`0c2ccf2`).
- **Changed:** an older kernel's NOR list is saved in Torii's two-copy
  format once, crash-safely, and the list and game count are logged only
  when they change; v0.2.1 logged the older list on every read (`7a7e916`,
  `906a468`).

## v0.2.1 (2026-10-18)

- **Added:** a build ID, the CRC32 of the kernel's ROM, written after
  linking without moving any code. The boot log line, crash dumps (the
  crashed kernel's ID) and Diagnostics show it, and a unit test checks it
  against the ROM, so `just logs` can tell which symbols fit a log (`d7d0b85`,
  `18502bc`). A crash dump in v0.2.0's layout is logged as skipped and cleared.
- **Changed:** the kernel builds byte for byte the same on CI and macOS:
  the Makefile sorts its file lists, which set the link order, and asserts
  name files relative to the project (`82d6615`).
- **Fixed:** log files no longer fill with junk on the real cart. A new
  file was zero-filled from a ROM array, and ROM reads while SD mode is on
  return the FPGA's SD buffer; it is now filled from RAM, and `disk_write`
  refuses a source in ROM (`059e795`).
- **Fixed:** a log file with junk after its text (from v0.2.0) is left as
  it is, and logging goes on in the next file; the end of a file's text is
  found by reading the whole file, so junk can't hide it (`059e795`), even
  junk with no zero byte in it, such as 0xFF (`7887069`).
- **Fixed:** a flush cut off by power loss left part of a line, and the
  next session's first line ran on from it; the next boot now drops the
  part (`7887069`).
- **Fixed:** the log's sequence number wraps from 99999999 to 1 instead
  of overflowing the header's 8 digits, which lost a file (`7887069`).
- **Added:** the save flow is logged: every "Save sav?" answer, save copy
  and load (name, size, save type, and the `f_write`, `f_read` and
  `f_close` results), NOR game write, and write of the settings, NOR game
  list or pending-save record, read back to check it (`b2dac40`). A save
  copy the card cuts short is now an error in the log, not silent.
- **Changed:** the menu runs at WAITCNT `0x4018` (WS0 2/1 with prefetch),
  which the real cart read correctly, instead of the BIOS's `0x0000`; in
  mGBA a folder of 300 files opens in 23 frames instead of 40. Writes never
  measured at that speed (control registers, SD transfers, NOR, the clock,
  and launching with its PSRAM writes) keep `0x0000`, and games start at
  `0x0000` (`b2dac40`). Still to be confirmed on the real cart.
- **Fixed:** about 100 bytes of the kernel's reset code stayed in IWRAM
  when a game started. The last step now runs from the top of IWRAM, which
  the BIOS clears, and clears all the rest (`b8ff2e0`).
- **Fixed:** the in-game menu's DEBUG gave no sign it worked, and a
  power-off then lost the snapshot. It now says DEBUG SAVED and picks
  RESET, whose boot logs it (`5dab00c`).
- **Fixed:** after a crash dump, the boot report says the kernel crashed
  and where, not that the session ended in the menu; GB and GBC games are
  named by their title, not `(????)` (`f0c4dab`).
- **Added:** the EEPROM probe ROM (`tests/roms/eeprom`) asks the cart's
  EEPROM in game mode, where it answers, with 6-bit or 14-bit addresses;
  the kernel's next boot logs each step and a verdict, to settle 512 against
  8K on the real cart (`f0c4dab`). Each run's data carries a nonce, so an
  earlier run's can't pass for its own (`08ab386`).
- **Changed:** Diagnostics shows the clock live and logs it at the answer,
  names each failed check in the summary, logs rumble answers as info,
  and checks the checklist's yes answers against the log, listing those it
  can't back as unconfirmed. Its EEPROM step points to the probe ROM; the
  kernel-mode probe, which the real cart never answered, is gone
  (`f2e94c5`). A launch counts as evidence only once it reached the game,
  and file names can't fake it (`08ab386`). The WAITCNT line says it is the
  menu's (`3dbd34e`).
- **Fixed:** the game NOR list survives a power cut: it is kept in two copies
  (kernel NOR `0x7A0000` and `0x7C0000`) with a sequence number and CRC, and a
  save always leaves one whole copy (`c14ddbf`). The official kernel's list
  still reads, the official kernel reads Torii's, and games it adds after a
  downgrade are kept on the way back (`811a126`).
- **Fixed:** each game written to NOR ends with a completion mark, written
  last, so a game cut off while written is never listed, and a lost list is
  rebuilt from the marks (`c14ddbf`). It takes the area's last 512 bytes, or
  one more 128 KB block when the ROM fills its last block; a game with no
  room left for that block is written without a mark (`811a126`).
- **Fixed:** the boot no longer erases the NOR list when it reads no games,
  which let the next write go over them; a write reads the list again first
  (`c14ddbf`), and a failed write shows File error (`811a126`).
- **Fixed:** delete latest drops the game from the list and erases its mark
  and header, so a later write with the same header that loses power isn't
  listed with the old size (`c14ddbf`).
- **Fixed:** a ROM without gbafix's `0x96` at `0xB2` is listed after it is
  written to NOR, instead of being overwritten by the next write (`c14ddbf`).
- **Fixed:** the NOR scan turned interrupts back on when it reached the end of
  the 64 MB (`c14ddbf`).
- **Added:** a game started from NOR goes on Recently Played as `NOR:NAME`
  and starts from NOR there (`c14ddbf`).

## v0.2.0 (2026-10-17)

- **Added:** LOGGING in settings page 2 (Off, Errors, Info, Debug; off by
  default) writes the kernel's log to four rotating 64 KB files in
  `/torii/logs`, so a real cart can bring back reports (`63f50a5`). Off,
  it touches neither the card nor cart RAM. Each launch logs what the
  kernel did (header, save type, IRQ hook source, patched words, CART and
  GPIO), and the next boot says how the session ended. A new file needs
  1 MB free besides (`4c831be`), and lines never split across files (`0167c1a`,
  `0bfd2bf`).
- **Added:** a kernel crash (an IRQ arriving outside the kernel's code) is
  kept in cart RAM page 0xF0 with the registers, stack and last log lines,
  and logged at the next boot even with LOGGING off (`63f50a5`); a dump
  stays until it reaches the card, and the largest arrives whole
  (`0167c1a`, `0bfd2bf`).
- **Added:** with LOGGING on, the in-game menu has DEBUG, which saves the
  game's registers, hook state and memory for the next boot's log
  (`63f50a5`, `ed77614`). Its code goes in only then, and never into a NOR
  copy (`0167c1a`). The menu's font keeps only the rows and letters it
  draws, so the menu is smaller than before and still takes 600 cheats.
- **Added:** Diagnostics (DIAGNOSE in settings page 2, or L+R+SELECT at
  power-on) runs the unit tests and probes the clock, PSRAM, NOR, the save
  chip (read only) and cart SRAM, times WAITCNT settings, glyphs, BIOS
  LZ77 and the SD card, tries EEPROM 512 against 8K, looks for a separate
  rumble control, and asks the checklist in `docs/release-checklist.md`
  (`63f50a5`, `7aca5da`, `0167c1a`).
- **Fixed:** reading the game NOR's ID left the chip in ID mode, so after
  a soft reset the NOR list read as empty and the kernel erased its table;
  the read now resets the chip, and so does boot (`0167c1a`).

- **Fixed:** Recently Played no longer overflows a 256-byte buffer on a
  long path (`6b868b7`), and skips a line too long for the kernel to have
  written (`db08c34`), whose folder overran the path buffer.
- **Fixed:** a name shorter than three bytes no longer makes the type check
  read before it (`72a69fc`).
- **Changed:** the list's name copies have explicit bounds and the charset
  switch a default (`8831f34`), for `just analyze`; no change in behaviour.
- **Fixed:** the SD list shows files without the archive bit and read-only
  folders (`371b98b`); it leaves out only hidden and system entries and
  volume labels (`6b868b7`). An 8.3-only name with accented letters lists
  instead of showing `?` (`6b868b7`). A name longer than 99 bytes, which a list entry can't hold, used
  to be cut and fail to open; it now shows cut with a `*` and says Name too
  long, and can't open or delete a file whose name starts the same
  (`c324466`). A folder path past 255 bytes says Path too long, and a folder
  that fails to open leaves the path as it was.
- **Fixed:** a game in more pieces on SD than the cart's table of cluster
  runs holds (61) shows File fragmented instead of overwriting the launch
  parameters and EWRAM (`906c429`), before its save is made (`6b868b7`).
  FAT12 and FAT16 cards whose chains end in any end mark no longer hang the
  launch; an empty game file or a failed card read shows File error
  (`6b868b7`, `db08c34`).
- **Fixed:** writing a 65th game to NOR reports NOR full instead of writing
  past the 64-entry list in EWRAM (`bdc832d`).
- **Fixed:** a Recently Played game whose file is gone shows File error
  (`df1119c`). It made a save and started Goomba or PocketNES with no ROM.
  After a failed Recently Played launch, the list opens its own games again
  (`ff9aa71`); they opened from the entry's folder, or reopened the entry.
- **Fixed:** a 99-byte `.gb` name no longer writes its save name one byte
  past the buffer (`8a5cc6c`).
- **Changed:** all source is UTF-8 with LF line endings and formatted by
  clang-format (`27835ea`); the kernel builds the same.
- **Fixed:** A and L+START do nothing in an empty SD folder (`d7dda3e`). They
  acted on a stale entry from the folder listed before: A opened its launch
  menu or started a GB, GBC or NES game. Ported from DS Style v7.4.
- **Changed:** names are drawn in ASCII (`a47a7a7`, `eff3a93`, `93092e0`,
  `6711497`). The 12x12 GBK font and FatFs's code page 936 go, freeing
  about 350 KB of ROM. FatFs hands names over as UTF-8, so every file lists,
  opens and saves under its own name on FAT32 and exFAT; accented Latin
  letters show without their accents, other scripts as `?`. Cheat files and
  Recently Played are read byte for byte, so `.cht` files in Latin-1 or GBK
  still parse. Upgrading: the save the old kernel still has to write back
  and the Recently Played list keep names in GBK, which no longer match. If
  the last game you played has a non-ASCII name, switch on once with the old
  kernel first so it writes the save back.
- **Removed:** Chinese (`c05855f`). English is the only language until language
  packs arrive; a saved Chinese setting loads English. Cheats come from
  `/CHEAT/Eng`, and the fast patch engine is no longer off for Chinese.
- **Fixed:** a game's brightness survives the in-game menu (`5b10b34`). The
  menu zeroed BLDY, which is write-only, so a game that dims the screen
  once (Pokémon Ruby's title menu) came back at full brightness. It now
  turns blending off through BLDCNT and restores it on exit. The BG2
  rotation registers are still reset, as the menu draws on BG2.
- **Fixed:** the in-game menu, cheats, reset and sleep work in rev 1 of
  Pokémon FireRed and LeafGreen, Crash Bandicoot Purple and SimCity 2000
  (`b736015`). The fast patch engine's built-in table is keyed by game code
  alone and has rev 0's offsets, so these games got unrelated words
  overwritten, and most crashed with an addon on. A table entry is now used
  only if every listed word in the ROM is an IRQ handler address; otherwise
  the kernel scans the ROM. Blank game codes (homebrew) no longer match the
  table's all-zero entry.
- **Fixed:** an unset in-game hotkey (erased settings) counts as the key the
  settings page shows, L R SELECT or L R START (`b736015`). It made the key
  read as "no key held", so reset or the menu would fire on every frame.
- **Fixed:** with an addon on, Grand Theft Auto Advance, Super Monkey Ball
  Jr. and LEGO Star Wars II (EU) no longer crash or hang (`c57c3b6`,
  `dcf4c5b`). Each clears the top of IWRAM after the kernel's hook is in;
  their clear now stops just below it.
- **Fixed:** the in-game menu works in Lara Croft Tomb Raider: Legend (EU),
  which computes its IRQ handler address instead of loading it
  (`dcf4c5b`).
- **Changed:** these four per-game patches live in one table in `irqpatch.c`
  (`35613bf`). Each applies only to the exact ROM (game code, revision and
  header check) and only if the patch site holds the expected word.
- **Fixed:** Crash Nitro Kart no longer crashes after the in-game menu
  (`dcf4c5b`). The kernel moves a game's stack down to make room for the
  menu's saved registers; this game reloads its stack from a second
  literal, which is now moved too.
- **Fixed:** the in-game menu turns IME off and on as intended (`37c7d60`).
  It wrote DMA2's count register instead, leaving it at 1 after every menu.
  Loading a save state turns IME back on too (`110277b`), since the state
  is saved while the menu has it off.
- **Fixed:** the IRQ patch keeps up to 64 stores, not 32 (`bd61eec`).
  Kirby: Nightmare in Dream Land has 42 listed, and the rest stayed
  unpatched. Cached `/PATCH/*.pat` files of the old layout are scanned
  again, and a `.pat` is no longer read whatever its size.
- **Changed:** of several built-in table entries for one game code, the
  first that matches the ROM is used (`c69aa23`), so FireRed rev 1 gets its
  own entry; lowercase game codes count. The exact-ROM patches run only on
  SD launches and check the header against the game code.

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
