# v0.5 save screens: redesign

Mockups for review before the save screens are rebuilt: `saves-dark.png` and `saves-light.png`, drawn by `tools/mockup/saves.py` (`uv run --with pillow python -I tools/mockup/saves.py`) with the v0.4 kit, so every font, colour and size is the menu's own.

## What was wrong with the first build

- **Three places manage slots and each has its own verbs.** The launch picker renames (START) and creates slots, the Start menu's Saves screen has Launch with this slot, Set as current, Duplicate and an Import that overwrites a slot, and New slot offers Copy of the current one, which Duplicate also does.
- **Read-only facts look like buttons.** Settings > Saves lists the totals and the "Orphaned saves" heading as list rows, so the cursor lands on things that do nothing.
- **A slot's actions are a full page**, where every other per-item menu in the kernel is a popup beside its row.
- **History is buried** under a slot's action page, and its name says nothing about what it holds.

## The model

A game has **slots**. One is **active**: Play loads it, and the game saves into it. Every time a slot's save is replaced (the save copy after playing, a restore, an import), the old one becomes a **backup** of that slot, kept as Settings' *Backups kept* says.

There is one place to manage a game's slots (its **Saves** page), one question at launch (which slot, only with two or more), and one page for the card as a whole (Settings > Saves).

## Screens

1. **A game's Saves** (Start menu > Saves). A Settings-style page: a *Slots* section card with the save type on the right, one row per slot (the active one dotted in the console's ink, the last-saved time on the right), and *+ New slot* as the last row. A opens the slot's popup, START plays the slot under the cursor, B goes back.
2. **A slot's popup** (the game popup's look, beside the row): Play, Make active (only on a slot that isn't), Backups (the count on the right), Rename, Copy to new slot, Export .sav, Delete (danger ink, last, with a confirmation).
3. **New slot**: Empty, Copy of the active slot, or From a .sav file. **Importing makes a new slot** and never overwrites one, so it can't lose anything; the user can then make it active and delete the old one.
4. **Backups**: the slot's backups, newest first, with when each was made and how long ago. The hint line explains what they are.
5. **Restore** asks in the centred confirmation, Cancel preselected, and says that the save it replaces is kept as a backup.
6. **Play with** (A on a game with two or more slots): a popup beside the game's row, the active slot first and chosen, A plays, B cancels. No renaming or creating here. Past 8 slots it falls back to a page.
7. **Settings > Saves**: an *On the card* card of facts (a bar, then Saves, Backups and Trash with their sizes) that the cursor never visits, drawn quieter than rows; *Without a game* (saves whose ROM moved or went), each a row whose popup offers Relink to a game or Delete; and *Empty trash* in danger ink. With nothing to fix, the section says so in one quiet line.

## Words

| Was | Now |
| --- | --- |
| current slot | active slot |
| Launch with this slot | Play |
| Set as current | Make active |
| Duplicate | Copy to new slot |
| History, version | Backups, backup |
| Import .sav (into a slot) | New slot > From a .sav file |
| Orphaned saves | Without a game |
| Keep (an orphan) | gone: B is keep |
| History (Settings) | Backups kept |

## Speed

- No card access to open a game's popup: Saves shows for every GBA game, and the game's facts are read once it is picked.
- The save copy, a restore, an import and a save-type change move the old save into the backups by renaming it, never by copying it, so they cost the same for any save size. On the cart, the 3.7 s that a 128 KB save copy spent copying the old save into the history (of 7.4 s in all) goes; the write and its verify stay.

## Open questions

- Should the launch ask at all? The alternative is that A always plays the active slot and choosing happens only in Saves (faster for one player; worse for a shared cart).
- Last-saved times need the clock; with it unset the rows show "Saved" or "Never saved".
