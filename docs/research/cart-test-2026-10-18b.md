# Real-cart test of v0.2.1, 2026-10-18

The same cart as the first test (`cart-test-2026-10-18.md`): an original Omega DE, FPGA firmware
7 (LX16 Rev.B), kernel NOR id 2202, game NOR id 223D. It ran the v0.2.1 release (build ID
`74515D2B`) with LOGGING at DEBUG, over 14 boots and one Diagnostics run. The logs, saves, RTS,
cheat and patch files are kept in `dist/cart-test-20261018b/` (not committed). The v0.2.1 part
of `logs/torii.log` starts at its line 338; the lines before are v0.2.0's from the first test.

## Confirmed on hardware

- **Launching:** Pokémon Emerald with CLEAN BOOT and with BOOT WITH ADDON (the IRQ hook from
  the built-in table, three patched words), Crash Bandicoot XS (EEPROM 8K), the EEPROM probe
  ROM four times, and Hello with ADDON and a cheat file (the IRQ hook from a scan of the ROM).
  Every launch reached "starting the game".
- **Saves survive power cycles:** Emerald's Flash 128K save was written back on each next
  power-on and loaded again on the next launch, and Crash's EEPROM save from the first test
  loaded and was written back. Emerald's `.sav` (CRC32 `FC6A4858`) matches the save chip's
  CRC that Diagnostics read twice.
- **Declining a save:** B at "Save sav?" kept the old `.sav` and logged "B, not saved".
- **Save logging:** every "Save sav?" answer, write (with its size and `f_close` result),
  blank save made and pending-save record is in the log.
- **Junk log recovery:** v0.2.0 had left junk after the text of `torii-0.log`. The first boot
  logged "torii-0.log has junk after its text; going on in the next file" and carried on in
  `torii-1.log`, whose text is followed by zeros to its end.
- **WAITCNT `0x4018` in the menu:** browsing, launching and Diagnostics all worked with it, and
  games started at `0x0000`.
- **Build ID and crash dump:** Diagnostics confirmed the build ID matches the ROM. Its
  deliberate crash test was caught and dumped, and `just logs` named its addresses
  (`Diagnostics+0xC14`, `delay+0x0`, `main+0x132`).
- **Diagnostics:** the unit tests (20/20), the clock, PSRAM, NOR reads, the save chip (read
  twice, untouched), cart SRAM pages, timings and the SD write test all passed. The summary
  named each failed item.
- **Clock:** the checklist's clock was live and right (11:39:29, answered yes), and the clock
  kept time while the console was off.

## What failed, and why

- **In-game menu and sleep:** neither opened. The console's L button is laggy: Diagnostics'
  button step ended with "keys never pressed: 200" (L). The hotkeys also needed KEYINPUT to
  equal the combo exactly, so any extra or stuck key blocked them; v0.2.2 matches the combo's
  keys only. Save states, the cheat test (Hello's cheat needs the menu's CHEAT ON) and DEBUG
  depend on the menu, so they weren't tested.
- **EEPROM probe:** each run applied the right save type and reported back, but every block
  it wrote read back with its last data byte as `FF`, so each verdict said "doesn't read
  back". Bytes 0–6 (the block and the run's nonce) were right. Crash's real saves keep byte 7,
  so the probe's timing is the likely cause: it polled the EEPROM right after the write's stop
  bit ("ready after 0 polls"), while Crash's EEPROM_V122 library spends over 100 cycles in a
  timer setup first. v0.2.2's probe waits before polling, tries four ways to write, and the
  kernel accepts a match on bytes 0–6.
- **Diagnostics' EEPROM item** turned green on any "eeprom probe:" line, though every run
  failed. v0.2.2 checks the verdict.
- **Save size:** the probe's `.sav` was made at 512 bytes under EEPROM 512, then kept at 512
  bytes when launched as EEPROM 8K. Block 1023, which the chip held, was lost at power-off.
  v0.2.2 grows such a file with `FF`.
- **NOR list:** "list read from an older kernel's copy A, 1 games" was logged on every read,
  up to three times a menu visit. v0.2.2 saves the list in the new format once.
- **Crash test report:** the boot after it said "the kernel crashed in the menu (step 0)".
- **Not tried:** GB or GBC, NES, a NOR write (answered no), DEBUG, and rumble (all four
  registers skipped).

## Hardware facts learned

- **EEPROM width is per request:** at save types `0x21` and `0x22`, 6-bit and 14-bit requests
  both read and write (byte 7 aside).
- **1024 blocks even at `0x21`:** with 14-bit requests at `0x21`, blocks 65 and 1023 kept their
  own data and didn't land on blocks 1 and 63.
- **Commercial EEPROM writes store all 64 bits:** Crash's 8 KB `.sav` has data in blocks 0–63
  and no block whose byte 7 is a stray `FF`.
- **The EEPROM answers only in game mode:** the kernel-mode probe of the first test read
  `AAAA…`; the probe ROM, started as a game, got answers.
