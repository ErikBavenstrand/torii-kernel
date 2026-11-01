# Torii code

Torii code is Torii's own cheat format: a superset of VBA's `address:value` lines and AnyGBA's extensions, with tests, blocks and more writes. A cheat file names it `cheatN_format = "Torii"`; `"VBA"`, its name before, reads the same, so VBA cheats run as they are. Without a format line, a line with any of `: | [ & # = ! < > * ~ @ { }` reads as Torii code, so it mixes with CodeBreaker and Action Replay lines in one cheat.

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
| `AAAAAAAA<<VALUE` | test: below, signed at the value's width | Torii |
| `AAAAAAAA>>VALUE` | test: above, signed | Torii |
| `AAAAAAAA*VALUE` | test: any of VALUE's bits set | Torii |
| `AAAAAAAA~VALUE` | test: none of VALUE's bits set | Torii |
| `@MASK` | test: every button of MASK held (1 to 4 digits, KEYINPUT's bits) | Torii |
| `@@MASK` | test: MASK pressed, held this frame and not all the frame before | Torii |

The buttons' bits: A `001`, B `002`, Select `004`, Start `008`, Right `010`, Left `020`, Up `040`, Down `080`, R `100`, L `200`. So `@0300` is L+R.

## Tests and blocks

A test covers the next line: it runs only while the test holds. A test right before another test covers it and what it covers, so both must hold. Ending a test with `{` makes it cover every line up to `}`. A `}{` between them starts the lines that run when the test fails. Blocks nest four deep, and a test right before a block covers the whole block.

A block with a `}{` can't hold another `}{`, nor sit inside one: the interpreter keeps one ELSE at a time, as the devices and mGBA do. Write the inner choice as a block without one, or as its own cheat.

```
02024104=0001+02024106:0063
```
While the halfword at `02024104` is 1, keep `02024106` at `0x63`.

```
@0300{+0203A000#01+}{+0203A000:00+}
```
While L+R are held, count up the byte at `0203A000`; otherwise keep it at 0.

A cheat whose only button test is `@MASK` lists as "Hold" those buttons, and one whose only test is `@@MASK` as "Press"; Torii's cheat list shows them by the cheat, as it does a CodeBreaker button code.

Tests run every frame, as the devices' conditional codes do. A ROM patch inside a test still goes in at launch whatever the test says: the ROM can't change while a game runs. A plain write to the ROM inside a test can't run at all, and the cheat is refused.

## On the code keypad

The keypad's symbol key turns the digits into Torii code's symbols, two rows of them: `[ ] + ^ | & # @` and `= ! < > * ~ { }`. After any symbol but `]` and `^`, it turns back to the digits; type `<<`, `>>` and `@@` by pressing the symbol twice. A `{` without its `}`, a `}` without a `{`, or blocks five deep are named with their line under the code.

## Where it lives

- Parsed by `source/cheatcode.c` (`raw_line`, `torii_line`), and lowered into the in-game table by `source/cheatlower.c`; `source/cheatcode.h` sums the syntax up.
- Run each frame by `source/cheatrun.s`, as every format is.
- Shown on a cheat's details page, a block's lines indented.
- Encoded by `tools/cheat_encode.py`'s `Torii`.
- Tested by `tests/host/test_cheatcode.c` (`test_cheatcode_torii`, and the fuzz test), `tests/kernel/test_cheatrun.c` (`test_cheatrun_torii`, through the interpreter itself) and `tests/torii_code.sh` (a launch).
