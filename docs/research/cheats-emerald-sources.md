# Pokémon Emerald: every source, grouped by function

The second survey for the golden Emerald set (CRC32 `1F1C08FB`). `cheats-emerald.md` covered GameHacking.org and libretro; this adds the web, and groups all of it by what the cheats do. Every code was decoded with Torii's own decoder (`dist/survey/load_web.py`, results in `dist/survey/emerald_all.json`), so the addresses below are what Torii would run, not what the names claim.

## Sources

| Source | Cheats | Decode OK | New programs | Worth |
| --- | --- | --- | --- | --- |
| GameHacking.org | 170 | 162 | (the base) | The core: most codes, named, mostly right |
| libretro (Code Breaker) | 74 | 64 | 3 | Copies GameHacking; some garbled |
| retrododo.com | 61 (+ value tables) | 45 | 44 | ROM-patch gems: shiny, gender, walk through walls, catch trainers' Pokémon |
| smartprix.com | 61 | 43 | 42 | 42 warps, always on (the L line lost), incl. dungeons |
| touchtapplay.com | 6 | 0 | 0 | Nothing usable |
| AnyGBA (`github.com/hoanglt1223/anygba`, `cheats/BPEE.toml`) | 29 | not decoded | 29 | **Best quality**: built from the pokeemerald decomp, pointer- and key-aware, checked on this ROM per its author; own syntax, needs translating |
| Kodewerx thread (AR) | 23 | 14 | 14 | Warps to Sky Pillar, Terra/Marine Cave, Southern Island; legendary re-battles |
| pkmn.net | 15 | 7 | 2 | Copies GameHacking |
| German mGBA file (`aaron4567891-prog/mGBA-Android`) | 29 | ~20 | ~20 | **Other release** (Smaragd): addresses shifted, don't use |
| Other GitHub lists (PKHEX-, gba-online, gba-pro-studio, cheat-companion, pdEMU, dingbat, GBAEmu, elite-gba) | ~70 | ~25 | ~12 | Copied from cheat sites, often AI-assembled; some Ruby/Sapphire addresses |

| pokemoncoders.com (read in the browser) | 46 + value tables | 34 | 24 | The biggest site; ROM-patch pickers for nature, wild species and level |
| neoseeker (AR, CB, GS pages) | 15 + value tables | 9 | 7 | Same families; adds a type modifier, "hold R: foes' HP 0" |
| supercheats (34 user submissions) | 7 kept | 2 | 2 | Mostly dumps of the same codes without names |
| GameFAQs, Labmaster's "Emerald Codes FAQ" (2005) | 6 templates | 1 | 1 | The origin of the AR families everyone copies (species, level, gender, nature, shiny, warps, TM/HM moves) |
| GameFAQs, two "Game Shark Codes" FAQs | skipped | | | Ruby/Sapphire codes filed under Emerald |
| cheatcc | 1 | 1 | 1 | Copies pokemoncoders |

pokemoncoders, supercheats, gamefaqs, neoseeker and cheatcc blocked plain fetches (HTTP 403) and were read in the in-app browser instead. PokéCommunity and Reddit weren't tried: search found no Emerald code threads. Value tables (386 species, 354 moves, 175 warps, 100 levels, 25 natures, ~300 type pairs) were kept as one template each: the picker values can be generated with `tools/cheat_encode.py` once the template is decoded.

mGBA's forum warns that SuperCheats and Neoseeker codes are "often bunk", and its users report the classic wild-species modifiers don't work on Emerald, GameHacking's included.

## What the web adds

GameHacking already has most kinds. The web's additions worth testing, by source:

