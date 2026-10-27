# In-game menu: the current menu, save states and prior art

Research for v0.8 (2026-10-25).

## Findings

> **Answer:** (Historical: written before v0.8, whose overlay menu replaced this one.) The menu then was a Mode 3 bitmap menu on BG2. It lives in a 4–8 KB blob in the game's ROM tail in PSRAM and is reached through the game's patched IRQ vector. It keeps one 448 KB state per game file name in cart SRAM pages 0x40–0xAF, which have no battery. Nothing in the kernel writes that state to SD, and it is unknown whether the FPGA does. An overlay can be built (a tile layer instead of BG2, its own font and stack, one more SRAM area to back up), but multiple slots must live on SD, and whether states survive a power-off has to be tested on the real cart first.
> **Confidence:** 🟡 medium — the code facts are primary; how the state reaches SD is unverified and decides the slot design.

**Findings**
- 🟢 The hook rewrites the game's literals for the IRQ vector at 0x3007FFC to 0x3007FF4 and puts `RTS_irq` at 0x3007FFC. It acts on the VBlank or keypad IRQ, and passes on the rest [1][2].
- 🟢 118 of 118 commercial games pass the hook after fixes, in the emulator only. A game that never enables VBlank gets no menu [2].
- 🟢 The patch area is 0x1000 bytes, or 0x2000 with cheats (600-cheat cap). It sits at the ROM's trimmed end and is clamped to 32 MB: a full 32 MB ROM leaves no room [3].
- 🟢 The hotkey masks (L+R+Start for the menu, L+R+Select for sleep) are patched in from Settings. The menu can't be turned off; sleep can [1][3].
- 🟢 (Before v0.8) The menu draws in Mode 3 on BG2 with an 8x10 font covering A–V only, in fixed colours. It backs up 0x12C00 bytes of VRAM to SRAM pages 0x20/0x30 [1][5].
- 🟢 It resets BG2PA–PD and BG2X/Y, which are write-only, and never restores them. That is the roadmap's "BG2 affine" item; a tile layer on BG0, BG1 or BG3 avoids it [1][2][6].
- 🟢 SAVE writes EWRAM, IWRAM, palette, VRAM, OAM, IO, the registers and a flag to pages 0x40–0xAF, then busy-waits 0x50000 loops. It never touches the SD registers [1].
- 🟢 LOAD's flag check is commented out (`@;bne errorRTS`), so an empty state (all 0xFF) is loaded: the crash [1].
- 🟢 LOAD restores only DMA1's control register, and sound comes back by hand. EZ-Flash notes the sound can turn to noise after a load [1][2].
- 🟢 The state file is `/RTS/<rom name>.rts`, 0x70000 bytes, keyed by file name only: two games with the same name share one. It isn't tied to v0.5's save slots [4].
- 🟢 At launch the CPU copies the .rts into SRAM and sends its FAT run table (+0x300) to the FPGA. A fragmented .rts fails the launch with error 6 [4].
- 🟢 SRAM pages 0x20 and up survive a soft reset but not a power cycle (real cart, 2026-10-18) [5].
- 🟡 The state reaches SD only if the FPGA writes it through using the +0x300 table. The kernel never does it, the vendor PDF doesn't say, and the emulated cart doesn't model it [4][7][8].
- 🟢 The state path has no cutpoints, no safefile and no tmp-then-rename; the .sav path has all three [4].
- 🟢 Free SRAM is 0xB0–0xE0, about 256 KB, less than one state, so more slots need SD [5].
- 🟢 SD writes run at 200 KB/s with a sync and reads at 1,710 KB/s: about 2.2 s to write a state and 0.26 s to read one [5].
- 🟢 RESET sets ROM page 0x8002 and soft-resets into the kernel. It already is "back to Torii"; nothing restarts the game alone [1].
- 🟢 Cheats are (address, byte) pairs with no names, under one global ON/OFF word at 0x03007FE0 [1].
- 🟢 The menu runs in IRQ mode with IME 0, polling the keys (no VBlank IRQ). Registers go to a 0x80-byte area carved off the game's stack, defaulting to 0x0203FE00 [1][3].
- 🟢 Sleep is BIOS Stop with the LCD and sound off, woken by Start+Select [1].
- 🟡 The RTC answers in game only while the GPIO port is open, which the kernel does only for clock or rumble games. Reading it from the menu could cut into the game's own transfer; a timestamp written by the kernel avoids that [4][5].
- 🟢 GB/GBC/NES states are the emulators' own, inside the 64 KB that v0.6.2's slots already hold [9].
- 🟢 The BIOS IRQ stack is 160 bytes. Timer reloads, BG2X/Y and the sound FIFOs can't be read back [6].
- 🟢 Prior art, EverDrive GBA PRO: Save State, Load State, Cheats, Reset Game, Exit Game; up to 98 slots per game with a timestamp and a screenshot; a held combo of 2+ buttons, set in Options (default SL+SR); hooks the IRQ from a per-game patch database, with a Safe mode that has no menu [10].
- 🟢 Prior art, Jagoomba and PocketNES: their L+R menus stop the sound, slide a separate BG layer in and offer states and sleep. The emulator owns the PPU, so this isn't a model for native games [11].

**Open questions**
- ❔ Does a SAVE survive a power-off? Test: SAVE in game, power off, check `/RTS/<game>.rts` on a PC. This decides whether slots need the FPGA's write-through or a kernel write on return to menu.
- ❔ Can the in-game menu send a new FAT table, so a slot can be swapped without a reboot?
- ❔ The `0x096C0000` "auto-save" register: does it control the write-back?
- ❔ Could PSRAM past the game's end be paged in from the menu, for spare space?
- ❔ Shrinking the state (skip unused EWRAM, compress) would cut 2.2 s SD writes.
- ❔ Simple DE and DS Style aren't on GitHub search; whether they have an in-game menu is unknown.

---

**Sources**
1. [source/gba_rts_patch.s](../../source/gba_rts_patch.s) · HEAD 2e6f038c · primary
2. [docs/research/hook-coverage.md](../../docs/research/hook-coverage.md) · 2026-10-17 · primary
3. [source/GBApatch.c, GBApatch.h, cheat.h, irqpatch.h](../../source/GBApatch.c) · HEAD · primary
4. [source/GBApatch.c:771-821, ezkernelnew.c:688-715, 1648-1659](../../source/ezkernelnew.c) · HEAD · primary
5. [docs/research/omega-de-hardware.md](../../docs/research/omega-de-hardware.md) · 2026-10 · primary
6. [GBATEK](https://problemkaputt.de/gbatek.htm) · undated · primary (read through a summarizing fetch)
7. EZ-FLASH OMEGA DE DOCUMENT.pdf (repo root) · undated · vendor
8. [emu/mgba/src/gba/cart/omega.c:715-741](../../emu/mgba/src/gba/cart/omega.c) · HEAD · primary
9. [CHANGELOG.md](../../CHANGELOG.md) v0.6.1–v0.6.2 · 2026-10-25 · primary
10. EverDrive GBA PRO user manual (PDF, krikzz via stoneagegamer.com) · 2026-04-20 · vendor
11. emulators/jagoombacolor/src/ui.c, pocketnes/src/ui.c · pinned · primary

*deep · 2 rounds · ~30 sources read · sonnet ×4, opus ×1*
