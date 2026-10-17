### Torii architecture: rendering, SD layout, plugins, dev tooling, prior-art inventory

> **Answer:** Use mode 0 (4 tile BGs plus sprites) with a retained window model: VWF text drawn by an ARM blitter in IWRAM, palette-index theming, covers as 8bpp sprites on their own palette, and one VBlank commit queue. Themes are data: compiled defaults, overridden key by key from theme.ini, with per-asset fallbacks. Four layers handle every common launcher style, but not all effects at once (limits below). On the SD card, put everything under one versioned system folder, keep a PogoShell-compatible plugin ABI with an explicit table, and write saves crash-safely (FatFs rename is neither atomic nor overwriting).
> **Confidence:** 🟢 high for the hardware budget and code facts; 🟡 medium for the per-glyph cost (one benchmark) and for NOR wait states, which have not been measured.

**Findings: rendering**
- 🟢 VRAM: 64 KB for BG maps and tiles, 32 KB for sprite tiles. A text BG indexes 1024 tiles. 4bpp tiles are 32 B, 8bpp tiles 64 B. [1][2]
- 🟢 Layout A fits exactly: 4bpp full-screen text (19.2 KB), 4bpp wallpaper (19.2 KB), 8bpp 120x80 art (9.6 KB), 9.3 KB of panels, and 4 maps (8 KB). An 8bpp full wallpaper plus full text plus maps is 256 B over. [1]
- 🟢 The BG palette is 255 visible colours shared by every BG. Sprites have their own 255. Photographic covers therefore belong in 8bpp sprites: about 5 covers of 80x80 at 8bpp, or 10 at 4bpp, fit in sprite VRAM. [1]
- 🟢 Effects: one blend mode and one coefficient pair per frame, no sprite-to-sprite alpha, one mosaic size, 2 rectangular windows plus an OBJ window. 128 sprites, 32 affine matrices, 1210 sprite cycles per line. [1]
- 🟢 Affine BGs are 8bpp with 256 tiles. Mode 1 trades one text BG for an affine BG. Bitmap modes have a single layer and only sprite tiles 512–1023. [1]
- 🔴 Switching the display mode mid-frame (tiles at the top, bitmap below) is undocumented and clashes with the VRAM layout. mGBA latches DISPCNT per line, but nothing shows it works on hardware. Don't rely on it. [1][6]
- 🟡 Glyph cost into 4bpp tiles: about 631 cycles in ARM from IWRAM versus 2044 in Thumb C from ROM. A 40-glyph row then costs 30% versus 98% of VBlank. The libtonc ARM blitter is about 260 B. [3][6]
- 🟢 Text BGs wrap when scrolled, so a 32x32 map can act as a ring buffer and a list scroll redraws only the new row (pokeemerald shifts the window and prints only the new rows). [1][4]
- 🟢 DMA per VBlank: about 111 KB from IWRAM, 42 KB from EWRAM, 56 KB from ROM at 3,1 wait states. BIOS SWI 12h decompresses LZ77 straight to VRAM. [1]
- 🟢 Patterns from the code:
  - pokeemerald: a window is a tile rectangle with a base tile, a palette bank and a RAM buffer, uploaded explicitly; there is a bounded VBlank DMA queue (40 KiB, VCOUNT guard) and unfaded/faded palette buffers.
  - Butano: shadow state, per-manager dirty ranges, and one commit after VBlank.
  - TTE: renderers behind drawg/erase function pointers.
  - TWiLight: theme.ini over compiled defaults, with per-asset fallback paths. [4][5]
- 🟢 Budget today: ROM 702,784 of 1 MB used; IWRAM 5,668 B free; EWRAM 400 B free. The FatFs CP936 tables take about 87 KB each, so dropping CP936 and HZK12 frees a lot of ROM. The kernel never sets WAITCNT. [6]
- 🟢 The current menu is mode 3 (`SetMode(MODE_3|BG2_ENABLE)` at ezkernelnew.c:1927). [6]
- 🟡 Fonts: Spleen is BSD-2 (fixed-width; Latin-1 at 6x12) and Cozette is MIT. pokeemerald's fonts are Nintendo assets and not usable. libtonc has no licence file. [6]

**Findings: SD layout and plugins**
- 🟢 Simple DE and DS Style plugins: the kernel looks for `/SYSTEM/PLUG/<ext>.{bin,gba,mb,mbz}`. It loads the plugin to PSRAM 0 and the ROM right after it, writes the PogoShell argv block at 0x0203FC00 (magic 0xFAB0BABE, ROM address at 0x0203FBFC) plus a fake romfs header, and runs .mb/.mbz files from EWRAM. Emulator saves go to `<stem>.esv`, so ROMs with the same name in different folders collide. [7]
- 🟢 PogoShell's pogo.cfg line format is `<ext> <icon> <handler> <savetype> <desc>`. Goomba reads the ROM address from 0x0203FBFC. [7]
- 🟢 Other systems:
  - TWiLight: per-game `gamesettings/<rom>.ini`; saves in `saves/` next to the ROM by default, with options for beside the ROM or central.
  - MinUI: `/Saves/<TAG>/<rom>.sav` and `/.userdata/`.
  - Analogue Pocket: Assets, Saves and Settings mirror each other per platform.
  - RetroArch: art is keyed by name with three fallbacks because names drift. [7][8]
