# Torii

A custom kernel for the EZ-Flash Omega Definitive Edition, in the making.
Named after the shrine gate you pass through into another world, like every
game you launch. Not affiliated with EZ-Flash.

It starts from EZ-Flash's official 1.06 source, with its bugs fixed, a new
menu drawn by a 60 fps tile engine, and the groundwork under it: a
reproducible build, an emulated cart to run the kernel on, and tests.

## Download

Get `ezkernelnew.bin` from the
[latest release](https://github.com/ErikBavenstrand/torii-kernel/releases/latest),
or from the `ezkernelnew` download of any
[CI run](https://github.com/ErikBavenstrand/torii-kernel/actions). Copy it to
the root of the cart's SD card, hold R while switching the GBA on, and confirm
the update.

## The menu

Home is a carousel of the places on the card: **Recent** and **Favourites**
(while they hold games), one item per console the card has games for (GBA,
GBC, GB, NES), **SD card**, **NOR**, **Settings** and **Info**. LEFT and
RIGHT turn it, A opens the item facing you, and B on any page goes back to
it. Info's Controls page lists every key.

- **Game lists** (a console, Recent, Favourites): UP/DOWN move,
  LEFT/RIGHT jump to the next or previous first letter, A plays the game
  as Settings' GBA boot mode says (with the addons Settings turns on, or
  clean), and START opens its menu: Play, Favourite, Clean boot, Boot with
  addon, Rename (a name of your own, kept in `/torii/names.txt`; the file
  stays), Game info (the save type, and a boot mode of the game's own) and
  Delete (the save stays). Hold L as you press A, or as you pick a way to
  start in the game's menu, and it starts with the GBA's boot screen; L and
  R do nothing else here. A floppy mark beside the size means the game has
  a save. Games show your own name if you gave one (Rename, or
  `just names-add`), else No-Intro's name with `just names-update`, else
  their cleaned file names. No-Intro names a game only by its CRC: a
  romhack or translation keeps its own file name and the placeholder art,
  never its base game's.
  Settings' Game view shows a list, or a list with box art (`just art-pack`
  once, then `just art-add` for romhacks; `just art-scrape`).
- **SD card:** the card's folders as they are, folders first. A opens a
  folder or starts a game (L held: a hard reset), LEFT/RIGHT page, B goes
  up a folder, START opens the file's menu (Launch, Clean boot or Boot
  with addon, Write to NOR, favourites, Rename, which renames the
  file with its save, Delete, File info). Torii's own folders and files it
  can't start stay hidden unless Settings' Show all files is on.
- **NOR:** what the cart's 64 MB game NOR holds, its games (A starts one,
  START opens Launch, Game info, Delete) and Format all.
- **Settings:** one page of sections (Look, Clock, Games, Cart, Hotkeys,
  Library, System). A changes the selected value in place and A again
  keeps and saves it (B puts it back), L/R jump between sections. Look has
  the light and dark themes.
- **Info:** Controls, In game (the hotkeys), Your cart, About, and
  Diagnostics.

At power-on the carousel faces the item you were last on, such as GBA.
Settings' Start on picks an item to face instead, from those home shows;
if that item is gone (no NES games any more, say), the carousel faces the
item you were last on. A library index of the card's games (`/torii/library.idx`)
is rebuilt in the background when the card changes; `/torii` also keeps
`recent.txt`, `favorites.txt` and the logs.

## Setup

