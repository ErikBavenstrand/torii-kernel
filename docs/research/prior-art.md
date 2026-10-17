# Prior art: what Simple DE (S) and DS Style (DS) add over official 1.06

DS Style descends from Simple DE. Value and complexity are rated low, medium or high (V/C). Pick = my recommendation for Torii.

| # | Group | Item | Kernel | What it does | V/C | Pick |
|---|---|---|---|---|---|---|
| 1 | Browse | List / List+Art / carousels | DS | 4 view modes, Select cycles | high/very high | ✅ as theme layouts on the new renderer |
| 2 | Browse | Start screen | DS | Home screen: SD, NOR, Settings, featured game, last played / favourites | high/high | ✅ |
| 3 | Browse | Favourites | DS | Up to 10, FAVOURITES.TXT | high/med | ✅ unlimited |
| 4 | Browse | Recents as a virtual folder | DS | Start → recents → favourites | med/med | ✅ |
| 5 | Browse | Boot to / resume position | DS | Choose the first screen; remember the SD position | med/low | ✅ |
| 6 | Browse | Hide system files, Clean names | DS | Hide kernel files; strip extensions and (tags) | med/low | ✅ |
| 7 | Browse | Empty-folder messages | DS | Friendly text instead of a blank screen | low/low | ✅ |
| 8 | Browse | 512 files via a PSRAM index | DS | Index in PSRAM 0x08FF0000; folder cap cut to 96 | high/high | ✅ idea, without the folder cut |
| 9 | Browse | Per-extension icons | S, DS | 40+ icons | med/low | ✅ themeable |
| 10 | Launch | PogoShell plugin launcher | S, DS | /SYSTEM/PLUG/<ext>.{bin,gba,mb,mbz} | very high/high | ✅ ABI kept, explicit table |
| 11 | Launch | Hard-reset toggle (BIOS intro) | S, DS | Setting; L inverts it per boot | med/low | ✅ |
| 12 | Launch | Boot progress screen | S, DS | "Copying ROM…" with an MB counter | low/low | ✅ |
| 13 | Launch | Boot mode Clean/Addon/Menu | DS | Skip the launch menu | high/med | ✅ |
| 14 | Launch | Quick-start hotkey | DS | Hold a key at boot to relaunch the last game | high/med | ✅ |
| 15 | Launch | Load Style (swap kernel .bin) | DS | Stage another kernel for an R-hold install | high/med | ❌ runtime themes make it unnecessary |
| 16 | Launch | Firmware updater disabled | S, DS | Points users to the official kernel | med/low | ❓ keep the updater? |
| 17 | Saves | Rotating save backups (5) | S, DS | .sav0–.sav4 before each launch | high/low | ✅ configurable count |
| 18 | Saves | Everything under /SYSTEM | S, DS | One system folder | med/low | ✅ our own layout |
| 19 | Saves | Save name from the last extension | S | Fixes .gg/.nes save names | med/low | ✅ (superseded by the layout) |
| 20 | Art | Title 120x80 and Box 80x80 | DS | Two thumbnail styles | high/med | ✅ |
| 21 | Art | CUSTOM art for any file or folder | DS | Matched by an FNV-1a hash of the name; 256 cap | high/med | ✅ without the cap |
| 22 | Art | NOR game art | DS | From the header game code | med/med | ✅ |
| 23 | Art | Border, rounded corners | DS | Art styling | med/med | ✅ as theme values |
| 24 | Art | Console folder art | DS | Art per system folder | med/low | ✅ |
| 25 | Art | Thumbnail scraper tool | DS tool | Libretro packs to BMP | high/high | ✅ our own CLI in tools/ |
| 26 | UI | Rounded light/dark theme | S | Compile-time choice | high/med | ✅ as default themes |
| 27 | UI | 16 accent colours, Custom | DS | Runtime colours | high/high | ✅ palette theming |
| 28 | UI | Settings categories, Help pages | DS | System/Interface/Games/Hardware | med/med | ✅ |
| 29 | UI | Username in the top bar | DS | NAME.TXT | low/low | 🤷 |
| 30 | UI | UI sounds | DS | Direct Sound PCM, 6 clips | med/high | 🤷 later |
| 31 | UI | Snake easter egg | DS | — | low/low | 🤷 |
| 32 | Settings | One human-editable SETTINGS.TXT | DS | Commented key=value | high/med | ✅ |
| 33 | Settings | Hardware page (reset, backup) | S | — | med/low | ✅ |
| 34 | Emu | Jagoomba Color v0.5 | S, DS | Newer GB/GBC emulator | high/low | ✅ |
| 35 | Emu | PocketNES as a plugin | S, DS | Not built in | med/low | ✅ |
| 36 | Emu | Plugin file types | S, DS | sms gg sg sv ws wsc ngp pce z80 col c8 nes… | very high/low | ✅ |
| 37 | Media | Image/text/music viewer plugins | S, DS | External PogoShell plugins | high/n/a | ✅ through plugins |
| 38 | Media | DS text/Markdown viewer | DS | Closed 10 KB plugin | med/med | 🤷 our own later |
| 39 | Lang | 13 languages incl. Thai | DS | JSON → headers | med/high | ⏳ English now, the system ready |
| 40 | Lang | UTF-8 / accented filenames | DS | Limited Latin set | med/med | ✅ full Latin-1 |
| 41 | Cheats | Cheat screen overhaul | DS | Remembers picks, tidy titles | med/med | ✅ in the cheat milestone |
| 42 | Cheats | 128-entry cap | DS | Safety | high/low | ✅ (D9) |
| 43 | NOR | NOR in the same views | DS | — | med/low | ✅ |
| 44 | RTC | 12/24h clock, time editor | DS | — | med/low | ✅ |
| 45 | Bug | Official recents overrun | DS | 11 rows at the limit, no truncate | high/low | ✅ (D9) |
| 46 | Bug | Clean-boot RESET_EWRAM | DS | Simple regression, fixed | high/low | ✅ check ours |
| 47 | Bug | Assorted UI fixes | DS, SL | Flicker, persistence, hangs | mixed | n/a (UI is rewritten) |
| 48 | Tool | Customiser (Windows) | DS tool | GUI theme builder | high/very high | ❌ runtime themes plus docs |
| 49 | Tool | Manager (browser installer) | DS tool | Install and update an SD card | high/high | 🤷 later, `just sd` covers us |
| 50 | Build | -O -ffast-math | S, DS | — | low | ❌ |
