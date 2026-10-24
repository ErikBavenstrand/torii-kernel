# v0.5 UX review: Settings and the save pages

A first-time player's pass (2026-10-24) over Settings, Settings' save page, a game's Saves page, its backups, the slot popup, New slot and the launch's Play with, from the golden screenshots, `just shot` runs and the screens' key handling. The question for every row, label, value, hint and key: would someone who has never seen Torii know what it is and what pressing A does? Owner decisions from the same day are folded in (Export removed, SELECT stays free, Play « default » stays).

The screens after the changes are the goldens: `tests/shots/png/settings.*`, `settings-dark.*`, `saves-orphans.*`, `saves-none.*`, `game-saves.*`, `slot-picker.*`.

## Findings, ranked

### 1. Saves are split across three Settings cards (changed)

**Was:** Auto save sat in Games, the Saves page in Library (as "Saves", between Show all files and the end of the card), and Backups kept in System. A player who wants to know "how are my saves handled?" had to read three cards, and nothing tied the three together. Library is about finding games; System about the kernel itself.

**Why it matters:** these are the three things a player decides about saves (does the copy ask, how far back can I go, where do I tidy up), and they are only found together by luck.

**Alternatives weighed:**

- *A. Leave the rows, rename only.* Cheapest, but keeps the scatter; Backups kept next to Logging still reads as a system knob.
- *B. A Saves card* after Games: Save without asking, Backups kept, Clean up saves ›. One place, and the card's name is the word the player already looks for. Library keeps Rescan, Identify and Show all files; System keeps Logging (Language joins it at v0.9).
- *C. Move the two settings onto the Clean up page* (Settings has one Saves row that opens a page with the figures and the two values). Fewer Settings rows, but values on a second page break Settings' rule that every value is edited in place on one page, and the page's VRAM is already tight (v0.5.1's BG allocation failure).

**Chose B.** It only regroups rows (no stored value moves: the rows keep their settings slots), and it matches how the Look/Clock/Games cards group by topic. Save states stays in Games: it is an in-game feature that changes the reset keys, beside Cheats.

### 2. "Auto save" reads as the game saving by itself (changed)

**Was:** "Auto save: Off", explained "Copies saves to the card, no question." A player knows "auto save" from games, where it means the game saves on its own; here it means the menu's *Save to SD card?* question at power-on is skipped. "No question" didn't say which question.

**Now:** **Save without asking**, explained *No "Save to SD card?" after a game.*, the exact question the player sees. Info's In game page says "(it asks first, unless Save without asking is on)".

### 3. The Settings row "Saves" didn't say what it opens (changed)

**Was:** "Saves ›" opened a page about storage, orphans and the trash. A player looking for a game's save (to restore one, say) went there and found none of their games; the per-game page is in the game's START menu.

**Now:** the row is **Clean up saves ›** and its page is titled **Clean up**: tidying is what the page is for (space used, saves whose game is gone, the trash). The explanation stays "Space, saves without a game, trash."

### 4. Export .sav was a step nobody needs (removed, owner's decision)

Every slot is already a plain `.sav` at `/torii/saves/<rom path>/<slot>.sav`; an export only helps on a PC, where that file is at hand. The slot popup is now 6 items (Play, Make active when it isn't, Backups, Rename, Copy to new slot, Delete) with no scroll. Its code, strings, the `export` case of `tests/game_saves.sh` and the "Exported to" message went. `slotstore_export` stays in `slotstore.c` (another branch owns that file now) with its own unit and `tests/slotstore.sh` cases; it has no caller in the menu and can go with them.

### 5. The detail pane's notes repeated the hint bar (changed)

**Was:** the active slot's note said "Play loads this one." and every other slot's "START: its menu.", a copy of the hint chip below it. Play with's note said "A plays this slot.", the A chip again. The one thing a newcomer can't guess, that **A on another slot plays it and makes it the one Play uses from then on**, was nowhere.

**Now:** active: *The game's Play uses this save.*; other slots: *A plays it and makes it active.*; Play with: *The game saves into this one.* Each tells a consequence, not a key.

### 6. The backups page's words (changed)

- **"Age" showing a date:** the fact was the "ago" text ("5h ago", "yesterday"), which falls back to the date when the clock is behind or the backup is old. "Age 10-23" reads wrong. It is now **Made**, which fits both "Made 5h ago" and "Made 10-23".
- **"#1":** a backup made while the clock was unset was listed as "#1". It is now **Backup 1**.
- **Note:** "A restores it; the save now is kept." ("now"?) is *A restores it; the newer one is kept.*
- **Hint:** B said "slots" where every other page says "back"; it says **back**.

### 7. "Relink" is jargon (changed)

**Was:** a save without a game offered "Relink to a game", the picker was titled "Relink to", the question "Relink to this game?" with a Relink button, while the progress said "Moving the saves".

**Now:** **Move to a game**, **Move to**, *Move the saves to this game?* and **Move**, matching what happens and the progress line. The orphan's hint said "open" for A, which opens a popup, not a page: it says **menu**, as the game lists' START chips do.

### 8. The storage line's units (changed)

**Was:** "Saves 81 · Backups 32 · Trash 16 KB": the first two figures had no unit (counts? KB?), and saves past 1 MB would have shown thousands of KB.

**Now:** "Saves 81 KB · Backups 32 KB", each in KB or MB as the game lists size files. The trash's size is on the Empty trash row (it already was); all three with units don't fit the card's 216 px once they reach MB ("Saves 6.3 MB · Backups 4.5 MB · Trash 512 KB" is 228 px).

### 9. Empty trash with nothing in it (changed)

**Was:** a dimmed row whose A opened a full dialog, "The trash is empty.", that had to be closed.

**Now:** the row stays dimmed with "0 KB" beside it, the hint bar offers only B there, and A does nothing. The row and its 0 KB already say it all; the dialog was an extra press.

### 10. Settings' hint bar shows the explanation, not the keys (kept, decided)

Every other page shows key chips; Settings shows the selected row's explanation. Kept: Settings' keys are the menu's usual ones (A edits, B back) and are shown once editing starts («» change, A done, B cancel), while the explanation is the only place a row like *Mode B* or *Clock for games* is explained. Chips plus the explanation don't fit 232 px. Changed the explanations that didn't explain: Fast patching is *Addons start faster for known games.*; Backups kept is *Older saves kept, to go back to.*

### Checked and fine

- Read-only facts never take the cursor: the save pages' facts are labels in the pane, Clean up's figures a line and a bar; Settings' action rows end in ›, value rows show their value.
- A, B and START: A plays (slot), restores (backup), opens (New slot, an orphan), edits (Settings); START opens a slot's popup; B is back everywhere, from the popup to the page to the list.
- Destructive steps ask with Cancel first (Delete a slot, Restore, Delete orphan saves, Empty trash, Move); Make active and Rename don't need to.
- The last slot's Delete explains itself ("A game keeps at least one slot."); a slot without backups says so on RIGHT.
- Depth: a game's backups are 3 presses from its list (START, Saves, RIGHT) and the restore one more.

## Proposed, for the owner

1. **Settings' Clean up page could list every game with saves** (each row opening that game's Saves page), so Settings becomes a way into saves too, not only a cleanup. A player who thinks "saves live in Settings" (as on most consoles) finds nothing of their games there today. The page already walks `/torii/saves` for its sizes; the list needs the walk to keep each game's path (EWRAM or `pReadCache`) and a third view. Medium work; worth it if cart use shows players looking there.
2. **A on a slot plays it at once.** A newcomer who presses A to "pick" a slot starts the game. Direction B chose this on purpose, and the hint and note now say it; the alternative is A opens the popup (Play first) and START is gone, one press more for the common job. Left as is.
3. **Play with's title shows the file name** ("Hello.gba") where the game lists show the display name; the Saves page shows the display name. The launch path only has the file name (`actions.c`); passing the list's display name through is small but touches the launch path another branch is changing.
4. **The storage bar's three shades have no legend.** The trash's share is the darkest; a legend (three swatches) would need its own tiles on plane 3. A single bar of saves against backups would be simpler.
5. **New slot's pane heading peeks above its popup** (the popup sits beside the third row, the pane's heading above it). Harmless; blanking the heading while the popup is up would hide it.
6. **The kept backup's note** ("From a type change: kept for good.") assumes the player knows save types. "Kept for good: the save from before its type changed." doesn't fit two lines of 112 px; left.
7. **Section order:** Saves sits after Games. Putting it after Library (both about the card's files) is the other natural place; after Games keeps it near Save states and the boot mode, which players read together.

## Tests

`tests/nav.sh`'s Settings rows follow the new order (routes by name keep working); the goldens that walk Settings by L/R (`settings`, `settings-dark`, `settings-armed`, `saves-none`, `saves-orphans`) step through one more card; `saves-orphans`' Settings shot is `settings.png` (was `library.png`); `saves-none` no longer has the message shot; `tests/game_saves.sh` lost its export case. Host tests check the Saves card's rows and the new section order.
