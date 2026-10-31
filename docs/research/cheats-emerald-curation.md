# Pokémon Emerald: golden cheat curation

Every cheat we have (GameHacking, plus libretro's three of its own), grouped. "Proposal" was the first pass; "Decision" is the owner's, settled in an interview on 2026-10-31 (D1-D50 below), and what replaced each cheat in the golden set. Buttons are decoded from the codes: "(any)" means the code fires whenever that button is down, in any combination.

## 1 Base (replaced)

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 1 | (M) |  | drop | replaced: each save-data cheat carries its own patch | drop |
| 2 | (M) |  | drop | replaced: each save-data cheat carries its own patch | drop |
| 3 | Notice |  | drop | device setup only, or replaced | drop |
| 4 | [M] Must Be On |  | drop | device setup only, or replaced | drop |
| 5 | [M] Must Be On |  | drop | device setup only, or replaced | drop |
| 6 | Anti DMA (libretro) |  | drop | device setup only, or replaced | drop |

## 2 Broken

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 7 | Level 100 |  | drop | can't be read | drop |
| 8 | Level 25 |  | drop | can't be read | drop |
| 9 | Level 50 |  | drop | can't be read | drop |
| 10 | Level 75 |  | drop | can't be read | drop |

## 3 Money & shop

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 11 | Free MOD Shop Items |  | keep |  | drop: writes the heap outside a shop |
| 12 | Infinite Money |  | keep (golden) | standalone | replaced: Money: Max money (AnyGBA, keyed) |
| 13 | Items Cost $1 |  | keep |  | keep: Money: Everything costs 1 |
| 14 | Max Coins |  | test | writes 02002DEC, not the coins (SaveBlock1 +494): likely wrong; a correct one can be made | replaced: Money: Max coins (keyed) |
| 15 | Max Coins From Pokies |  | test | writes 02002DEC, not the coins (SaveBlock1 +494): likely wrong; a correct one can be made | replaced: Money: Max coins (keyed) |
| 16 | Max Money |  | test | writes 02000568, not the money: likely wrong | drop |
| 17 | Maximum Sale Price |  | keep |  | drop |

## 4 Items

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 18 | All Berries (Part 1) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | drop: the item shop (L+R+B) sells every item |
| 19 | All Berries (Part 2) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | drop: the item shop (L+R+B) sells every item |
| 20 | All Decorations (Part 1) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | drop: the item shop (L+R+B) sells every item |
| 21 | All Decorations (Part 2) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | drop: the item shop (L+R+B) sells every item |
| 22 | All Hold Items (Part 1) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | drop: the item shop (L+R+B) sells every item |
| 23 | All Hold Items (Part 2) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | drop: the item shop (L+R+B) sells every item |
| 24 | All Mail |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | drop: the item shop (L+R+B) sells every item |
| 25 | All Misc Items (Part 1) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | drop: the item shop (L+R+B) sells every item |
| 26 | All Misc Items (Part 2) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | drop: the item shop (L+R+B) sells every item |
| 27 | All Pokeballs |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | drop: the item shop (L+R+B) sells every item |
| 28 | All TM's/HM's (Part 1) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | drop: the item shop (L+R+B) sells every item |
| 29 | All TM's/HM's (Part 2) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | drop: the item shop (L+R+B) sells every item |
| 30 | Aurora Ticket |  | keep (golden) | own PC slots | replaced: item shop TICKETS (ticket and ferry flag) |
| 31 | Have All Pokeballs |  | keep (golden) | standalone | drop: the item shop |
| 32 | Have All TMs/HMs |  | keep (golden) | standalone | drop: the item shop |
| 33 | Have Old Sea Map |  | keep (golden) | own PC slots | replaced: item shop TICKETS (ticket and ferry flag) |
| 34 | Mystic Ticket |  | keep (golden) | own PC slots | replaced: item shop TICKETS (ticket and ferry flag) |

## 5 Pokédex & Pokémon

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 35 | 1st Pokemon Max Stats |  | keep |  | drop |
| 36 | 2nd Pokemon Max Stats |  | keep |  | drop |
| 37 | 3rd Pokemon Max Stats |  | keep |  | drop |
| 38 | 4th Pokemon Max Stats |  | keep |  | drop |
| 39 | 5th Pokemon Max Stats |  | keep |  | drop |
| 40 | 6th Pokemon Max Stats |  | keep |  | drop |
| 41 | Chikorita | R+L+Down | decide | all four on Down+R+L: clash | replaced: field menu GIFTS |
| 42 | Cyndaquil | R+L+Down | decide | all four on Down+R+L: clash | replaced: field menu GIFTS |
| 43 | Enable National Dex [Press Select] | Select (any) | keep |  | replaced: field menu UNLOCK > POKéDEX |
| 44 | Infinite Wynaut Eggs |  | keep |  | replaced: field menu GIFTS (Wynaut egg) |
| 45 | Pokedex Complete |  | keep (golden) | standalone | replaced: field menu UNLOCK > POKéDEX |
| 46 | Receive Starter | R+L+Down | decide | all four on Down+R+L: clash | replaced: field menu GIFTS |
| 47 | Shiny Groudon |  | keep (golden) | standalone | drop |
| 48 | Totodile | R+L+Down | decide | all four on Down+R+L: clash | replaced: field menu GIFTS |
| 49 | Wild Pokemon Modifier |  | keep |  | replaced: Wild: Species (AnyGBA, 386 choices) |
| 50 | Complete Pokedex (libretro) |  | drop | broken copy (1 byte of 48) | drop |

## 6 Battle

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 51 | 1-Hit Double Battle Kill (Instant Win) | L | decide | overlap: pick one or two | replaced: Battle: One-hit KO (AnyGBA) |
| 52 | 1-Hit Kill | L | decide | overlap: pick one or two | replaced: Battle: One-hit KO (AnyGBA) |
| 53 | 1-Hit Win | R | decide | overlap: pick one or two | replaced: Battle: One-hit KO (AnyGBA) |
| 54 | Always Your Turn |  | keep |  | drop |
| 55 | Battle: 1 Hit Kills |  | decide | overlap: pick one or two | replaced: Battle: One-hit KO (AnyGBA) |
| 56 | Easy Win (Doubles) |  | keep |  | drop |
| 57 | Easy Win (Trainer) |  | keep |  | drop |
| 58 | Infinite PP |  | keep |  | replaced: Battle: Infinite PP (AnyGBA) |
| 59 | Opponent Can't Attack |  | keep |  | drop |
| 60 | Skip Trainer Battles |  | keep |  | keep: Battle: Skip trainer battles |
| 61 | 1-Hit Kill (libretro) |  | decide | overlap: pick one or two | replaced: Battle: One-hit KO (AnyGBA) |

## 7 EXP

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 62 | Battle: R = More EXP |  | decide | overlap: same addresses | replaced: Battle: EXP x2-x255 (AnyGBA) |
| 63 | Exp Share |  | decide | overlap: same addresses | keep: Battle: EXP for the whole party |
| 64 | Gain 5000 Exp | A+R | decide | overlap: same addresses | replaced: Battle: EXP x2-x255 (AnyGBA) |
| 65 | Max EXP Gain After Battle |  | decide | overlap: same addresses | replaced: Battle: EXP x2-x255 (AnyGBA) |
| 66 | Medium EXP Gain After Battle |  | decide | overlap: same addresses | replaced: Battle: EXP x2-x255 (AnyGBA) |
| 67 | Mini EXP Gain After Battle |  | decide | overlap: same addresses | replaced: Battle: EXP x2-x255 (AnyGBA) |
| 68 | Recieve More Exp |  | decide | overlap: same addresses | replaced: Battle: EXP x2-x255 (AnyGBA) |

## 8 Catching

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 69 | 100% catch Rate |  | decide | first three write the same address | replaced: Battle: Every ball catches (AnyGBA) |
| 70 | Safari PKMN Easily Caught |  | decide | first three write the same address | replaced: Battle: Every ball catches (AnyGBA) |
| 71 | Wild PKMN Easily Caught |  | decide | first three write the same address | replaced: Battle: Every ball catches (AnyGBA) |
| 72 | Wild Pokemon Catching Spree |  | decide | first three write the same address | replaced: Battle: Every ball catches (AnyGBA) |

## 9 Overworld

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 73 | Access Fly Map [Press Select] | Select+A | keep |  | drop |
| 74 | Change PLYR Name (View Trainer Card) |  | keep |  | drop |
| 75 | Early Access to Magma Hideout |  | keep |  | drop |
| 76 | Fly Anywhere On Map |  | keep |  | keep: Overworld: Fly to any town |
| 77 | Fly codes |  | keep |  | drop |
| 78 | Free Daycare |  | keep |  | keep: Overworld: Free Day Care |
| 79 | Low Game Time |  | keep |  | drop |
| 80 | Max Repel Steps |  | keep |  | drop |
| 81 | No Random Battles |  | decide | duplicates: an 8-bit and a 16-bit write | keep: Wild: No wild encounters |
| 82 | No Random Battles |  | decide | duplicates: an 8-bit and a 16-bit write | drop: duplicate |
| 83 | Skip Intro |  | keep |  | drop |
| 84 | The Human Bike | B+Up / B+Down | keep |  | drop |
| 85 | Walk in Menu/Conversation |  | keep |  | drop |

## 10 HMs & badges

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 86 | Access Secret Power |  | keep |  | replaced: Overworld: Field moves without badges (AnyGBA) and HMs in the shop |
| 87 | Balance Badge [Press Select+L+Up] | Select+L+Up | keep |  | replaced: field menu UNLOCK > ALL BADGES |
| 88 | Dynamo Badge [Press Select+Down] | Select+Down | keep |  | replaced: field menu UNLOCK > ALL BADGES |
| 89 | Feather Badge [Press Select+L+Rght] | Select+L+Right | keep |  | replaced: field menu UNLOCK > ALL BADGES |
| 90 | Have Cut Ability |  | keep |  | replaced: Overworld: Field moves without badges (AnyGBA) and HMs in the shop |
| 91 | Have Dive [Press R+Select] | Select+R | keep |  | replaced: Overworld: Field moves without badges (AnyGBA) and HMs in the shop |
| 92 | Have Flash [Press L+Select] | Select+Up | keep |  | replaced: Overworld: Field moves without badges (AnyGBA) and HMs in the shop |
| 93 | Have Rock Smash Ability |  | keep |  | replaced: Overworld: Field moves without badges (AnyGBA) and HMs in the shop |
| 94 | Have Surf Ability [Press Select] | Select (any) | keep |  | replaced: Overworld: Field moves without badges (AnyGBA) and HMs in the shop |
| 95 | Heat Badge [Press Select+Left] | Select+Left | keep |  | replaced: field menu UNLOCK > ALL BADGES |
| 96 | Knuckle Badge [Press Select+Rght] | Select+Right | keep |  | replaced: field menu UNLOCK > ALL BADGES |
| 97 | Mind Badge [Press Select+L+Down] | Select+L+Down | keep |  | replaced: field menu UNLOCK > ALL BADGES |
| 98 | Rain Badge [Press Select+L+Left] | Select+L+Left | keep |  | replaced: field menu UNLOCK > ALL BADGES |
| 99 | Stone Badge [Press Select+Up] | Select+Up | keep |  | replaced: field menu UNLOCK > ALL BADGES |
| 100 | Waterfall |  | keep |  | replaced: Overworld: Field moves without badges (AnyGBA) and HMs in the shop |

## 11 Menus & shops

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 101 | Access Birchs Bag [Press L+R+Up] | R+L+Up | keep |  | drop |
| 102 | Access Deco Shop [Press L+A+Select] | Select+A+L | keep |  | drop |
| 103 | Access MOD Shop [Press L+A+Select] | Select+A+L | keep |  | drop |
| 104 | Access PC [Press B+Select] | Select+B | keep |  | replaced: field menu PC (L+R+Down) |
| 105 | Install Match Call [Press Select+R+Up] | Select+R+Up | keep |  | drop |
| 106 | Install PokeNAV [Press Select+R+Down] | Select+R+Down | keep |  | drop |

## 12 Legendaries

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 107 | Battle Deoxys [Press R+Select] | Select+R | keep |  | replaced: field menu LEGENDS (respawn) and the species choice |
| 108 | Battle Ho-oh [Press R+A+Select] | Select+A+R | keep |  | replaced: field menu LEGENDS (respawn) and the species choice |
| 109 | Battle Latias/Latios [Press R+Select] | Select+R | keep |  | replaced: field menu LEGENDS (respawn) and the species choice |
| 110 | Battle Lugia [Press L+B+Select] | Select+B+L | keep |  | replaced: field menu LEGENDS (respawn) and the species choice |
| 111 | Battle Mew [Press L+Select] | Select+L | keep |  | replaced: field menu LEGENDS (respawn) and the species choice |
| 112 | Choose Latias/Latios [Press L+Select] | Select+L | keep |  | replaced: field menu LEGENDS (respawn) and the species choice |

## 13 Warps (hold L, take a door)

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 113 | Battle Frontier | L (any) | keep |  | replaced: field menu WARP |
| 114 | Birth Island | L (any) | keep |  | replaced: field menu WARP |
| 115 | Birth Island |  | drop | always-on copy: every door leads there | drop |
| 116 | Dewford Town | L (any) | keep |  | replaced: field menu WARP |
| 117 | Ever Grande City | L (any) | keep |  | replaced: field menu WARP |
| 118 | Fallarbor Town | L (any) | keep |  | replaced: field menu WARP |
| 119 | Faraway Island | L (any) | keep |  | replaced: field menu WARP |
| 120 | Faraway Island |  | drop | always-on copy: every door leads there | drop |
| 121 | Fortree City | L (any) | keep |  | replaced: field menu WARP |
| 122 | Lavaridge Town | L (any) | keep |  | replaced: field menu WARP |
| 123 | Lilycove City | L (any) | keep |  | replaced: field menu WARP |
| 124 | Littleroot Town | L (any) | keep |  | replaced: field menu WARP |
| 125 | Mauville City | L (any) | keep |  | replaced: field menu WARP |
| 126 | Mossdeep City | L (any) | keep |  | replaced: field menu WARP |
| 127 | Naval Rock |  | drop | always-on copy: every door leads there | drop |
| 128 | Navel Rock | L (any) | keep |  | replaced: field menu WARP |
| 129 | Oldale Town | L (any) | keep |  | replaced: field menu WARP |
| 130 | Pacifidlog Town | L (any) | keep |  | replaced: field menu WARP |
| 131 | Petalburg City | L (any) | keep |  | replaced: field menu WARP |
| 132 | Rustboro City | L (any) | keep |  | replaced: field menu WARP |
| 133 | Safari Zone | L (any) | keep |  | replaced: field menu WARP |
| 134 | Slateport City | L (any) | keep |  | replaced: field menu WARP |
| 135 | Sootopolis City | L (any) | keep |  | replaced: field menu WARP |
| 136 | Southern Island | L (any) | keep |  | replaced: field menu WARP |
| 137 | Southern Island |  | drop | always-on copy: every door leads there | drop |
| 138 | Verdanturf Town | L (any) | keep |  | replaced: field menu WARP |

## 14 Rematches (hold Select)

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 139 | Champion Wallace | Select (any) | keep |  | replaced: Overworld: Gym leaders ready for a rematch |
| 140 | Elite Four Drake | Select (any) | keep |  | replaced: Overworld: Gym leaders ready for a rematch |
| 141 | Elite Four Glacia | Select (any) | keep |  | replaced: Overworld: Gym leaders ready for a rematch |
| 142 | Elite Four Phoebe | Select (any) | keep |  | replaced: Overworld: Gym leaders ready for a rematch |
| 143 | Elite Four Sidney | Select (any) | keep |  | replaced: Overworld: Gym leaders ready for a rematch |
| 144 | Gym Leader Brawly | Select (any) | keep |  | replaced: Overworld: Gym leaders ready for a rematch |
| 145 | Gym Leader Flannery | Select (any) | keep |  | replaced: Overworld: Gym leaders ready for a rematch |
| 146 | Gym Leader Juan | Select (any) | keep |  | replaced: Overworld: Gym leaders ready for a rematch |
| 147 | Gym Leader Norman | Select (any) | keep |  | replaced: Overworld: Gym leaders ready for a rematch |
| 148 | Gym Leader Roxanne | Select (any) | keep |  | replaced: Overworld: Gym leaders ready for a rematch |
| 149 | Gym Leader Tate&Liza | Select (any) | keep |  | replaced: Overworld: Gym leaders ready for a rematch |
| 150 | Gym Leader Watson | Select (any) | keep |  | replaced: Overworld: Gym leaders ready for a rematch |
| 151 | Gym Leader Winona | Select (any) | keep |  | replaced: Overworld: Gym leaders ready for a rematch |
| 152 | Re-Battle Activator | Select (any) | keep |  | drop |

## 15 Battle Frontier

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 153 | Access Pyramid Chall [Press Select] | Select (any) | keep |  | drop |
| 154 | Infinite BP |  | keep |  | replaced: Money: Max Battle Points (pointer write) |
| 155 | No Species Restriction At Battle Tower |  | keep |  | keep: Overworld: No species limit at the Battle Tower |
| 156 | Receive BP [Hold Select] | Select (any) | keep |  | drop |
| 157 | Receive Symbol | Select (any) | keep |  | replaced: field menu UNLOCK > ALL SYMBOLS |

## 16 Minigames

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 158 | Infinite Safari Balls |  | keep |  | keep: Minigames: Infinite Safari Zone |
| 159 | Infinite Safari Time |  | keep |  | keep: Minigames: Infinite Safari Zone |
| 160 | Lvl 80 Pokeblock |  | keep |  | keep: Minigames: Level 80 Pokéblock (pointer, slot 40) |
| 161 | Max Direct Hits |  | keep |  | drop |
| 162 | Max Lvl Gold (Beauty) |  | keep |  | drop |
| 163 | Max Lvl Gold (Cool) |  | keep |  | drop |
| 164 | Max Lvl Gold (Cute) |  | keep |  | drop |
| 165 | Max Lvl Gold (Smart) |  | keep |  | drop |
| 166 | Max Lvl Gold (Tough) |  | keep |  | drop |
| 167 | Maximum Feel |  | keep |  | drop |
| 168 | Maximum Hearts |  | keep |  | drop |
| 169 | Maximum Score |  | keep |  | keep: Minigames: Max contest score |
| 170 | No Misses |  | keep |  | drop |
| 171 | Quick Blend |  | keep |  | drop |
| 172 | Quick Contest |  | keep |  | keep: Minigames: Quick contests |
| 173 | Soot Sack Full |  | keep |  | keep: Minigames: Full Soot Sack (pointer) |

## The golden set

`cheats/gba/Pokemon - Emerald Version (USA, Europe).cht`, built by `tools/golden/emerald.py` from pret's pokeemerald (its addresses and symbol file for this ROM; item and species tables in `tools/golden/emerald.json`). 469 cheats in 8 folders (`cheatN_folder`), each with a guide and a buttons line; the 386 species, 20 levels, 25 natures and 5 EXP rates are choice groups.

| Folder | Cheats |
| --- | --- |
| Money | Max money, Max coins (Torii's keyed lines), Max Battle Points, Everything costs 1 |
| Menus | Item shop (L+R+B): Marts of Medicine, Balls, Training, TMs & HMs, Held items, Berries, Valuables at game prices, and the four event tickets free with their ferry flags. Field menu (L+R+A): PC, Heal, Warp (16 towns, 17 dungeons, 5 islands), Legends (respawn 11), Gifts (Hoenn and Johto starters, Beldum, Castform, Lileep, Anorith, Wynaut egg), Moves (relearner), Unlock (badges, Frontier symbols, Pokédex); L+R+Up heals, +Down PC, +Left warp, +Right moves |
| Pokémon | All new Pokémon shiny, Perfect IVs, Max friendship, Max EVs, Nature (25), Gender (2), Pokérus when caught, Eggs hatch fast |
| Battle | One-hit KO, Infinite HP, Infinite PP, Skip trainer battles, EXP x2/x3/x5/x10/x255, EXP for the whole party, Every ball catches |
| Wild | Species (386), Level 5-100 (20), No wild encounters, Encounter every step |
| Overworld | Fly to any town, Free Day Care, Walk through walls (hold R), Field moves without badges, Gym leaders ready for a rematch, No species limit at the Battle Tower |
| Minigames | Infinite Safari Zone, Level 80 Pokéblock, Quick contests, Max contest score, Full Soot Sack |

How they're built (D1, D7): CodeBreaker for RAM writes, Action Replay v3 raw for ROM patches and save-block pointer writes, Torii's raw lines only for money and coins (XORed with the save's key). No master codes, nothing encrypted. Nothing writes an item slot: items come from the game's own Mart and "Obtained" scripts, so nothing in the bag or PC is overwritten (D9, D10). The menus are game scripts, strings and menu tables written into the ROM's free space (from `0x08F00000`) at launch, on Emerald's unused multichoice menus, started by a RAM trigger only while the player is free to move; walk-through-walls, the Pokédex and the new Pokémon's personality are small Thumb routines (`tools/golden/walls.s` and `pid.s`, over the unused `Unused_AdjustBgMosaic`, and `dex.s`). Shiny, Nature and Gender share `pid.s`: each cheat carries the same routine and hook in `CreateBoxMon` (wild, gift and egg Pokémon, not trainers') and sets only its own switch in free ROM (`0x08F1F000`), so any of them work together; AnyGBA's shiny patch and the old nature and gender patches each set the personality alone and undid the others. The emulators copy keeps the old patches, as mGBA's lines can't hold the routine.

`python3 tools/golden/emerald.py > "cheats/gba/Pokemon - Emerald Version (USA, Europe).cht"` rebuilds it; `--emulators` writes `cheats/gba/emulators/Pokemon - Emerald Version (USA, Europe).cht`, the copy for mGBA and RetroArch without the Torii-only cheats (the menus, whose code lines pass mGBA's 512 characters and make it reject the whole file, and the keyed money and coins). The file has no comments: mGBA stops reading at a line that doesn't start with `cheat`.

## Install

Copy the file to the card as `/torii/cheats/<the ROM's path without its extension>.cht`: for `/Pokemon - Emerald.gba`, `/torii/cheats/Pokemon - Emerald.cht`; for `/GBA/Pokemon - Emerald.gba`, `/torii/cheats/GBA/Pokemon - Emerald.cht`. Copy it by hand rather than through `just cheats`: `tools/cheats.py` leaves out cheats of over 256 lines, which drops the menus. Then pick cheats on the game's Cheats screen before playing: every cheat that patches the game (the menus, the Pokémon, battle and wild ROM cheats) is set at launch and can't be switched in game; the RAM ones can, from the in-game menu (L+R+START).

Needs Torii with cheat folders and the larger ROM-patch limits (2,048 a cheat, 4,096 picked; branch `claude/rom-patch-limits-a52fb3`). The first open of the file indexes it in about 7 seconds (469 cheats, 6 windows); later opens read the index.

## Tests

On the emulated cart with the real ROM (CRC32 `1F1C08FB`) and a 100% save (Project Pokémon's "Five Star" Emerald save, Italian, 8 badges, game cleared, 386 Pokédex; it loads on the USA ROM), by reading RAM after the cheat ran and by screenshots. Scripts in `dist/eg/` (not committed).

| Cheat | Result |
| --- | --- |
| Max money, Max coins | 999,999 and 9,999 decoded with the save's key |
| Max Battle Points, Eggs hatch fast, Full Soot Sack, Level 80 Pokéblock, Gym rematch flags, No wild encounters | the save bytes as intended (BP 9,999, Day Care byte FE, ash 9,999, slot 40 `0E FF FF FF FF FF 01 00`, rematch 65-72 = 1, immunity 0) |
| Species, Level, shiny, Perfect IVs, Wild gender | a wild Ralts in Granite Cave: Lv 50, shiny, all IVs 31, female (male with Male) |
| Shiny, Nature, Gender | one routine (`tools/golden/pid.s`) picks a personality with all three: shiny + Adamant + female + Perfect IVs gave a shiny female Adamant Ralts with IVs all 31; shiny + Modest and Jolly alone right too; the control a random Lonely one |
| EXP x10, EXP for the whole party, Infinite PP, One-hit KO | against a control battle: the lead +50 EXP and 1 PP used; with them every party Pokémon +80 (50 shared by six, x10), no PP used, one hit |
| Max EVs | HP EVs 58 to 81: the 510 total filled |
| Infinite HP | 209/209 through a Lv 100 Machamp's attacks; without it, fainted |
| Every ball catches, Pokérus | a full-HP Lv 100 Ralts caught with one Great Ball, Pokérus set |
| Walk through walls | blocked without R; through the Pokémon Center counter with R |
| Item shop, Everything costs 1 | the 8 categories, the TM Mart; TM01 for "₽1", money 329,622 to 329,621, TM01 4 to 5 |
| Tickets | "Obtained the EON TICKET!"; the ferry flag set; a second pick says you have it |
| Field menu | Heal, PC (and L+R+Down), Warp (Oldale, Sky Pillar, Southern Island's dock; L+R+Left), Legends (Regirock's defeated and hidden flags cleared, Regice's not), Gifts ("Jack received TREECKO!"), Moves (L+R+Right, the relearner), Unlock (badges, symbols; the Pokédex's bits restored from zero) |
| Quick contests, Max contest score | a Normal Cool contest: results after the first round, first place with full stars; without them, still in the appeals |
| The emulators copy | mGBA reads all 465 cheats (all on at once) |

| Skip trainer battles | Route 102's Rick, his defeat flag cleared: he challenges in the control; with it the player walks past |
| Fly to any town | GameHacking's RAM code didn't work on this ROM (the Fly map's state isn't where it writes); rebuilt as one ROM patch of `GetMapsecType` (a town not visited is "can fly"): with every visited flag cleared, Littleroot can be flown to, the control can't |
| Free Day Care | taking a Pokémon back: ₽100 taken in the control, none with it (the lady still names the price; the guide says so) |
| Field moves without badges | badges cleared: Fly from the party menu says "can't be used until a new BADGE" in the control, opens the Fly map with it |
| No species limit at the Battle Tower | a Mewtwo in the party: "NOT ABLE" in the control, "ABLE" with it (Open Level) |
| Infinite Safari Zone | entered by the gate: steps 494 after walking in the control, 500 with it, 30 balls |
| Max friendship | a Treecko (70) walked: 72 in the control, 255 with it |
| Eon Ticket from the shop | on a save without it (flag and ticket cleared): the Lilycove ferry accepts it ("a tiny spit of an island far in the south") |

Screenshots of all of these: `dist/eg/gallery-*.png` (not committed). Still for the real cart: the menus' launch with about 2,500 ROM patches (Torii estimates 260 ms more), and every cheat at a real game's speed.

## Kernel changes this needed

- Cheat folders (`cheatN_folder`): branch `claude/cheat-folders-449ad5`, merged to main.
- ROM patches free in game and 2,048 a cheat, 4,096 picked: branch `claude/rom-patch-limits-a52fb3`.
- `raw_line` kept the previous line's key for a line without `^KEY`, so a cheat whose first keyed line was `[P]+O|V` was refused on the cart: fixed on the same branch (399cbbd8); the golden set no longer uses `|`.

## The decisions (2026-10-31)

D1 device formats where they do the same; Torii's raw lines only for XOR. D2 every group the owner's call. D3 a downloaded 100% save. D4 pickers as choice groups, and headings (D5 `cheatN_folder`, D6 built in its own session). D8 max money, coins, BP, ₽1 prices. D9 one shop on a combo, categories inside. D11 tickets with their flags. D12 no pocket fills. D14 game prices, the price cheat makes everything ₽1. D15 one combo per menu, game-native menus. D16 menu data as ROM patches. D17 L+R+B. D18-D19, D50 the kernel's limits lifted so each menu is one cheat. D20 the item menu holds items only; D21-D22 eight categories, no key items. D23 the field menu, L+R+A plus arrows (D44). D24 all new Pokémon shiny. D25 perfect IVs, friendship, nature, gender, Pokérus, max EVs. D26 Pokédex (later the field menu's Unlock, D49), fast eggs. D27 no max stats or starter scripts; gifts in the field menu. D28 one-hit KO, infinite HP and PP, skip trainers. D29 the EXP multiplier and EXP for all. D30 every ball catches. D31 Safari. D32-D34 species, level, encounters. D35, D39 fly anywhere, free Day Care, walls on R. D36-D37 badges (later Unlock), field moves without badges. D38, D45 legends and gifts (with the Johto starters) in the field menu. D40, D46 warps there, paged. D41, D47 rematches: the gym leaders' rematch flags. D42 no species limit at the Tower, symbols (later Unlock). D43 Pokéblock, contests, Soot Sack. D48 the `raw_line` fix.
