# Pokémon Emerald: golden cheat curation

Every cheat we have (GameHacking, plus libretro's three of its own), grouped. "Proposal" is mine; "Decision" is yours. Buttons are decoded from the codes: "(any)" means the code fires whenever that button is down, in any combination.

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
| 11 | Free MOD Shop Items |  | keep |  | |
| 12 | Infinite Money |  | keep (golden) | standalone | |
| 13 | Items Cost $1 |  | keep |  | |
| 14 | Max Coins |  | test | writes 02002DEC, not the coins (SaveBlock1 +494): likely wrong; a correct one can be made | |
| 15 | Max Coins From Pokies |  | test | writes 02002DEC, not the coins (SaveBlock1 +494): likely wrong; a correct one can be made | |
| 16 | Max Money |  | test | writes 02000568, not the money: likely wrong | |
| 17 | Maximum Sale Price |  | keep |  | |

## 4 Items

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 18 | All Berries (Part 1) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | |
| 19 | All Berries (Part 2) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | |
| 20 | All Decorations (Part 1) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | |
| 21 | All Decorations (Part 2) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | |
| 22 | All Hold Items (Part 1) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | |
| 23 | All Hold Items (Part 2) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | |
| 24 | All Mail |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | |
| 25 | All Misc Items (Part 1) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | |
| 26 | All Misc Items (Part 2) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | |
| 27 | All Pokeballs |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | |
| 28 | All TM's/HM's (Part 1) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | |
| 29 | All TM's/HM's (Part 2) |  | test | write 02002Dxx, outside the save blocks (a bag menu buffer?); parts must go together | |
| 30 | Aurora Ticket |  | keep (golden) | own PC slots | |
| 31 | Have All Pokeballs |  | keep (golden) | standalone | |
| 32 | Have All TMs/HMs |  | keep (golden) | standalone | |
| 33 | Have Old Sea Map |  | keep (golden) | own PC slots | |
| 34 | Mystic Ticket |  | keep (golden) | own PC slots | |

## 5 Pokédex & Pokémon

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 35 | 1st Pokemon Max Stats |  | keep |  | |
| 36 | 2nd Pokemon Max Stats |  | keep |  | |
| 37 | 3rd Pokemon Max Stats |  | keep |  | |
| 38 | 4th Pokemon Max Stats |  | keep |  | |
| 39 | 5th Pokemon Max Stats |  | keep |  | |
| 40 | 6th Pokemon Max Stats |  | keep |  | |
| 41 | Chikorita | R+L+Down | decide | all four on Down+R+L: clash | |
| 42 | Cyndaquil | R+L+Down | decide | all four on Down+R+L: clash | |
| 43 | Enable National Dex [Press Select] | Select (any) | keep |  | |
| 44 | Infinite Wynaut Eggs |  | keep |  | |
| 45 | Pokedex Complete |  | keep (golden) | standalone | |
| 46 | Receive Starter | R+L+Down | decide | all four on Down+R+L: clash | |
| 47 | Shiny Groudon |  | keep (golden) | standalone | |
| 48 | Totodile | R+L+Down | decide | all four on Down+R+L: clash | |
| 49 | Wild Pokemon Modifier |  | keep |  | |
| 50 | Complete Pokedex (libretro) |  | drop | broken copy (1 byte of 48) | drop |

## 6 Battle

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 51 | 1-Hit Double Battle Kill (Instant Win) | L | decide | overlap: pick one or two | |
| 52 | 1-Hit Kill | L | decide | overlap: pick one or two | |
| 53 | 1-Hit Win | R | decide | overlap: pick one or two | |
| 54 | Always Your Turn |  | keep |  | |
| 55 | Battle: 1 Hit Kills |  | decide | overlap: pick one or two | |
| 56 | Easy Win (Doubles) |  | keep |  | |
| 57 | Easy Win (Trainer) |  | keep |  | |
| 58 | Infinite PP |  | keep |  | |
| 59 | Opponent Can't Attack |  | keep |  | |
| 60 | Skip Trainer Battles |  | keep |  | |
| 61 | 1-Hit Kill (libretro) |  | decide | overlap: pick one or two | |

## 7 EXP

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 62 | Battle: R = More EXP |  | decide | overlap: same addresses | |
| 63 | Exp Share |  | decide | overlap: same addresses | |
| 64 | Gain 5000 Exp | A+R | decide | overlap: same addresses | |
| 65 | Max EXP Gain After Battle |  | decide | overlap: same addresses | |
| 66 | Medium EXP Gain After Battle |  | decide | overlap: same addresses | |
| 67 | Mini EXP Gain After Battle |  | decide | overlap: same addresses | |
| 68 | Recieve More Exp |  | decide | overlap: same addresses | |

## 8 Catching

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 69 | 100% catch Rate |  | decide | first three write the same address | |
| 70 | Safari PKMN Easily Caught |  | decide | first three write the same address | |
| 71 | Wild PKMN Easily Caught |  | decide | first three write the same address | |
| 72 | Wild Pokemon Catching Spree |  | decide | first three write the same address | |

## 9 Overworld

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 73 | Access Fly Map [Press Select] | Select+A | keep |  | |
| 74 | Change PLYR Name (View Trainer Card) |  | keep |  | |
| 75 | Early Access to Magma Hideout |  | keep |  | |
| 76 | Fly Anywhere On Map |  | keep |  | |
| 77 | Fly codes |  | keep |  | |
| 78 | Free Daycare |  | keep |  | |
| 79 | Low Game Time |  | keep |  | |
| 80 | Max Repel Steps |  | keep |  | |
| 81 | No Random Battles |  | decide | duplicates: an 8-bit and a 16-bit write | |
| 82 | No Random Battles |  | decide | duplicates: an 8-bit and a 16-bit write | |
| 83 | Skip Intro |  | keep |  | |
| 84 | The Human Bike | B+Up / B+Down | keep |  | |
| 85 | Walk in Menu/Conversation |  | keep |  | |

## 10 HMs & badges

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 86 | Access Secret Power |  | keep |  | |
| 87 | Balance Badge [Press Select+L+Up] | Select+L+Up | keep |  | |
| 88 | Dynamo Badge [Press Select+Down] | Select+Down | keep |  | |
| 89 | Feather Badge [Press Select+L+Rght] | Select+L+Right | keep |  | |
| 90 | Have Cut Ability |  | keep |  | |
| 91 | Have Dive [Press R+Select] | Select+R | keep |  | |
| 92 | Have Flash [Press L+Select] | Select+Up | keep |  | |
| 93 | Have Rock Smash Ability |  | keep |  | |
| 94 | Have Surf Ability [Press Select] | Select (any) | keep |  | |
| 95 | Heat Badge [Press Select+Left] | Select+Left | keep |  | |
| 96 | Knuckle Badge [Press Select+Rght] | Select+Right | keep |  | |
| 97 | Mind Badge [Press Select+L+Down] | Select+L+Down | keep |  | |
| 98 | Rain Badge [Press Select+L+Left] | Select+L+Left | keep |  | |
| 99 | Stone Badge [Press Select+Up] | Select+Up | keep |  | |
| 100 | Waterfall |  | keep |  | |

## 11 Menus & shops

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 101 | Access Birchs Bag [Press L+R+Up] | R+L+Up | keep |  | |
| 102 | Access Deco Shop [Press L+A+Select] | Select+A+L | keep |  | |
| 103 | Access MOD Shop [Press L+A+Select] | Select+A+L | keep |  | |
| 104 | Access PC [Press B+Select] | Select+B | keep |  | |
| 105 | Install Match Call [Press Select+R+Up] | Select+R+Up | keep |  | |
| 106 | Install PokeNAV [Press Select+R+Down] | Select+R+Down | keep |  | |

## 12 Legendaries

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 107 | Battle Deoxys [Press R+Select] | Select+R | keep |  | |
| 108 | Battle Ho-oh [Press R+A+Select] | Select+A+R | keep |  | |
| 109 | Battle Latias/Latios [Press R+Select] | Select+R | keep |  | |
| 110 | Battle Lugia [Press L+B+Select] | Select+B+L | keep |  | |
| 111 | Battle Mew [Press L+Select] | Select+L | keep |  | |
| 112 | Choose Latias/Latios [Press L+Select] | Select+L | keep |  | |

## 13 Warps (hold L, take a door)

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 113 | Battle Frontier | L (any) | keep |  | |
| 114 | Birth Island | L (any) | keep |  | |
| 115 | Birth Island |  | drop | always-on copy: every door leads there | drop |
| 116 | Dewford Town | L (any) | keep |  | |
| 117 | Ever Grande City | L (any) | keep |  | |
| 118 | Fallarbor Town | L (any) | keep |  | |
| 119 | Faraway Island | L (any) | keep |  | |
| 120 | Faraway Island |  | drop | always-on copy: every door leads there | drop |
| 121 | Fortree City | L (any) | keep |  | |
| 122 | Lavaridge Town | L (any) | keep |  | |
| 123 | Lilycove City | L (any) | keep |  | |
| 124 | Littleroot Town | L (any) | keep |  | |
| 125 | Mauville City | L (any) | keep |  | |
| 126 | Mossdeep City | L (any) | keep |  | |
| 127 | Naval Rock |  | drop | always-on copy: every door leads there | drop |
| 128 | Navel Rock | L (any) | keep |  | |
| 129 | Oldale Town | L (any) | keep |  | |
| 130 | Pacifidlog Town | L (any) | keep |  | |
| 131 | Petalburg City | L (any) | keep |  | |
| 132 | Rustboro City | L (any) | keep |  | |
| 133 | Safari Zone | L (any) | keep |  | |
| 134 | Slateport City | L (any) | keep |  | |
| 135 | Sootopolis City | L (any) | keep |  | |
| 136 | Southern Island | L (any) | keep |  | |
| 137 | Southern Island |  | drop | always-on copy: every door leads there | drop |
| 138 | Verdanturf Town | L (any) | keep |  | |

## 14 Rematches (hold Select)

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 139 | Champion Wallace | Select (any) | keep |  | |
| 140 | Elite Four Drake | Select (any) | keep |  | |
| 141 | Elite Four Glacia | Select (any) | keep |  | |
| 142 | Elite Four Phoebe | Select (any) | keep |  | |
| 143 | Elite Four Sidney | Select (any) | keep |  | |
| 144 | Gym Leader Brawly | Select (any) | keep |  | |
| 145 | Gym Leader Flannery | Select (any) | keep |  | |
| 146 | Gym Leader Juan | Select (any) | keep |  | |
| 147 | Gym Leader Norman | Select (any) | keep |  | |
| 148 | Gym Leader Roxanne | Select (any) | keep |  | |
| 149 | Gym Leader Tate&Liza | Select (any) | keep |  | |
| 150 | Gym Leader Watson | Select (any) | keep |  | |
| 151 | Gym Leader Winona | Select (any) | keep |  | |
| 152 | Re-Battle Activator | Select (any) | keep |  | |

## 15 Battle Frontier

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 153 | Access Pyramid Chall [Press Select] | Select (any) | keep |  | |
| 154 | Infinite BP |  | keep |  | |
| 155 | No Species Restriction At Battle Tower |  | keep |  | |
| 156 | Receive BP [Hold Select] | Select (any) | keep |  | |
| 157 | Receive Symbol | Select (any) | keep |  | |

## 16 Minigames

| # | Cheat | Buttons | Proposal | Note | Decision |
| --- | --- | --- | --- | --- | --- |
| 158 | Infinite Safari Balls |  | keep |  | |
| 159 | Infinite Safari Time |  | keep |  | |
| 160 | Lvl 80 Pokeblock |  | keep |  | |
| 161 | Max Direct Hits |  | keep |  | |
| 162 | Max Lvl Gold (Beauty) |  | keep |  | |
| 163 | Max Lvl Gold (Cool) |  | keep |  | |
| 164 | Max Lvl Gold (Cute) |  | keep |  | |
| 165 | Max Lvl Gold (Smart) |  | keep |  | |
| 166 | Max Lvl Gold (Tough) |  | keep |  | |
| 167 | Maximum Feel |  | keep |  | |
| 168 | Maximum Hearts |  | keep |  | |
| 169 | Maximum Score |  | keep |  | |
| 170 | No Misses |  | keep |  | |
| 171 | Quick Blend |  | keep |  | |
| 172 | Quick Contest |  | keep |  | |
| 173 | Soot Sack Full |  | keep |  | |