- **ROM patches from the decomp (AnyGBA):** perfect IVs for new Pokémon, max EVs from the next battle, max friendship, EXP multiplier (×N), infinite HP and PP on your side, every ball catches, wild species and wild level of your choice, encounter rate (none to every step), all shiny, field moves without badges. These patch the game's code, not RAM, so they survive Emerald moving its save blocks.
- **Pointer-and-key save writes (AnyGBA):** money, coins, item/ball/berry/vitamin of your choice ×99 in the bag or PC, all badges (flag bits), complete Pokédex, fast egg hatching (Day Care steps). Each writes through `03005D8C`/`03005D90` and XORs with the key at SaveBlock2+AC: the same idea as our standalone draft, without zeroing the key, so nothing else gets scrambled.
- **retrododo ROM patches:** Shiny Encounters (`08067C7C`, `08067C86`), next wild Pokémon male/female (`0806AF78`), Walk Through Walls (`08092C06`), Catch Other Trainer's Pokémon (`08067BE4`, held on no keys), 100% catch rate (`0802D62E`, from a GitHub list).
- **More warps:** dungeons and landmarks (Meteor Falls, Granite Cave, Victory Road, Sky Pillar top, Cave of Origin, Seafloor Cavern, Terra and Marine Caves, Artisan Cave, Hall of Fame, ...).
- **Legendary re-battles (Kodewerx):** clear the flag of Groudon/Kyogre, Mew/Latios/Latias, Lugia/Ho-Oh, Deoxys, Rayquaza.
- **ROM-patch pickers (pokemoncoders, neoseeker, Labmaster):** these change one halfword of the game's code per choice, so each family is one template and a value per choice, a natural fit for Torii's grouped variants:
  - nature of new Pokémon (`08067EAE`, 25 values);
  - wild species, the mGBA version (`080B5010`/`080B5012`, 386 values);
  - wild level (`080B4CEA`, 100 values);
  - a species' types (`083203EE`, Bulbasaur's ~300 pairs; other species would need their offsets).
- **More ROM patches (pokemoncoders):** 100% catch rate (`080565EE`, `08056610`), Master Ball free at the Mart (`080DFF60`), caught Pokémon have Pokérus (`0806B0F4`).
- **RAM pickers that need a CodeBreaker hook:** wild species, level and IVs written at `03007CF6`–`03007CF9`, behind CodeBreaker's "master code" `1006AF88`, which runs them inside the game's encounter routine. Torii runs cheats from its own hook, so whether they land in time is to test.
- **Move modifier (Labmaster):** `0203CED6` (written at its mirror), the move a TM teaches; 354 values.
- **Others:** Battle Kyogre (R+A+Select) and Groudon (R+B+Select) scripts, Never Lose HP (`020240AC`), sell at max price (`03005E18`), Mart prices (`020051B0`), all base decorations.

## Groups

"Kind" says how the code works: **ROM** a patch at launch, **save** a fixed-address write into the save blocks (needs the anti-DMA patch, `cheats-emerald.md`), **ptr** a write through the save-block pointers, **RAM** a plain write elsewhere, **script** a script hook on a key. "★" marks the strongest candidate per row.

### 1. Money, coins, points

| Cheat | Variants (source) | Kind | Note |
| --- | --- | --- | --- |
| Max money | ★ AnyGBA (ptr + key); our draft (save + key zeroed); GameHacking "Infinite Money" (save) | ptr | Web copies at `020257BC` are Ruby/Sapphire's |
| Max coins | ★ AnyGBA (ptr + key, SaveBlock1+494); GameHacking "Max Coins" (`02002DEC`) | ptr | GameHacking's is the wrong address |
| Battle Points | GameHacking "Infinite BP", German "9999 GP" (`0202590C`), retrododo (script) | save | |
| Shop prices | GameHacking: Items Cost $1, Free MOD Shop, Maximum Sale Price | RAM | pokemoncoders: Mart price `020051B0` ($1 or $0), sell at max `03005E18` |

### 2. Items

| Cheat | Variants (source) | Kind | Note |
| --- | --- | --- | --- |
| Any item ×99 in the bag / PC | ★ AnyGBA (a picker per pocket: items, balls, berries, vitamins) | ptr | Fits Torii's grouped variants: one row, one pick |
| Rare Candy / Master Ball ×99 | ★ AnyGBA; web "in PC" (`02025EE0`, save, 1 in 32 without anti-DMA); "at the Mart" (`02005274`, RAM) | ptr | The Mart slot code is doubtful on Emerald (to test) |
| All Poké Balls / TMs & HMs | ★ our draft; GameHacking "Have All ..."; pkmn.net TM variant (count 1, not 9) | save | |
| All berries | ★ AnyGBA (×99 each); GameHacking pocket rewrites (2 parts) | ptr | |
| Event tickets (Mystic, Aurora, Old Sea Map) | ★ our draft (own PC slots); GameHacking (one shared slot) | save | German ones need its "Aktivierungscode" |
| Pokéblocks, Soot Sack | Our draft; GameHacking | save | |

