# Pokémon Emerald: the cheats we have

A survey for a curated cheat set, starting with Pokémon Emerald (USA, Europe), CRC32 `1F1C08FB`. It covers every cheat this PC has for the game, decoded by the kernel's own decoder (`tools/cheat_patches.c --raw --ops`), so the addresses and conditions below are what Torii would run, not what the names claim.

## Sources

| Source | Cheats | Torii reads | Not usable |
| --- | --- | --- | --- |
| GameHacking.org export (`dist/cheats/gamehacking/1F1C08FB.cheats`) | 170 (6 more dropped on reading as repeats) | 162 | 4 master codes, 4 unreadable ("Level 25/50/75/100") |
| libretro database, Code Breaker (`dist/cheat-corpus/…(Code Breaker).cht`) | 74 | 64 | 1 master code, 9 unreadable |

Of libretro's 64 usable cheats, 61 run exactly the same program as a GameHacking cheat, 21 of them under another name (see "Names" below). Only three are its own:

- **Anti DMA**: a ROM patch at `08076BEE`. GameHacking's "(M)" patches `08076BEA` instead, 4 bytes earlier. Both work: see "The base code" below.
- **Complete Pokedex**: writes one byte of each Pokédex flag array where GameHacking's "Pokedex Complete" writes all 48 bytes. libretro's is a broken copy.
- **1-Hit Kill**: `02024104 = 01` always, the same address as GameHacking's "Battle: 1 Hit Kills", which writes only when the value is above 1.

Several libretro cheats decode to garbage: they are encrypted lines decoded with the wrong key, such as reads of `0C0F…` and writes to `0D16A646`, in the cartridge's save area. "Free MOD Shop Items" and "Have All Pokeballs" still read as usable because their good part matches GameHacking's. A golden set must never take these.

**Conclusion:** GameHacking is the one source worth curating from. libretro only confirms its codes.

## How Emerald's codes work

Most Emerald codes are one of a few mechanisms. Knowing them explains the overlaps.

- **The base code, "(M)".** GameHacking's working "(M)" is not a plain master code. It does two things:
  - It patches the ROM so the save blocks stay put. `SetSaveBlocksPointers` (`08076BDC`) places the save blocks at a random offset, `(offset + Random()) & 0x7C` (0 to 124 bytes), each time it runs. Both GameHacking's patch at `08076BEA` (the add) and libretro's at `08076BEE` (the mask) write `movs r4, #0`, so the offset is always 0: they're equivalent.
  - It zeroes the save's encryption key (`*[03005D90]+AC/AE = 0`). Money, coins and item quantities are stored XORed with that key. Zeroing it lets codes write plain values, but every such value already saved reads as garbage once the key is 0, until a code or the game sets it again.
  
  Codes that write save data at a fixed address (money, items, Pokédex, tickets) need the patch, or they hit the wrong bytes 31 times in 32; those writing a value the game keeps XORed need the key zeroed too. Codes on the party, battle state or `03000E…` script hooks need neither. "Needs the base cheat" below lists them.
- **Script hooks at `03000E38`–`03000E4E`.** About 35 codes write a script address into the game's script engine while a key combination is held, and the game runs that script: badges, HMs, opening a shop, PC or bag, battles with legendaries, starters, PokéNav. They are powerful but blunt, and two codes on the same combination fight.
- **Warps at `020322E4`.** 23 codes write a map number while **L** is held: walk through any door to arrive there. Five of them also exist in an always-on copy, which traps you, since every door then leads there.
- **Trainer rematch at `02038BCA`.** 13 codes set the next opponent (gym leaders, Elite Four, Wallace) while **Select** is held.
- **Bag pocket rewrites at `02002DD0`–`02002F6C`.** "All Berries", "All Hold Items", "All Misc Items", "All TM's/HM's" and "All Decorations" each come in two parts. They rewrite the same memory, so only one pocket code can be on at a time, and they overlap the Berry Blender and contest codes' addresses (`02002EF2`–`02002F56`) too.
- **Plain writes:** money, coins, Battle Points, Safari, stats, EXP, catch rate, Pokédex.
- **ROM patches** (run once at launch): Skip Trainer Battles, Opponent Can't Attack, No Species Restriction at Battle Tower.