- Docker and [just](https://github.com/casey/just). On macOS, install
  [colima](https://github.com/abiosoft/colima): the recipes start a VM of
  their own, `torii`, that shares the repo over virtiofs, so containers see
  edits at once (colima's default sshfs shows them up to 20 seconds late).
- To run the emulator: CMake, pkg-config and SDL2
  (`brew install cmake pkgconf sdl2 lua@5.4`, or `apt install cmake pkg-config libsdl2-dev liblua5.4-dev libedit-dev`).

## Use

| Command | Result |
|---|---|
| `just build` | `dist/ezkernelnew.bin`, the file you flash to the cart |
| `just sd /Volumes/OMEGA [game.gba...]` | copies the kernel (and games) to a real SD card and ejects it; hold R while switching on to install |
| `just logs /Volumes/OMEGA` | copies the kernel's logs off a real SD card into `dist/logs/torii.log`, naming the code addresses in crash dumps (see below); `--tag v0.2.1` names them with that release's symbols |
| `just names-update /Volumes/OMEGA` | writes the game names database to a real SD card: `/torii/names.db` from the No-Intro dats of [libretro-database](https://github.com/libretro/libretro-database) (CC BY-SA 4.0; downloaded once to the git-ignored `dist/dats`, never bundled) and the CRCs of the card's games in `/torii/crc.txt`, so the lists show No-Intro names. A game is named only by its CRC: a romhack, a translation or a patched dump the dats don't list keeps its file name. Use a kernel from v0.4.2 on: an older one forgets each CRC whose date this PC sees hours off the cart (a file dated in winter, in summer) |
| `just names-add /Volumes/OMEGA names.tsv` | gives many games names of your own at once: each line of the file is a game's file name (with or without its extension, any case), a tab and its name (an empty name removes it); every game on the card with that file name gets it, in `/torii/names.txt`, where Rename keeps the names it gives. It prints the lines that named no game; `--dry-run` writes nothing |
| `just art-add /Volumes/OMEGA` | makes box art for many games at once: put pictures (PNG, JPEG, ...) named after your games' file names, with or without the extension (`Pokemon - Glazed.png` for `Pokemon - Glazed.gba`), in the card's `/torii/art/custom-src` (or give another folder after the card), and each becomes its game's custom art, `/torii/art/custom/<file name>.bin` (cropped to a square, 80x80, 15 colours dithered). It prints which picture went to which game and the pictures that matched none; a second run converts only pictures that changed (`--force` all, `--dry-run` none). Custom art wins over matched art, so it is how a romhack gets a box of its own. Use pictures you may use, and don't publish box art |
| `just art-add "Game.gba" picture.jpg /Volumes/OMEGA` | the same for one game, from any picture, as `/torii/art/custom/Game.gba.bin`; the game lists' "List + art" view shows it. The view also shows matched art the card holds as `/torii/art/<CRC32>.bin` (`just art-scrape`) |
| `just art-scrape /Volumes/OMEGA` | fetches box art for the card's games from [libretro-thumbnails](https://thumbnails.libretro.com) and writes it as `/torii/art/<CRC32>.bin`, matching each game by CRC (computed as `names-update` does, into `/torii/crc.txt`) to its No-Intro name in the dats; custom art and art already on the card stay. A game the dats don't list (a romhack) gets none, unless it is a GBA game whose file name is the title of the game its header's code names (a patched dump). It deletes the art v0.4.1's scraper gave romhacks (their base game's box, under the romhack's own CRC); `--keep-stale` only lists it. Custom art is never touched. It also deletes the placeholders v0.4's first builds made, `/torii/art/box-gba.bin` and so on, for every console whatever `--console` says: a game without art shows the kernel's own drawn box for its console. One request at a time, half a second apart; pictures are cached in the git-ignored `dist/art-cache`, so a second run downloads only what's new. It prints how many games per console have art. `--dry-run` shows what it would fetch, `--offline` uses only the cache, `--console GBA` limits it. Box art has no licence grant: it is fetched on your PC for your card; never commit or publish it |
| `just play [game.gba...]` | runs the kernel on an emulated cart, first copying any games given onto its SD card unless they are already there |
| `just dev` | like `play`, rebuilding whenever `source/` changes |
| `just test` | host and kernel unit tests, then homebrew games through the kernel: boot, every save type across power cycles, crash reports and save states |
| `just art-pack /Volumes/OMEGA` | run once: builds `/torii/art.pak`, box art for every No-Intro GBA, GBC and GB game (NES too when the card has NES games, or with `--console`) that [libretro-thumbnails](https://thumbnails.libretro.com) has, about 15 MB, so an official game you put on the card later gets its cover on the cart with no PC. The kernel tries custom art first, then `/torii/art/<CRC32>.bin`, then the pack; a romhack is in no dat, so it still needs `just art-add`. Each picture is stored once, however many CRCs (regions, revisions) share it. 256 requests at a time over kept-alive connections, each picture converted on every core as it arrives; pictures, conversions and the names the server lacks are cached in the git-ignored `dist/art-cache`, so a run that stops (Ctrl-C, the server asking for fewer requests) still writes what it has, and the next run picks up where it left off; it shows progress and the time left. The first run makes about 9,700 requests and takes about 35 seconds on a 14-core Mac, with a cache of about 1.6 GB; `--offline` uses only the cache. Box art is the publishers' work: the pack stays on your card; never commit or publish it |
| `just cart-add game.gba` | copies a file onto the emulated SD card |
| `just game-add PATH...` | copies games you own (files or whole folders) into the git-ignored `roms/` and lists them in `roms/games.toml` |
| `just test-host` | builds the hardware-free kernel code (sorting, cheat parsing) for Linux in the tools container with AddressSanitizer and UndefinedBehaviorSanitizer and runs `tests/host` (also part of `just test`) |
| `just test-kernel` | builds a test kernel that runs the unit tests in `tests/kernel` at boot (also part of `just test`) |
| `just compat` | plays each listed game through the kernel and checks it boots, gets the right save type and clock, and keeps its saves; prints a table and writes `dist/compat/index.html` with screenshots |
| `just size` | kernel size against the 1 MB kernel area, IWRAM and EWRAM, plus the largest symbols; fails if anything is over |
| `just stack` | the largest stack frames and a static estimate of the deepest call chains |
| `just lint` / `just format` | checks the C (clang-format), Python, shell scripts and CI workflows / formats the C files in place |
| `just analyze` | advisory static analysis of `source/` with cppcheck, clang-tidy and GCC's `-fanalyzer`; fails only on findings missing from `tools/analyze-baseline.txt` (`just analyze-baseline` rewrites it) |
| `just font` / `just font-preview` | converts the menu fonts in `assets/fonts` to C in `source/engine` (`tools/font.py`; committed) / redraws the font candidates' preview sheets in `docs/research/fonts` |
| `just palette` | converts the menu's colours, `docs/design/v0.4/palette.json`, to C in `source/ui/palette.c` (`tools/palette.py`; committed) |
| `just compile-commands` | writes `compile_commands.json` for the kernel and its unit tests, so clangd (editors, agents) can jump to definitions and show errors; `.clangd` adapts the GCC flags for clang |

`just --list` shows the rest.

The build uses devkitARM r54, the closest available release to the r53
EZ-Flash used, so the compiled kernel isn't bit-for-bit their file. To flash
exactly the official kernel, use the `ezkernelnew.bin` in the repo root.

The build is reproducible: the same source gives the same `ezkernelnew.bin`
on any machine, which `just repro` (and CI) checks by rebuilding from a
shuffled copy in another folder. Each kernel carries a build ID, the CRC32
of its ROM, which its log lines show; releases attach `ezkernelnew.sym` and
`ezkernelnew.elf`, and `just logs` names crash addresses only with symbols
of the same ID.

## Logs and Diagnostics

To bring back a report from a real cart, set Logging (Settings, System) to
Errors, Info or Debug. The kernel then writes what it does to
`/torii/logs` on the card: every boot, every launch with the patches it
applied, errors, and how the previous session ended. Off, the default, it
writes nothing. A kernel crash is kept in cart RAM and logged at the next
boot, even with Logging off. With Logging on, the in-game menu has a DEBUG
entry that saves the game's registers and memory for the next boot's log.

Info's Diagnostics row (or L+R+SELECT held while switching on) runs
**Diagnostics**: the unit tests, hardware probes and timings, then a few
questions; [docs/release-checklist.md](docs/release-checklist.md) lists
them. Read the results with `just logs`.

## The emulated cart

`emu/` adds a model of the Omega DE cart to the mGBA emulator: SD card, NOR,
PSRAM, game loading, saves (SRAM, Flash and EEPROM) and the clock. The real
kernel boots on it, launches games and writes saves back to SD. The cart's SD
card, NOR and SRAM live in `dist/cart/` and persist between runs.

Keys: arrows, X = A, Z = B, A = L, S = R, Enter = Start, Backspace = Select.
The window opens at 4×; `just scale=3 play` changes it.

Kernel code can log with `tlog("...")` (`source/tlog.h`), which writes to
mGBA's debug console: the lines show in the terminal under `just play` and as
`kernel: ...` from the headless runner.

Run with `OMEGA_TRACE=1` to log what the kernel and games do with the cart
(register writes, launches with their save type, SD reads and writes, mode
switches, clock use) to stderr.

`just compat` tests the games in `roms/games.toml`: one game per set of
requirements (system, save chip, kernel save mode, ROM size, clock, other
hardware), since Ruby and Sapphire, say, need the same of the kernel; `just
compat --all` tests every game. Each game boots through the kernel while the
test presses START and A; the screen must change, the save type must match
the ROM, a clock game must find the clock on, and the save must reach
`SAVER/` and come back on relaunch (a test pattern stands in when the game
doesn't save on its own). `dist/compat/index.html` shows the results, what
changed since each game's previous run, coverage by requirement, and
screenshots. Add your games with `just game-add ~/path/to/roms`: it copies
the ROMs (not their saves) into the git-ignored `roms/` and lists them in
`roms/games.toml`, which stays local too; tests read only `roms/`. Mark ROMs
to leave out with `skip = "why"`.

When code running headlessly hits an illegal opcode or jumps somewhere that
can't run code, `mgba-shot` stops with exit status 3 and prints a crash report:
the registers and the code addresses on the stack, named from
`dist/ezkernelnew.sym` (or the ROM's own `.sym`). `just compat` counts a
crash as a failure.

To step through the kernel:

- `just debug` opens the window and waits; `just gdb` in a second terminal
  attaches gdb with the kernel's symbols and source (`break main`,
  `continue`, `next`, `bt`, `watch`).
- `just debug-shot -f 600` does the same headlessly, taking `mgba-shot`
  options (keys, screenshots); `just gdb -batch -ex 'break tlog' -ex
  continue -ex bt` scripts a session.
- `just debug-cli` uses mGBA's own debugger in the terminal instead
  (`break main`, `watch/w ADDR`, `x/4 ADDR`, `trace 20`, `help`). `just
  build` writes `dist/ezkernelnew.sym`, which mGBA loads for symbol names.

Save states include the cart: its registers, data port, PSRAM and SRAM (the
NOR chips and SD card are left as they are, like disks). With `mgba-shot`,
`-x FRAME:PATH` saves one and `-l PATH` starts from one, so a test can jump
straight to a point in a game.

To drive the emulator from a script or an AI agent, `just play-remote` (window)
and `just remote` (headless, at real speed) load `tools/remote.lua`, which
takes one command per line on `127.0.0.1:8888`: `press A`, `wait 60`,
`r32 0x02000000`, `screenshot dist/s.png`, `save dist/s.ss` and more (listed
at the top of the script). For example:

```bash
printf 'press START\nwait 30\nscreenshot dist/s.png\n' | nc 127.0.0.1 8888
```

Any mGBA Lua script also runs with `MGBA_SCRIPT=script.lua` in the window or
`mgba-shot -L script.lua` headlessly.

Not modelled yet: the in-game menu's real-time saves, and firmware updates.

## Research and plans

`docs/research/omega-de-hardware.md` describes how the cart works: the
upgrade file, the FPGA firmware, the cart registers, game loading and saves.
`docs/design/` holds the menu's design (`engine.md`, `v0.4/`), and
`docs/plans/roadmap.md` what is done, what is still open, and what comes next.

## Credits

The menu's Game Boy Advance, Game Boy Color and Game Boy pictures
(`assets/ui/sprites/gba.png`, `gbc.png`, `gb.png`) are redrawn on the
outlines of [KyleBing's retro game console icons](https://github.com/KyleBing/retro-game-console-icons),
licensed [CC BY 4.0](https://creativecommons.org/licenses/by/4.0/). The menu's
fonts are [Galmuri](https://github.com/quiple/galmuri), under the SIL Open
Font License (`assets/fonts/galmuri`).