### 3. Pokémon

| Cheat | Variants (source) | Kind | Note |
| --- | --- | --- | --- |
| Shiny | ★ AnyGBA (every new Pokémon); retrododo "Shiny Encounters" (wild); Kodewerx (wild, hold L+R); smartprix (`0806AFA6`) | ROM | |
| Perfect IVs / max EVs / max friendship | AnyGBA only | ROM | CB `03007CF9` for wild IVs (pokemoncoders) |
| Nature | ★ pokemoncoders/Labmaster (ROM `08067EAE`, 25 values); GameFAQs R/S FAQ (other game) | ROM | New Pokémon only |
| Types | neoseeker (ROM type table) | ROM | A fun extra; per species |
| Pokérus | pokemoncoders (ROM, on catch) | ROM | |
| Gender of the next wild | retrododo (`0806AF78`); German (`0806AF7C`, other release) | ROM | |
| Max stats | GameHacking (6 slots); retrododo (first slot, per stat) | RAM | Battle stats only, recomputed on level-up |
| Fast egg hatching | ★ AnyGBA (Day Care steps); GitHub (`03004380`); German ROM patch (other release) | ptr | |
| Starters / gifts | GameHacking (Down+R+L, clash); retrododo (`03007CF6`, suspect) | script | |
| Complete Pokédex, National Dex | ★ AnyGBA, our draft, GameHacking | ptr / save | |

### 4. Wild encounters

| Cheat | Variants (source) | Kind | Note |
| --- | --- | --- | --- |
| Species of your choice | ★ AnyGBA (ROM `080B4E6C`); pokemoncoders mGBA (ROM `080B5010`); GameHacking "Wild Pokemon Modifier"; CB `03007CF6` (pokemoncoders, retrododo, neoseeker, cheatcc) | ROM | mGBA users: RAM modifiers don't work on Emerald, which fits the CB hook reading |
| Level of your choice | AnyGBA (ROM `080B4C74`); Labmaster (ROM `080B4CEA`); CB `03007CF8` | ROM | |
| Perfect IVs | AnyGBA (ROM, all new Pokémon); CB `03007CF9` (wild) | ROM | |
| Encounter rate | ★ AnyGBA (none to every step); GameHacking No Random Battles (3 copies), Max Repel Steps | ROM | |
| Catch rate | ★ AnyGBA "every ball catches" (`08056566`); pokemoncoders (`080565EE`); GitHub `0802D62E`; GameHacking (3 on one address) | ROM | |
| Catch trainers' Pokémon | retrododo (ROM + RAM) | ROM | |
| Safari Zone | AnyGBA (`0203A04C`); GameHacking; GitHub (`0203855C`) | RAM | Three addresses: to test |

### 5. Battle

| Cheat | Variants (source) | Kind | Note |
| --- | --- | --- | --- |
| One-hit KO | GameHacking (5 overlapping); AnyGBA/GitHub (both foes, doubles) | RAM | |
| Infinite HP / PP | ★ AnyGBA (ROM, your side); GameHacking Infinite PP; GitHub `02023C08` | ROM | |
| EXP | ★ AnyGBA multiplier (×N); GameHacking 7 overlapping; web "9999 EXP" | ROM | |
| Always your turn, foe can't attack, skip trainers, never miss | GameHacking | ROM / RAM | |

### 6. Movement and field

