# Torii code

Torii code is Torii's own cheat format: a superset of VBA's `address:value` lines and AnyGBA's extensions, with tests, blocks, more writes and a switch. A cheat file names it `cheatN_format = "Torii"`; `"VBA"`, its name before, reads the same. Without a format line, a line with any of `: | [ & # = ! < > * ~ @ $ { }` reads as Torii code, so it mixes with CodeBreaker and Action Replay lines in one cheat.

Only Torii reads the lines below past VBA's and AnyGBA's. mGBA, VBA-M and the devices don't.

## Lines

A cheat is lines joined by `+` (or spaces or new lines). Each line is one statement.

- A **value** is 2, 4 or 8 hex digits: a byte, a halfword or a word.
- A **target** is `AAAAAAAA`, or `[PPPPPPPP]+O`: the u32 at `P`, plus `O`.

| Line | Does | From |
| --- | --- | --- |
| `TARGET:VALUE` | write; at `08000000`-`0DFFFFFF`, a ROM patch made at launch | VBA |
| `TARGET:VALUE^KEY` | write VALUE XOR the bytes at KEY (an address or `[P]+O`) | AnyGBA |
| `TARGET\|VALUE` | set VALUE's bits | AnyGBA |
| `AAAAAAAA&VALUE` | clear the bits VALUE hasn't | Torii |
| `AAAAAAAA#VALUE` | add VALUE, wrapping at the width | Torii |
| `AAAAAAAA=VALUE` | test: equal | Torii |
| `AAAAAAAA!VALUE` | test: not equal | Torii |
| `AAAAAAAA<VALUE` | test: below (unsigned) | Torii |
| `AAAAAAAA>VALUE` | test: above (unsigned) | Torii |
| `AAAAAAAA*VALUE` | test: any of VALUE's bits set | Torii |
| `AAAAAAAA~VALUE` | test: none of VALUE's bits set | Torii |
| `@MASK` | test: every button of MASK held (1 to 4 digits, KEYINPUT's bits) | Torii |
| `$AAAAAAAA` or `$AAAAAAAA:VV` | the cheat's switch (below) | Torii |

The buttons' bits: A `001`, B `002`, Select `004`, Start `008`, Right `010`, Left `020`, Up `040`, Down `080`, R `100`, L `200`. So `@0300` is L+R.

## Tests and blocks

A test covers the next line: it runs only while the test holds. Ending a test with `{` makes it cover every line up to `}`. A `}{` between them starts the lines that run when the test fails. Blocks nest four deep, and a test right before a block covers the whole block.

```
02024104=0001+02024106:0063
```
While the halfword at `02024104` is 1, keep `02024106` at `0x63`.

```
@0300{+0203A000#01+}{+0203A000:00+}
```
While L+R are held, count up the byte at `0203A000`; otherwise keep it at 0.

Tests run every frame, as the devices' conditional codes do. A ROM patch inside a test still goes in at launch whatever the test says: the ROM can't change while a game runs. A plain write to the ROM inside a test can't run at all, and the cheat is refused.

## Switches: ROM cheats that toggle in game

The ROM can't be written while a game runs, so a cheat that patches it would stay on until Back to Torii. A switch lifts that. `$AAAAAAAA:VV` names a byte of the game's RAM that Torii keeps at `VV` (`01` without `:VV`) while the cheat is on, and at 0 while it's off:

- toggled off in the in-game menu's Cheats list;
- with every cheat off (the Cheats row).

A reset of the game turns picked cheats back on, as it always has.

The cheat's ROM patches go in at launch as always. The code they put in the game reads the byte and does the game's own thing while it's 0. A cheat with a switch toggles in game like a cheat that only writes RAM.

The rules:
- one switch a cheat;
- outside any test;
- in EWRAM or IWRAM, at a byte the game never uses (for Pokémon Emerald, past its last variable at `0x0203CF64`).

Torii writes it at each VBlank, as it runs cheats.

How the ROM code reads it is the cheat author's business: typically a routine the cheat adds, which checks the byte first and, at 0, does what the game would have done.

## On the code keypad

The keypad's symbol key turns the digits into Torii code's symbols, two rows of them: `[ ] + ^ | & # @` and `= ! < > * ~ { }`. After any symbol but `]` and `^`, it turns back to the digits. A switch's `$` isn't on the keypad: a switch only does something with ROM code made on a PC.

## Where it lives

- Parsed by `source/cheatcode.c` (`raw_line`, `torii_line`), and lowered into the in-game table by `source/cheatlower.c`; `source/cheatcode.h` sums the syntax up.
- Run each frame by `source/cheatrun.s`, which writes the switches (`CHEAT_PROG_SWITCH`).
- Encoded by `tools/cheat_encode.py`'s `Torii`.
- Tested by `tests/host/test_cheatcode.c` (`test_cheatcode_torii`, `test_cheatcode_lower_switch`) and `tests/kernel/test_cheatrun.c` (`test_cheatrun_switch`).