## Needs the base cheat

The save blocks, as `SetSaveBlocksPointers` places them at offset 0 (its literals at `08076C14`): SaveBlock2 at `02024A54` (pointer `03005D90`, 0xF2C bytes), SaveBlock1 at `02025A00` (`03005D8C`, 0x3D88) and the PC boxes at `02029808` (`03005D94`, 0x83D0). The values the game keeps XORed with the key (pokeemerald's `ApplyNewEncryptionKeyToAllEncryptedData`) are SaveBlock1's money (+490), coins (+494), the bag's item quantities (+560 to +848) and the game stats (+159C).

Every usable cheat that writes into those blocks (`EWRAM` mirrors folded), from both sources:

| Cheat | Writes | Needs |
| --- | --- | --- |
| Infinite Money | SaveBlock1 +490 | Patch and key |
| Have All Pokeballs | SaveBlock1 +650 (Poké Ball pocket, quantities) | Patch and key |
| Have All TMs/HMs | SaveBlock1 +690 (TM pocket, quantities) | Patch and key |
| Mystic Ticket, Aurora Ticket, Have Old Sea Map | SaveBlock1 +4E0 (a PC item slot: the item, not its count) | Patch |
| Pokedex Complete (and libretro's Complete Pokedex) | SaveBlock2 +28 to +8B, SaveBlock1 +988 and +3B24 | Patch |
| Lvl 80 Pokeblock | SaveBlock1 +848 | Patch |
| Soot Sack Full | SaveBlock1 +142C (written at `02066E2C`, a mirror) | Patch |
| Infinite BP | SaveBlock2 +EB8 (written at `020A590C`, a mirror) | Patch |
| Low Game Time | SaveBlock2 +10 | Patch |
| Shiny Groudon | PC boxes +79E4 (box 14) | Patch |

Every other cheat works without it. "Max Money" (`02000568`) and "Max Coins" (`02002DEC`) don't touch the save blocks at all: they write somewhere else, which doesn't look like money or coins, so they are likely broken (to test).

**Checked in the game** (`dist/survey/emerald_base.sh`, Emerald from `roms/` on the emulated cart, no save): without the base cheat, the game put its save blocks 0x48 bytes on (SaveBlock1 at `02025A48`), where the fixed-address codes miss. With it picked, all three sat at offset 0 at every check (frames 1155 to 3555) and the ROM read `2400` at `08076BEA`, as in plain mGBA running the same file. Torii read it as one cheat with one ROM patch and two pointer writes, not greyed. The ROM patch goes in at launch, so the cheat must be picked before the game starts: turned on from the in-game menu, only its key writes would run.

**Its name:** players know it as the **Master Code**, and by what it does as **Anti-DMA**. "Master Code (Anti-DMA)" says both. Torii greys plain master codes, which do nothing on a cart, but this one does real work, so it isn't greyed.

## Standalone cheats (no master code)

Rather than one "Master Code (Anti-DMA)" every money or item cheat depends on, each save-data cheat carries what it needs itself: the anti-DMA patch, and for money and bag counts the key zeroing too (`*[03005D90]+AC/AE = 0`). There is no master code to forget, and nothing is scrambled unless a money or bag cheat is picked. They are Action Replay v3 codes (the only format with both pointer writes and ROM patches; CodeBreaker has neither), encrypted as the devices take them, so they run in mGBA and on the devices too.

The draft is `cheats/gba/Pokemon - Emerald Version (USA, Europe).cht`, built by `dist/survey/golden_b.py` from GameHacking's codes:

| Cheat | From | Key zeroed | Change |
| --- | --- | --- | --- |
| Infinite Money | Infinite Money | Yes | |
| All Poké Balls (99 each) | Have All Pokeballs | Yes | |
| All TMs & HMs (9 each) | Have All TMs/HMs | Yes | Named for what it gives: 9, not 99 |
| Mystic Ticket (in PC), Aurora Ticket (in PC), Old Sea Map (in PC) | The same | No | Each in a PC item slot of its own (47, 48, 49), with a count of 1; all three wrote only the item into slot 18, over each other and whatever was there |
| Complete Pokédex | Pokedex Complete | No | |
| Level 80 Pokéblock, Full Soot Sack, Infinite Battle Points, Low Game Time, Shiny Groudon (PC box 14) | The same | No | Mirror addresses written as the addresses they mirror |

**Checked** (`dist/survey/golden_b_test.sh`): Torii's decoder reads each as one ROM patch, the key writes where meant, then the original writes unchanged. In Emerald on the emulated cart, all 12 picked together landed all 295 writes with the patch in (12 cheats patching the same halfword alike is fine); Infinite Money alone did too; plain mGBA running the same file matched. The 12 together take 62% of the cheat budget (load 39,756 of 64,000), most of it Shiny Groudon's and the Pokédex's writes: the rest of the set has room, but not for everything at once.

## The groups

Button conditions are decoded from the codes (`KEYINPUT`), not taken from the names.

### Base

| Cheat | Does | Note |
| --- | --- | --- |
| (M) → **Master Code (Anti-DMA)** | ROM patch `08076BEA` + encryption key zeroed | Needed by the codes listed above |
| (M) | Unreadable | Drop |
| [M] Must Be On ×2, Notice | Master codes only | Drop |

### Warps (hold L, walk through a door)

Battle Frontier, Birth Island, Dewford Town, Ever Grande City, Fallarbor Town, Faraway Island, Fortree City, Lavaridge Town, Lilycove City, Littleroot Town, Mauville City, Mossdeep City, Navel Rock, Oldale Town, Pacifidlog Town, Petalburg City, Rustboro City, Safari Zone, Slateport City, Sootopolis City, Southern Island, Verdanturf Town: 22 on L.

Always-on duplicates: Birth Island, Faraway Island, Naval Rock (spelled so), Southern Island. Faraway Island's two copies disagree: `391A` (on L) against `381A` (always). One map number is wrong.

### Trainer rematch (hold Select)

Gym leaders Roxanne, Brawly, Watson, Flannery, Norman, Winona, Tate&Liza and Juan, Elite Four Sidney, Phoebe, Glacia and Drake, Champion Wallace: 13 on Select.

### Script hooks (key combination)

| Cheat | Combination |
| --- | --- |
| Badges: Stone, Knuckle, Dynamo, Heat, Balance, Feather, Mind, Rain | Select + Up, Right, Down, Left; Select + L + Up, Right, Down, Left |
| Have Surf Ability | Select |
| Have Dive | Select + R |
| Have Flash (says L+Select) | **Select + Up**, the same as Stone Badge |
| Have Cut / Rock Smash / Waterfall / Secret Power | Rewrite a script the game runs (no key) |
| Access PC | B + Select |
| Access Fly Map (says Select) | **A + Select** (writes `030022C4`, not a script) |
| Access Birch's Bag | L + R + Up |
| Access Deco Shop / Access MOD Shop | both A + Select + L: they clash |
| Access Pyramid Challenge | Select |
| Enable National Dex | Select |
| Install Match Call / Install PokeNAV | Select + R + Up / Select + R + Down |
| Battle Mew / Choose Latias/Latios | both Select + L: they clash |
| Battle Deoxys / Battle Latias/Latios / Have Dive | all Select + R: they clash |
| Battle Ho-oh / Battle Lugia | A + Select + R / B + Select + L |
| Receive Starter, Chikorita, Cyndaquil, Totodile | all Down + R + L: they clash |
| Receive BP, Receive Symbol, Re-Battle Activator | Select |
| Early Access to Magma Hideout, Infinite Wynaut Eggs | Always on, no key |

A code on "Select held" fires whenever Select is down, inside any combination too: Surf, National Dex, Pyramid, Receive BP, Receive Symbol and Re-Battle, and all 13 rematches, fire with every Select combination above. The 22 warps fire the same way whenever L is down.

### Items

| Cheat | Note |
| --- | --- |
| All Berries, All Hold Items, All Misc Items, All TM's/HM's, All Decorations (each Part 1 + Part 2) | Rewrite the bag's pockets: one at a time, both parts on |
| All Mail, All Pokeballs | Smaller pocket rewrites, same addresses |
| Have All Pokeballs, Have All TMs/HMs | A cleaner way: fixed slots `02026050`, `02026090` (99 of each) |
| Mystic Ticket, Aurora Ticket, Have Old Sea Map | All write the same key item slot `02025EE0`: one at a time |
| Free MOD Shop Items, Items Cost $1 | Shop prices |

### Money and points

| Cheat | Address | Note |
| --- | --- | --- |
| Infinite Money | `02025E90` = 999999 | Needs the base code |
| Max Money | `02000568` | Another address: one of the two is suspect |
| Max Coins | `02002DEC` = 9999 | |
| Max Coins From Pokies | Same, only in the slots | |
| Infinite BP | `020A590C` | Past the end of EWRAM (mirrors `0202590C`): suspect |
| Receive BP | Script on Select | |

### Battle

| Cheat | Note |
| --- | --- |
| 1-Hit Kill (on L), 1-Hit Double Battle Kill (on L), 1-Hit Win (on R) | Zero the opponents' HP; overlapping |
| Battle: 1 Hit Kills | `02024104` clamped to 1 |
| Always Your Turn, Easy Win (Doubles), Easy Win (Trainer) | |
| Opponent Can't Attack, Skip Trainer Battles | ROM patches |
| 1st–6th Pokemon Max Stats | Six; GameHacking's "Pokemon #1–6: Max Stats" are the same six again |
| Infinite PP | |

### EXP and catching

| Cheat | Note |
| --- | --- |
| Mini / Medium / Max EXP Gain After Battle | Same two addresses: pick one |
| Gain 5000 Exp (A + R), Battle: R = More EXP, Recieve More Exp, Exp Share | Overlap with the three above |
| 100% catch Rate, Wild PKMN Easily Caught, Safari PKMN Easily Caught | All write `02024214`: overlap |
| Wild Pokemon Catching Spree, Wild Pokemon Modifier | |
| Shiny Groudon | Writes a whole Pokémon's data |
| Level 25/50/75/100 | Unreadable: drop |

### Overworld

No Random Battles ×2 (8-bit and 16-bit writes of the same address; libretro has a third), Max Repel Steps, Walk in Menu/Conversation, The Human Bike (B + Up on, B + Down off), Fly Anywhere On Map, Fly codes, Free Daycare, Low Game Time, Skip Intro, Change PLYR Name.

### Minigames

- **Berry Blender:** Max Direct Hits, Max Lvl Gold (Beauty, Cool, Cute, Smart, Tough), Maximum Feel, No Misses, Quick Blend.
- **Contests:** Quick Contest, Maximum Hearts, Maximum Score.
- **Other:** Lvl 80 Pokeblock, Soot Sack Full, Infinite Safari Balls, Infinite Safari Time.
- **Battle Frontier:** No Species Restriction At Battle Tower.

## Names

GameHacking's names mix styles: "[Press …]" in some, none in most, "Rght", "Chall", "Recieve", "Fre Daycare" (libretro), "Pokies", and "(Part 1)"/"(Part 2)" for halves that only work together. libretro names the same codes differently ("Receive Stone Badge" for "Stone Badge [Press Select+Up]", "Berry Blender …" for GameHacking's bare names). Two labels are wrong (Have Flash, Access Fly Map), and 22 warps and 13 rematches say nothing of their L and Select.

## For the golden set

1. **Base:** one required base cheat (anti-DMA + encryption key), tested, picking the right ROM address of the two.
2. **One code per effect:** pick one of each overlapping family (EXP, catching, 1-hit, money, No Random Battles) and drop the rest.
3. **Merge parts:** a pocket's two parts as one cheat (Torii runs up to 256 lines a cheat).
4. **Buttons:** no two cheats on one combination, and the combination in every name, in one style ("Hold L: …", or "(Select+Up)").
5. **Warps and rematches:** perhaps one cheat per destination as now, named "Warp: Lilycove City (hold L, take a door)".
6. **Test:** each in Torii on Emerald before it goes in (`roms/` has the game, so the emulator can check what each code changes): Infinite BP's and Max Money's addresses, and Faraway Island's map, are the doubtful ones.