| Cheat | Variants (source) | Kind | Note |
| --- | --- | --- | --- |
| Walk through walls | retrododo/dingbat (`08092C06`); AnyGBA (`0808B0C0`) | ROM | Two addresses: test both |
| Field moves without badges / HMs | ★ AnyGBA (ROM); GameHacking Surf/Dive/Flash/Cut... (scripts) | ROM | |
| Badges | ★ AnyGBA (flag bits, all 8); GameHacking (8 scripts on Select combos) | ptr | |
| Fly anywhere, Fly map, Human Bike, walk in menus, always run | GameHacking; German "Immer Rennen" (other release) | RAM | |
| See in the dark | retrododo (unreadable) | | |

### 7. Warps

| Set | Source | Note |
| --- | --- | --- |
| 22 towns and islands, on L | ★ GameHacking | |
| 42 places incl. dungeons, always on | smartprix (mirror `020722E4`) | Lost their L line: rebuild on L |
| Sky Pillar top, Terra/Marine Cave, Southern Island (Lati@s) | Kodewerx | Some on L/R |
| 175 locations | retrododo (value table) | Placeholder codes: to read |

### 8. Legendaries and events

| Cheat | Variants (source) | Note |
| --- | --- | --- |
| Re-battle legendaries | ★ Kodewerx (flags per legendary); GameHacking "Re-Battle Activator" | Save flags: need anti-DMA |
| Battle Mew/Deoxys/Lugia/Ho-Oh/Lati@s | GameHacking (scripts, clashing keys) | |
| Event islands | Tickets (group 2) or warps (group 7) | |

### 9. Trainer rematches, menus, Frontier, minigames

GameHacking only, as in `cheats-emerald.md`: 13 rematches on Select, PC/Bag/shops/PokéNav on key combinations, Battle Frontier (Pyramid, symbols, species restriction), Berry Blender and contests. Kodewerx adds raw trainer name, ID and Secret ID editors (unreadable as published).

### 10. Not usable

- Every master code (the anti-DMA patch is built into each cheat that needs it).
- The German file (another release), Ruby/Sapphire addresses in GitHub lists, the libretro garbled lines, GameHacking's Level 25–100, touchtapplay's.

## Next

1. **Translate AnyGBA's 29** into AR v3 (pointer writes, ROM patches, key XOR) and decode them: they are the best base for money, items, Pokémon and wild encounters, and their pickers map onto Torii's grouped variants.
2. **Decide per row** which candidate goes in (the ★ is a proposal).
3. **Test** the doubtful ones on the emulated cart: the two walls addresses, Mart slot, Safari addresses, `03007CF6`, Max Coins.
4. **Pickers:** build each family (nature, species, level, move, item) as a run of variants from its template, values generated rather than copied.

## AnyGBA's format

AnyGBA is a GBA static recompiler (ROM to native PC program, from 2026-10), not a cheat device. Its cheat engine reads the usual formats, plus a raw syntax of its own for its curated Emerald list. Everything in that syntax maps onto Action Replay v3, which Torii runs, except one thing:

| AnyGBA | Meaning | In AR v3 |
| --- | --- | --- |
| `ADDR:VALUE` | Write RAM | Write |
| `0808B0C0:46C02000` | Patch ROM (16 or 32 bits) | ROM patches, one per halfword (Torii takes 256 a cheat) |
| `[03005D8C]+0490:VALUE` | Write through a pointer | Pointer write |
| `[ptr]+OFF\|80` | Set bits | OR |
| `VALUE^[03005D90]+00AC` | XOR the value with the save's key before writing | **None**: no format reads one address to change another's value |

The XOR is what makes AnyGBA's money, coins and item counts clean: they write the right encrypted value under the save's own key. Without it, a cheat has to zero the key (our draft's way), which leaves every other encrypted value (money, coins, bag counts, game stats) reading as garbage until something rewrites it.

**Recommendation:** translate AnyGBA's codes into AR v3, and don't add its syntax as a format. For the XOR, the options are:

- keep zeroing the key (works today, scrambles other values once);
- or add one op to Torii's interpreter, "write VALUE XOR [address]", reachable from a Torii-only code line. The same key scheme covers FireRed/LeafGreen's money and items, so it would serve those golden sets too. It's a kernel change (decoder, interpreter, cost model, tests), so it's a decision of its own.
