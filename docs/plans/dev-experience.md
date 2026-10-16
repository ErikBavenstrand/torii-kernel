# Plan: run every supported game on the emulated cart

Goal: test the whole kernel on the emulated Omega DE cart, with real games, as
easily as on the real cart. The real cart is the reference: when a game works
there but not here, the cart model in `emu/` is wrong, and the fix is
recorded in `docs/research/omega-de-hardware.md`. Once Torii changes the
kernel, changes are tested here and confirmed on the real cart.

Starting point: Pokemon Emerald already launches from SD through the kernel
and reaches its title screen headlessly (about 1.3 s for 3,600 frames).

## 1. Emerald plays fully

Done when all of these work, launched from SD:

- New game starts (checked by hand in `just play`).
- In-game save to Flash 128K.
- Power-cycle: the kernel writes the save to `SAVER/Emerald.sav`.
- Relaunch: Continue shows the same save.
- The clock works: no "The internal battery has run dry".

Any failure here is fixed in the cart model.

## 2. One-command play

- `just play ROM...` copies each game onto the persistent cart in
  `dist/cart` if it is not already there, then opens the window. Saves stay
  between sessions; `just cart-reset` gives a blank cart.

## 3. Game set

- `roms/` is git-ignored and holds games you own and an optional starting
  save per game (`Emerald.sav`).
- A committed manifest lists each game: file checksum, game code, expected
  save type, whether it uses the clock. Missing files are skipped.
- Start with Emerald and FireRed, then add games to cover every save type
  (SRAM, EEPROM 512/8K, Flash 64K/128K), games over 16 MB and clock games.

## 4. Scripted game test

From the starting save, per game: launch, Continue, save in-game (about 10
key presses), power-cycle, check the save reached `SAVER/`, relaunch, check it
loads. New games are never scripted.

Checks read cart state, not reference screenshots:

- Boots: the screen at frame N is neither blank nor the kernel menu, and the
  cart is in game mode.
- Save type: the type the kernel sent at launch matches the manifest.
- Save survives a power cycle: `SAVER/` matches what the game stored.
- Clock: the game read valid clock values.
- Emerald also: the save counter went up after the in-game save.

Screenshots are still written for a human look.

## 5. Compatibility report

`just compat` prints one row per game, marking each check as passed or
failed, and writes `dist/compat/` with a screenshot per game and an
`index.html` showing them side by side. Local only: commercial games never go
into CI. If CI is added later, it runs only `just test`.

## 6. Debugging

- `OMEGA_TRACE=1` logs unlocks, page and save-type register writes, the save
  type sent at launch, SD reads and writes by sector, and switches between
  kernel mode and game mode.
- Document how to attach gdb to mGBA's GDB stub to step through the kernel.

## 7. Later

1. The in-game menu and real-time saves (RTS).
2. Firmware updates, last or never.

Out of scope: installing games to NOR, scripting new games, recorded inputs,
save states.