- 🟢 FatFs f_rename refuses to overwrite (FR_EXIST) and is not atomic: it registers the new entry, then removes the old one. A safe save writes .tmp, syncs, keeps a .bak, renames, and recovers at boot. The appnote warns that power loss mid-write can break the FAT. [7][9]

**Findings: dev tooling**
- 🟢 The toolchain is GCC 10.1 (it has -fanalyzer, experimental). CFLAGS are just `-g -Wall -Os`. There is no .clang-format, .editorconfig, .clangd or .gitattributes, `just lint` skips C, and there are no host tests. [10]
- 🟡 Recommended setup, in order:
  1. Ratchet warnings: -Wextra, -Wshadow, -Wundef, -Wformat=2, -Wstrict-prototypes and -Wmissing-prototypes, with FatFs exempt and -Werror once clean.
  2. Add -fstack-usage.
  3. Try gc-sections and compare with `just size`.
  4. Pin clang-format via uvx and run it in `just lint` and CI.
  5. Add a Claude PostToolUse format hook.
  6. Add a .clangd.
  7. Add host tests under ASan/UBSan for the parsers, sort and cheat decoders.
  8. Run cppcheck and clang-tidy as advisory checks.
  9. Run -fanalyzer occasionally.

  Skip MISRA, IWYU, pre-commit and LTO. [10]

**Findings: prior-art inventory**
- 🟢 DS Style descends from Simple DE. About 55 features, settings and fixes are catalogued in `prior-art.md`. [11]
- 🟢 Bugs found in passing:
  - The official recents writer writes 11 rows at the 10-row limit and never truncates.
  - Simple DE dropped RESET_EWRAM from the boot reset, which DS Style fixed.
  - The cheat table has no cap.
  - DS Style cut the folder limit to 96. [11]

**Open questions**
- ❔ Omega DE NOR wait states, and the measured glyph and LZ77 timing on the real cart.
- ❔ Whether mid-frame mode switching works on hardware.
- ❔ libtonc's licence; the alternative is to write our own blitter based on its design.
- ❔ The source of the DS Style txt/md viewer plugin (not in any repo).

---

**Sources**
1. [GBATEK](https://problemkaputt.de/gbatek.htm) · primary
2. [Tonc regbg/regobj](https://www.coranac.com/tonc/text/regbg.htm) · 2008 · secondary
3. [Tonc TTE](https://www.coranac.com/tonc/text/tte.htm) · 2008 · secondary
4. [pret/pokeemerald](https://github.com/pret/pokeemerald) window.c, bg.c, text.c, palette.c, dma3_manager.c, list_menu.c · primary
5. [Butano bn_core.cpp](https://github.com/GValiente/butano), [TWiLight ThemeConfig.cpp](https://github.com/DS-Homebrew/TWiLightMenu), [libtonc tte](https://github.com/devkitPro/libtonc) · primary
6. Torii repo: `just size`, source/, emu/mgba video.c · 2026-10-17 · primary
7. [Sterophonick/omega-de-kernel](https://github.com/Sterophonick/omega-de-kernel), [DS Style](https://github.com/FrankieT19/omega-de-ds-style-kernel), [Pogoshell-K](https://github.com/NuVanDibe/Pogoshell-K), [jagoombacolor](https://github.com/EvilJagaGenius/jagoombacolor), source/ff15/ff.c · primary
8. [TWiLightMenu](https://github.com/DS-Homebrew/TWiLightMenu), [MinUI](https://github.com/shauninman/MinUI), [Analogue SD structure](https://www.analogue.co/developer/docs/directories-and-sd-folder-structure), [libretro thumbnails](https://docs.libretro.com/guides/roms-playlists-thumbnails/) · primary
9. [FatFs appnote](http://elm-chan.org/fsw/ff/doc/appnote.html) · primary
10. [GCC 10 docs](https://gcc.gnu.org/onlinedocs/gcc-10.1.0/gcc/Warning-Options.html), [Claude Code hooks](https://code.claude.com/docs/en/hooks), [clang-format](https://clang.llvm.org/docs/ClangFormatStyleOptions.html), [cppcheck](https://cppcheck.sourceforge.io/manual.html), [Memfault flags](https://interrupt.memfault.com/blog/best-and-worst-gcc-clang-compiler-flags) · primary/vendor
11. Clones and diffs against official 12cb68b, [DS Style guide](https://frankiet19.github.io/omega-de-ds-style-kernel/DS%20Style%20User%20Guide.pdf) · primary

*deep · 2 rounds · ~60 sources read · 1 opus, 9 sonnet researchers*
