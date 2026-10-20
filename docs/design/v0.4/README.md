# v0.4 design

The mockups the v0.4 UI is built from, drawn at the GBA's 240x160 in the menu's own Galmuri fonts. `docs/plans/v0.4-decisions.md` says what was decided and why. The full design canvas, with every round of variants, is <https://claude.ai/artifact/NPcgyfpzpP8LwFE5EYeWqL>.

These are a first target, not a contract: expect to change a lot once the first version runs on a real cart.

- `pages-light.png`, `pages-dark.png`: all 24 screens in each theme.
- `motion.png`: the carousel step, opening an item, a popup, and the theme switch, frame by frame.
- `spec.md`: the numbers behind the mockups (spacing, carousel geometry, popups, keys, timings, hardware mapping).
- `palette.json`, `palette.png`: surfaces and one accent per item, in both themes, snapped to 15-bit colour.
- `icons@4x.png`: 16x16 and 8x8 icons for every item and list marker. The icons and the carousel objects themselves now live in `assets/ui` (`just assets` converts them for the kernel).

## Credits

The Game Boy Advance, Game Boy Color and Game Boy sprites (`assets/ui/sprites/gba.png`, `gbc.png`, `gb.png`) are redrawn at sprite size on the outlines of KyleBing's retro game console icons (<https://github.com/KyleBing/retro-game-console-icons>), licensed CC BY 4.0. The other sprites and the icons are our own.
