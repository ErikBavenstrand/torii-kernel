### Display names, CRC name database and box art for Torii v0.4 / v0.4.1

> **Answer:** Clean names by stripping `(...)` and `[...]` groups (RetroArch's "Remove () and []" algorithm, MIT-licensed), then moving a trailing ", The"-style article to the front. For v0.4.1, a PC tool should download the libretro-database No-Intro `.dat` files (CC BY-SA 4.0 repo) on the user's machine and build a small CRC32-to-name table (about 1 MB raw for GBA+GBC+GB+NES). Art comes from thumbnails.libretro.com `Named_Boxarts` by exact No-Intro name (87% of GBA dat games have art, 35% of NES), fetched on the user's machine and never bundled.
> **Confidence:** 🟡 medium — the data facts (dat contents, counts, URLs, coverage) were measured directly (🟢); licence and terms for No-Intro, thumbnails and ScreenScraper could not be confirmed from an owning page.

Measured on 2026-10-20 from libretro-database master (dat version 2026.10.07) and the live thumbnails server. Raw downloads and the coverage script are in the scratchpad (`dl/`, `cov.py`).

**Findings: name cleaning (Q1)**
- 🟢 RetroArch's "Remove () and []" label mode is `label_sanitize()` in `libretro-common/playlists/label_sanitization.c` (MIT-style licence in the file header). It drops everything from `(` to `)` and `[` to `]`, collapses consecutive spaces, trims one trailing space, and does not handle nested blocks. It is display-only: the stored label keeps its tags [1][2].
- 🟢 The same file keeps a list of No-Intro region strings and disc markers (`(CD`, `(Disc`, `(Disk`, `(Side`, `(Tape`) used to decide which tags to keep when you want only some of them [2].
- 🟢 RetroArch matches a thumbnail by three names in order: the ROM file name, the playlist label with `&*/:`<>?\|` replaced by `_`, then the label cut at the first `(` [3]. Tags therefore matter for art lookups even though they are hidden.
- 🟢 In the four libretro No-Intro dats, names follow `Title[, The] [- Subtitle] (Region[, Region]) [(Languages)] [(Rev N | vX.Y | Beta | Proto | Demo | Sample | Unl | Pirate | Virtual Console | SGB Enhanced | GB Compatible ...)]`. Measured tag census: region and language groups dominate (GBA: Japan 983, USA 915, Europe 858, `En,Fr,De,Es,..` 556, Virtual Console 189, Rev 1 139). GBC adds `GB Compatible` (818) and `SGB Enhanced` (361). NES adds `Pirate` (2,166) and `Demo N` [4].
- 🟢 Articles are moved to the end of the title part, before the subtitle: "Legend of Zelda, The - A Link to the Past (USA)", "Monde Perdu, Le - Jurassic Park (France)", "Linea, La (World)", "Maus, Die (Europe)", "Aventures de Buzz l'Eclair, Les (France)", "Geheimnis der Happy Hippo-Insel, Das (Germany)". Counts of ", The" titles: GBA 118, GBC 91, GB 111, NES 506; ", A"/", An" 2 to 18 per system [4].
- 🟢 The dats contain no `[...]` tags and no non-ASCII names (grep found none). So `[b]`, `[!]`, `[a1]`, `[T+Eng]` and non-ASCII names come from user files (GoodTools sets, translations, romhacks), not from No-Intro [4].
- 🟢 379 of the dats' names contain two or more " - " separators ("Adventures of Jimmy Neutron Boy Genius, The - Attack of the Twonkies"), so a naive "cut at the last ' - '" would break names. Keeping subtitles is right [4].
- 🟡 Scene-release numeric prefixes ("0001 - Game (U).gba", "1234 - ...") are not part of No-Intro or GoodTools; they exist only in some release sets. No authoritative spec found; the rule below is a conservative heuristic [no source].
- 🔴 ES-DE, Pegasus and TWiLight Menu++ cleaning rules: the ES-DE user guide excerpt I could read says only that it matches media to ROM file names; I could not read the rest, and found no source for Pegasus or TWiLight [5]. Treat their behaviour as unknown. RetroArch is the only one I verified in code.

**Findings: CRC name database (Q2)**
- 🟢 libretro-database layout: `dat/` (libretro-maintained), `metadat/` (third-party dats incl. No-Intro), `rdb/` (compiled RetroArch databases). CRC is the primary key for cartridge systems; entries also carry MD5 and SHA-1 [6].
- 🟢 The four files are at `https://raw.githubusercontent.com/libretro/libretro-database/master/metadat/no-intro/<System name>.dat` (spaces as `%20`): "Nintendo - Game Boy Advance", "Nintendo - Game Boy Color", "Nintendo - Game Boy", "Nintendo - Nintendo Entertainment System". Format is clrmamepro text: `game ( name "..." region "..." serial "..." rom ( name ".." size N crc XXXXXXXX md5 .. sha1 .. serial ".." ) )` [4].
- 🟢 Sizes (measured):

  | System | Games (= unique CRCs) | dat bytes | name bytes (UTF-8, no NUL) | raw table bytes (4 B CRC + 4 B offset + names + NULs) |
  |---|---|---|---|---|
  | GBA | 3,503 | 963,559 | 149,035 | about 180,600 |
  | GBC | 2,433 | 625,655 | 110,281 | about 132,200 |
  | GB | 2,115 | 494,585 | 75,680 | about 94,700 |
  | NES | 13,286 | 3,104,048 | 453,487 | about 573,100 |
  | Total | 21,337 | 5.2 MB | 788,483 | about 980,500 |

  Average name is 34 to 45 bytes. A sorted-CRC array plus a string pool compresses further if needed (shared region suffixes), but about 1 MB raw already fits the PSRAM page-0 reserve (5 to 8 MB) and an SD file [4].
- 🟢 CRCs are unique within each dat (entries equal unique CRCs in all four), so a per-system sorted table needs no collision handling beyond the system key. Cross-system collisions were not checked and are unlikely to matter if keyed by console [4].
- 🟢 NES dat entries include the 16-byte iNES header in the size (16,400 = 16 + 16,384) and the CRC covers the whole file, so the ROM's whole-file CRC32 matches. A few unheadered variants (`.unh`, size 16,384) are listed as separate entries. Headerless dumps made by other tools will not match [4].
- 🟢 GBA internal game code: the dat carries a `serial` field equal to the 4-character game code (header 0xAC): 3,124 of 3,503 GBA games (89%) have one. GB has 6 serial lines in 2,115 games, GBC 124 in 2,433, NES 4. So the game code is a GBA-only alternative key [4]. (The 0xAC offset is from the GBA header spec, 🟡, not re-verified here.)
- 🟡 A game code does not tell revisions or romhacks apart: a hack of a game shares its base's code, so a game-code match gives a base-game name or picture to a hack. That fits the decision that custom art by file name wins over matched art [no single source; inference].
- 🔴 No-Intro / DAT-o-MATIC terms of use: the about page returned only a stub; a search found no official terms page, only secondary notes that DAT-o-MATIC shows CAPTCHAs and bans frequent automated requests [7]. Do not scrape DAT-o-MATIC. Using the libretro mirror avoids that, but whether No-Intro's data may be bundled is unconfirmed.
- 🟢 Licence of libretro-database: GitHub reports CC BY-SA 4.0 for the repository (`LICENSE`) [8]. The README states no licence and says many sources are imported from upstream groups (No-Intro, Redump, TOSEC, GameTDB) [6]. So the repo licence may not cover upstream content.
- 🔴 OpenVGDB and GameTDB: OpenVGDB was not fetched (no source read). GameTDB's pages returned 403 to my fetch; a search snippet says its database can be downloaded by anyone [9] but I found no terms text, and GameTDB's covers are for GameCube, Wii, Wii U, DS and 3DS, with no sign of GBA covers (🟡; rom-properties takes GBA images from another host) [9]. Neither is needed if libretro's dats are used.

**Findings: box art (Q3)**
- 🟢 URL scheme (HTTP 200, measured): `https://thumbnails.libretro.com/<System name, URL-encoded>/Named_Boxarts/<name>.png`, for example `.../Nintendo%20-%20Game%20Boy%20Advance/Named_Boxarts/Advance%20Wars%20(USA).png` (430,864 bytes, PNG). The directory also serves an Apache index listing; each system has `Named_Boxarts`, `Named_Snaps`, `Named_Titles`, and `Named_Logos` for some [3][10].
- 🟢 The same files exist as GitHub repos `libretro-thumbnails/<System with _ for spaces and hyphen spaces>` (for example `Nintendo_-_Game_Boy_Advance`, path `Named_Boxarts/<name>.png` on raw.githubusercontent.com returned HTTP 200). The server syncs about every two days [10].
- 🟢 Naming: file name is the No-Intro game name; the characters `&*/:`<>?\|"` are each replaced by `_` (GitHub README adds the double quote; the docs page lists the first nine). Example: `Q*bert's Qubes (USA)` becomes `Q_bert's Qubes (USA).png` [3][10].
- 🟢 Coverage of the dat's game names by an exact `Named_Boxarts` file (measured): GBA 3,056 of 3,503 (87%); GBC 1,380 of 2,433 (57%); GB 1,459 of 2,115 (69%); NES 4,588 of 13,286 (35%; protos, pirates, demos and Japan-only releases are missing). The GBA folder holds 5,959 files, more than the dat has games, so it also has non-dat names; NES has 13,418 [4][10].
- 🟡 Images are not normalised (sizes vary; the sample was 430 KB). The user's pipeline (crop to 80x80 and convert on the PC) still has to handle it. I did not sample size or aspect distribution.
- 🟢 Terms: no licence is stated for the libretro-thumbnails repos or the docs. The README says "The game art itself and promotional art originates from the work of each respective game's developers and publishers", with sources such as MobyGames and Fandom, and volunteer contributions [10]. So the art is third-party copyright with no grant: do not commit or bundle it, fetch on the user's machine for personal use, as the decisions file already says.
- 🟡 TWiLight Menu++ uses GameTDB DS covers: PNG files named by game ID (`ASME.png`) or by file name (`SM64DS.nds.png`), in `sd:/_nds/TWiLightMenu/boxart`, recommended 128x115, max 208x143, plus a TWiLightBoxart tool that fills the card [11]. This is DS-only; it gives no route to GBA art.
- 🔴 ScreenScraper: its API page lists daily request limits by donation tier (50,000 per day at $1 or $2 per month; 100,000 at $5 and $10) and mentions developer accounts, rate limits, commercial use, media use and credits, but the text of those terms was not on the page [12]. It needs an account; unlike the libretro server, it is not anonymous. Not recommended for the first version.

**Conflicts**
- ⚖️ Which characters are replaced in thumbnail names: [3] lists `&*/:`<>?\|` (nine); [10] also lists `"`. The No-Intro names contain no `"` in these dats, so it does not matter for these four systems. Leaning on [10]'s ten for safety.

**Open questions**
- ❔ Exact No-Intro terms for redistributing derived name tables (a licence or permission would settle bundling). Safest: the tool builds the table on the user's machine and nothing is committed.
- ❔ OpenVGDB (names and covers) and GameTDB terms: not read.
- ❔ ES-DE, Pegasus and TWiLight Menu++ own cleaning rules: not verified.
- ❔ Image-size distribution of Named_Boxarts per system, to plan cropping.
- ❔ How many user romhacks and translations fall outside the dat (they never match by CRC).

---

**Sources**
1. [libretro/RetroArch `menu/menu_displaylist.c`](https://raw.githubusercontent.com/libretro/RetroArch/master/menu/menu_displaylist.c) (LABEL_DISPLAY_MODE_REMOVE_PARENTHESES[_AND_BRACKETS], lines 3822 to 3829) · read 2026-10-20 · primary
2. [libretro-common `playlists/label_sanitization.c`](https://raw.githubusercontent.com/libretro/RetroArch/master/libretro-common/playlists/label_sanitization.c) · read 2026-10-20 · primary
3. [Libretro docs: ROMs, playlists, thumbnails](https://docs.libretro.com/guides/roms-playlists-thumbnails/) · read 2026-10-20 · primary
4. libretro-database `metadat/no-intro/*.dat` for the four systems, downloaded and analysed locally (dat version 2026.10.07) · 2026-10-20 · primary
5. [ES-DE USERGUIDE.md](https://gitlab.com/es-de/emulationstation-de/-/raw/master/USERGUIDE.md) (first 100k characters only) · 2026-10-20 · primary
6. [libretro-database README](https://raw.githubusercontent.com/libretro/libretro-database/master/README.md) · 2026-10-20 · primary
7. Web search on DAT-o-MATIC terms (datoso docs, Vikidia) · 2026-10-20 · secondary
8. [GitHub API: libretro-database licence](https://api.github.com/repos/libretro/libretro-database/license) (CC-BY-SA-4.0) · 2026-10-20 · primary
9. [GameTDB FAQ](https://gametdb.com/Main/FAQ) via search snippet; rom-properties NETWORK.md · 2026-10-20 · secondary
10. [libretro-thumbnails README](https://github.com/libretro-thumbnails/libretro-thumbnails), live `thumbnails.libretro.com` listings and HTTP checks · 2026-10-20 · primary
11. [TWiLight Menu++ wiki, how to get box art](https://git.randommeaninglesscharacters.com/rvtr/wiki/src/branch/main/pages/_en-US/twilightmenu/how-to-get-box-art.md) via search snippet · 2026-10-20 · secondary
12. [ScreenScraper API page](https://api.screenscraper.fr/) · 2026-10-20 · primary

*standard · 2 rounds · 12 sources read · done directly (no sub-researchers)*

---

## Recommendations for B4 and the v0.4.1 tickets

**B4: cleaning rules, in this order** (hardware-free, on UTF-8 bytes; every step is on ASCII punctuation so multi-byte characters pass through untouched)
1. Take the file's base name and drop the extension (the last `.` followed by 1 to 4 alphanumerics; leave dot-only or leading-dot names as they are).
2. Replace `_` with a space only if the name contains no spaces (some sets use underscores as spaces). Otherwise keep `_`.
3. Remove every `(...)` and `[...]` group (RetroArch algorithm: skip from an opener to its closer, not nested). Two refinements: if a group is unclosed, keep the text; track nesting depth so `(a (b))` and `[x (y)]` are fully removed.
4. Remove a leading scene or index prefix: `^\d{1,5}\s*-\s*` only if something remains after it and the name has another letter. (Heuristic, 🟡; applies to "0001 - Game", not "007 - NightFire", which is a real title: so require 4 or more digits, or use the rule only when the digits are at least 4 long.) Recommend: 4 to 5 digits only.
5. Collapse runs of whitespace to one space and trim both ends.
6. Move a trailing article from the title part to the front: split at the first ` - ` (the title part ends there; also stop at the end of the string). If the title part ends in `, The`, `, A`, `, An`, `, Le`, `, La`, `, Les`, `, L'`, `, Der`, `, Die`, `, Das`, `, El`, `, Il`, `, Los`, `, Las`, `, Lo`, `, Gli`, `, Een`, `, Het`, `, De`, `, Das`, move it to the front: "Legend of Zelda, The - A Link to the Past" becomes "The Legend of Zelda - A Link to the Past". For `, L'` join without a space ("L'Eclair"). Do it before step 3 or after it, not both; best after step 3 so a ", The" before a tag still works. Matching is case-sensitive on the article, so "Monde, de" stays.
7. Trim stray separators left by removal: trailing ` -`, leading `- `, doubled ` - `. Do not rewrite inner ` - ` into other punctuation: 379 of the dats' names have two or more.
8. If the result is empty (the whole name was tags or digits), fall back to the base name from step 1 unchanged.
9. Dedupe (two games cleaning to the same name keep their first differing tag): sort the colliding files by their original base name, find the first `(...)` or `[...]` group that differs among them, and append it in parentheses, e.g. "Pokemon Emerald (USA)" vs "Pokemon Emerald (Europe)". If the first group does not separate them (identical groups differ later), append the first differing group by position. If nothing differs (identical base names are impossible within a folder, but two folders can repeat), append the folder name. A game that never collides gets no tag. Name uniqueness is per console, not across the library.

Keep a short, tunable table of articles and a table of "tags worth keeping on collision" so tweaks are cheap. Test table should contain these real names: "Advance Wars (USA)", "Legend of Zelda, The - A Link to the Past (USA)", "Monde Perdu, Le - Jurassic Park (France) (En) (SGB Enhanced)", "Aventures de Buzz l'Eclair, Les (France)", "Pokemon - Emerald Version (USA, Europe)", "Mario & Luigi - Superstar Saga (USA, Australia)", "0001 - Game (U).gba", "Game (U) [!].gba", "Game [T+Eng_v1.0].gba", "Game (Rev 1) [b1]", "Pokémon Émeraude (France).gba", "日本語 (Japan).gba", "(Unlicensed).gba" (empty after cleaning), "Dr. Mario (World).nes" (dots are not extensions).

**v0.4.1: CRC database**
- Source: libretro-database `metadat/no-intro/<System>.dat` for the four systems (CC BY-SA 4.0 repo; the mirror avoids DAT-o-MATIC's CAPTCHAs). A `just names-update` (or a step of `just art-scrape`) downloads them on the user's machine with a polite single fetch each, caches them in a git-ignored folder, and builds one binary file for the card, `/torii/names.db`. Nothing is bundled in the repo or the release.
- Format: header with the dat version, then per system a sorted array of `(crc32, name offset)` and a string pool; for GBA also a sorted `(game code, name offset)` array from the `serial` field. About 1 MB for the four systems (GBA about 180 KB, NES about 573 KB); store full No-Intro names and run the B4 cleaner at display time so the rules stay editable. Looked up by binary search straight from PSRAM (1 MB fits the 5 to 8 MB reserve) or from the card.
- Key: whole-file CRC32 (NES: with the iNES header, like the dat). GBA game code is the fallback when the CRC misses, accepting that a hack shows its base game's name (custom names and custom art take priority anyway). GB and GBC have no usable code; a CRC miss falls through to the cleaned file name.
- Keep the first line of `names.db` the dat version so the report can say "database from 2026.10.07".

**v0.4.1: box art**
- Fetch source: `https://thumbnails.libretro.com/<System>/Named_Boxarts/<name>.png` (URL-encode the path), per game, on the user's machine, never committed. No-auth, plain HTTPS GET, polite: one request at a time, a small delay, cache downloads in a git-ignored folder, skip files already there, and retry only on 5xx.
- Match chain for each library game: custom art by file name (`/torii/art/custom/<file name>`) → CRC → `names.db` No-Intro name → replace `&*/:`<>?\|"` with `_` → try `<name>.png` → if missing, try the RetroArch fallback of the name cut at the first `(` (`Name.png`, which often exists in other regions) → GBA only: game code → dat name → the same steps → placeholder. Report the hit rate per console at the end. Expect about 87% GBA, 69% GB, 57% GBC and 35% NES of dat games to have art, less for hacks and unlisted dumps.
- Convert on the PC (crop to 80x80, quantise to the engine's tile format); store converted art on the card by CRC; never commit images.
- ScreenScraper: skip for v0.4.1 (account needed, rate limits, terms unread). GameTDB and TWiLight Menu++'s box art are DS-oriented and do not cover GBA, so they add nothing here.

**Licence notes**
- RetroArch's `label_sanitization.c` is MIT-style: the algorithm can be reimplemented freely (re-implementing is safest for an Apache-2.0 repo, not copy-pasting).
- libretro-database is CC BY-SA 4.0 by its repo licence; if the tool only downloads and converts at the user's request and the resulting `names.db` stays on the user's card (not in the repo or releases), no share-alike obligation attaches to Torii's code. If you ever ship `names.db`, credit No-Intro and libretro and check No-Intro's terms first (unconfirmed).
- Box art has no licence grant: it belongs to publishers and contributors. Fetch locally, never publish, and say so in the tool's help text.
