# Hi-colour GBC games in Jagoomba Color

Status (2026-10-26, v0.7.1): investigated, not fixed. The fix is a renderer
change, not a one-day patch; this note is where to start.

## The problem

jagoombacolor#20 and #53. Some GBC games rewrite the background palettes
during the frame, usually all eight every one or two scanlines from an HBlank
handler or HDMA, to show more than 32 colours at once. The issues name Alone
in the Dark, Crystalis (title), Donkey Kong Country (title; in `roms/`),
Tomb Raider, V-Rally '99 and Cannon Fodder, plus hi-colour demos. In
Jagoomba these screens look patchy: every line gets the palettes as they
stood at the end of the frame.

## Why, in the source

- `FF69_W` in `emulators/jagoombacolor/src/lcd.s` (BCPD writes) stores the
  byte in `gbc_palette` and nothing else. It doesn't note the scanline.
- `transfer_palette_` runs once per frame from `display_frame`, at VBlank. It
  copies `gbc_palette2` (a copy of `gbc_palette`), with the gamma conversion
  if the setting is on, into GBA palette RAM from `0x05000100`: each GB
  palette's four colours go to the first four entries of a 16-colour GBA
  palette (GB BG palettes 0-7 to GBA BG palettes 8-15, OBJ palettes to GBA
  OBJ palettes 0-7). So one GB palette set is 8 bytes every 32 bytes, not
  one block.
- `lcd.s`'s own TODO list has "Palette changes per line using vcount, may
  up to 4 palettes per frame total" and "Real HDMA"; the author's comment on
  #20 (2022-06-02) says the same: palettes reach the GBA only at frame end.
- The per-line effects Jagoomba does have (scroll, `BGxCNT`, `DISPCNT`,
  `WIN0H`) go through `do_gba_hdma`, which starts three HBlank DMAs on DMA0,
  DMA1 and DMA2 from buffers filled during the frame. Only DMA3 is left.

## What a fix needs

1. In `FF69_W`, when the value changes mid-frame, record the scanline and
   either the byte or a snapshot of the 64-byte BG palette set, into a
   per-frame buffer (144 lines x 64 bytes is 9 KB; double-buffered, as the
   scroll buffers are, 18 KB of RAM the emulator doesn't have spare today).
   A change list (line, index, value) is smaller but costs more per line to
   replay.
2. Replay at each GBA line. One HBlank DMA can't do it: the GBA side of a
   palette set is eight 8-byte pieces 32 bytes apart, and DMA3 is the only
   free channel. So it takes an HBlank IRQ that copies up to 64 bytes in the
   ~270-cycle HBlank window, or a GBA palette layout where the eight GB BG
   palettes sit in one block (a change to how tiles pick their palette all
   through the renderer).
3. Gamma: `gamma_convert_one` has to run when recording, not at VBlank.
4. Scaling: in the scaled display modes GB lines map onto 160 GBA lines,
   so the replay has to follow the same line map the scroll buffers use.
5. Speed: an IRQ per line is roughly 144 x 100-200 cycles, 5 to 10% of a
   frame, on an emulator whose speed is already tight (Crystal's stutter,
   jagoombacolor#46 and #35). It should switch on only for frames with
   mid-frame palette writes.

## Suggested route

A separate branch of the patch, as the issue's author suggested: the change
list in `FF69_W` first, replayed by an HBlank IRQ, only on frames that
changed a palette mid-frame; test it on Donkey Kong Country's title screen
(`roms/`) with `just shot` screenshots against mGBA's own GBC core, then
check the frame cycles on Pokémon Crystal and the `just compat` GB/GBC games
for slowdown. Worth doing only if the hi-colour games matter to the owner:
they are a handful of titles, mostly menus and title screens.
