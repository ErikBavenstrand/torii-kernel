# Changelog

Changes to the kernel (`source/`) since EZ-Flash's official 1.06 source
(commit `12cb68b`), newest first. Tooling, tests and the emulator are in the
git log, not here.

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
