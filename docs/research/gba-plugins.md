### Plugins other than emulators, and what to build into Torii itself

> **Answer:** The non-emulator plugins in the wild are image viewers (BMP/JPG/PCX), music players (MOD, MIDI, NSF, VGM, GSM, Krawall, Maxmod), a text reader, a Z-machine interpreter and a Markdown viewer, almost all from the PogoShell era with unstated licences. Of these, a text/Markdown reader and a JPEG/BMP viewer are cheap to build into the kernel with clean-licence code. A tracker player only just fits IWRAM, and MP3 isn't feasible. The PogoShell ABI is fully known from Simple DE's loader and the MIT libpogo source, so Torii can host the existing plugins unchanged.
> **Confidence:** 🟢 high for the ABI and costs from owner docs; 🟡 medium for the plugin inventory's quality.

**Findings: the PogoShell ABI (what Simple DE and DS Style do)**
- 🟢 Lookup happens before the built-in types, so `/SYSTEM/PLUG/gba.bin` would hijack GBA ROMs. The order is `.bin`, `.gba`, `.mb`, `.mbz`, and nothing is refused [1].
- 🟢 PSRAM layout [1]:
  - The plugin sits at PSRAM 0, with the ROM after it at the plugin size rounded up to 4.
  - The romfs table comes next, 32 KB aligned: `0xFAB0BABE`, then `0x50|0x80000000`, then two 40-byte entries (32-byte name, size, offset relative to the table).
- 🟢 Argv block [1][2]:
  - `0x0203FC00`: `0xFAB0BABE`, then argc = 2.
  - From `0x0203FC08`: the strings `/plugin`, then `/rom`.
  - `0x0203FBFC`: the ROM address (`0x08000000` plus the plugin size).
  - `0x0203FBF8`: the ROM size.
  - FluBBa's emulators read it this way (`GBA_Shared/FileHelper.c`, MIT) [3].
- 🟢 Boot modes [1]:
  - `.mb` is copied to EWRAM and run there.
  - `.mbz` is BIOS-LZ77-decompressed to EWRAM and run there.
  - `.bin` and `.gba` boot from PSRAM like a game.
  - EWRAM is cleared up to `0x0203FBF8` before any of them runs.
- 🟢 Saves are `/SAVER/<name>.esv`, 64 KB (save mode 0x10). Torii's slots replace this [1].
- 🟡 There is no return path: the plugin resets or jumps to `0x08000000` [1][2].
- 🟡 A likely bug in Simple DE: argv[0] is built from `plugin + 9`, giving "/LUG/x.ext" [1].
- 🟢 libpogo (Pogoshell-K) is MIT; Simple DE and DS Style are Apache-2.0, but their loader code carries no attribution to Kuwanger [1][2].

**Findings: plugins that exist**

| Kind | Files in SimpleLight / DS Style `SYSTEM/PLUG` | Size | Notes |
|---|---|---|---|
| Images | bmp.mb, jpg.mb/jpeg, pcx.mb, lz.mb, raw.mb, ap.mb | 12-19 KB | 🟢 [4] |
| Music | mod.bin, mid.bin, gsm.bin, wav.bin (GSM 6.10 only), k3m.bin (Krawall), sb (Maxmod), nsf.mbz, vgm/vgl/vga, bgf, cwz, mda | 17-79 KB | 🟢 [4]; no MP3/OGG/XM/IT player found 🟡 |
| Text | txt.bin (ps2txt lineage, includes a font), md.bin / markdown.bin (DS Style's bundled reader) | 433 KB (txt) | 🟢 [4][5] |
| Interactive fiction | zcode.bin (Z-machine); GBA Frotz, Level 9, Magnetic Scrolls, GBAGI (Sierra AGI) | 80 KB | 🟡 [4][6] |
| Homebrew bundled by Simple | XBill, Sega Tween, HuZERO, 1968, Kaboom!, SwanDriving… | — | 🟡 [4] |

- 🟡 PogoShell-K has an internal JPEG viewer (decoded image at most about 252 KB) and dropped its mod player because of its licence [2].

**Findings: what building into the kernel would cost**
- 🟢 Image viewer:
  - picojpeg is public domain or MIT, with about 2.3 KB of work memory [7].
  - TJpgDec takes 3.5-8.5 KB of code and a 3.5 KB work area, under a custom free licence [8].
  - Either fits in the 128 KB scratch buffer and under 10 KB of ROM. It needs a bitmap or tile path, which the UI doesn't have today.
- 🟢 Music player (Maxmod, ISC):
  - It needs 6.5 KB ROM, 1.8 KB EWRAM and 5.7 KB IWRAM, about all the IWRAM free today.
  - CPU is about 2% per channel at 16 kHz, so an 8-channel MOD takes about 20%.
  - It plays its own soundbank format, not raw .mod or .xm files [9][10].
- 🟢 Krawall is LGPL-2.1+ and plays XM/S3M only [11].
- 🟡 GSM playback is cheap, but the decoder's toast licence requires shipping its notice [12].
- 🔴 MP3: there's no GBA measurement; it needs more than 30 KB of RAM, and the closest working port (DS Helix) uses a second CPU. It's not feasible in the kernel [13].
- 🟡 Text/Markdown reader: Torii already has VWF fonts, Latin-1 and a tile UI; DS Style ships one. It's the cheapest useful built-in.

**Open questions**
- ❔ Where DS Style's Markdown reader comes from, and its licence.
- ❔ The licences of the mod, mid, txt and zcode plugins.

---

**Sources**
1. [Sterophonick/omega-de-kernel `source/ezkernelnew.c`](https://github.com/Sterophonick/omega-de-kernel) (lines 1720-1925, 2064-2077, 3138-3322; DS Style has an identical copy) · 2026-03 · primary
2. [NuVanDibe/Pogoshell-K](https://github.com/NuVanDibe/Pogoshell-K) `libpogo/source/device.c`, `core.c`, `LICENCE` · primary
3. [FluBBaOfWard/GBA_Shared FileHelper.c](https://github.com/FluBBaOfWard/GBA_Shared) · 2026 · primary
4. [Sterophonick/SimpleLight `SYSTEM/PLUG`](https://github.com/Sterophonick/SimpleLight) · 2026-03 · primary
5. [FrankieT19/omega-de-ds-style-kernel](https://github.com/FrankieT19/omega-de-ds-style-kernel) · v7.4, 2026-09-07 · primary
6. [GameBrew: PogoShell, ps2txt](https://www.gamebrew.org/wiki/PogoShell_GBA) · secondary
7. [richgel999/picojpeg](https://github.com/richgel999/picojpeg) · primary
8. [TJpgDec, elm-chan.org](https://elm-chan.org/fsw/tjpgd/00index.html) · primary
9. [devkitPro/maxmod licence](https://github.com/devkitPro/maxmod) · primary
10. [BlocksDS Maxmod memory and CPU usage](https://blocksds.skylyrac.net/maxmod/md_documentation_2memory__usage.html) · primary
11. [sebknzl/krawall](https://github.com/sebknzl/krawall) · primary
12. [pineight GSM player](http://pineight.com/gba/gsm/) · primary
13. [Helix-OSDL](https://osdl.sourceforge.net/main/documentation/OSDL/OSDL-generic/Helix-OSDL/helix-osdl.html) · secondary

*deep · 2 rounds · ~40 sources read · 4 researchers on sonnet*
