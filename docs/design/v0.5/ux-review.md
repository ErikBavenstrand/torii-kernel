# v0.5 UX review: navigation

A fresh-eyes pass over the whole menu (2026-10-24), from the golden screenshots and the screens' key handling, after the save screens' rebuild. It looks for wasted steps in common jobs, inconsistent keys and words, dead ends and missing feedback. The changes are small on purpose: the biggest win is where a list opens, and the rest is wording.

## What was found

**Common jobs**

- **Playing the same game again took a scroll.** Every game list opened on its first row: after playing a game, the cart reboots facing that console (Start on: Last place), and A opened its list at the top. With 300 GBA games, the game just played was a long scroll away. Recent has it on top, but only if you go there first.
- **Leaving a list lost your place.** B, a look at another item, then A again put the cursor back on row 0. The SD card page already kept its folder and each folder's cursor; the game lists, NOR, Settings and Info didn't.
- **Recent can't be tidied.** A game played once by mistake (or a test ROM) stays on Recent until 32 newer ones push it off. The only way off was Delete, which deletes the ROM.
- **Favouriting takes five presses** (START, DOWN, DOWN, A, B), as Saves now comes second. SELECT is free in the lists, but the v0.4 decisions keep it free on purpose, so it's left to the owner (see below).

**Words that disagree**

- The NOR page said **Launch** (its hint and popup) where every other page says **Play**.
- Settings' System card said **History**, explained as "Versions kept of each save slot", though the save screens' word for those is **backups** (`docs/design/v0.5/saves.md`, Words). Its Saves row still said "orphaned saves", which the same table renamed **without a game**.
- The save pages' hint said **START options** (and "START: its options."), where every other page says **START menu**.
- GBA boot mode's explanation named "Launch".

**Checked and fine**

- B is back everywhere (SD: up a folder first); A opens or plays; START opens the row's popup on every list; popups and confirmations behave the same everywhere, and destructive actions ask with Cancel preselected.
- The step hint («» boot) only shows while the popup's cursor is on Play.
- After a delete the cursor stays on the row, now the next game. After a rename it follows the game.
- Empty states have text (No games here, Nothing here, No games on NOR, Every save has its game), and empty Recent and Favourites leave home.

## What changed

1. **Lists open where you are likely to want them** (`source/screens/nav.c`, `games.c`). The first time in a session a console's list or Favourites opens, the cursor is on the game Recent played last that the list holds, so after a play session, A, A plays it again. After that, every page opens on the row it was left on (nav keeps a row per item for the session: 40 bytes of EWRAM). Recent needed nothing: its newest game is already first. Pages get `NAV_ROW_FIRST` for a first open; NOR, SD card, Settings and Info treat it as their first row.
2. **Remove on Recent's game popup**, above Delete (`games.c`, `userdata_recent_forget`): it takes the game off Recent and nothing else (the file, its save and its favourite stay), with no question, since nothing is lost and the next play puts it back. The row goes and the cursor stays on the row, now the next game. "Remove from Recent" didn't fit the popup's width.
3. **One word per thing:** NOR's Launch is Play (it reuses the games' strings, so two strings went); History is **Backups kept** ("How many backups each slot keeps."); the Saves row explains "Space, saves without a game, trash."; the save pages say START **menu**; GBA boot mode says "How A and Play start GBA games."

Tests: `tests/shots/games-last.opts` shows a list opening on the last-played game, the kept row, and Remove. The UI walk now opens GBA on Hello (the shots card's Recent). Scenarios and routes that count rows from the top start with the tests' hook (`NAV_TEST_HOME`), which from then on opens every page on its first row (`tests/nav.sh`'s routes already did).

## What was left, and why

- **A quick favourite key (SELECT in the lists).** The highest-value change left: favouriting is five presses. The v0.4 decisions keep SELECT free ("SELECT is kept free", T8), so it's the owner's call. Building it is small (`games.c` page_event, a hint chip, and a star refresh `ui_list_refresh` already does).
- **Play « default ».** "default" says nothing about what will happen (with addons or clean, from Settings or the game's own boot mode). Showing the resolved mode needs the game's `.mde`, a card read the popup was made card-free to avoid. The games page and the SD page also offer the stepper under different rules (games: every GBA game gets clean; SD: only while an addon is on, and GB/GBC/NES get clean too). Unifying them touches launch semantics, which another engineer is changing now.
- **Game info's Played** is blank for a game off Recent or played while the clock was unset. "never" would be wrong for a game that fell off Recent's 32; left blank.
- **Backups' "Age"** shows a date when the clock is behind the backup's time. It reads oddly but only with a wrong clock.
- **Empty trash with nothing in it** opens a "The trash is empty." message. Harmless feedback; a quiet, inert row would cost the save screens' layout.
- **Deep SD folders** take one B per level to get home. No free key that wouldn't clash (L+A hard reset, LEFT/RIGHT paging).
- **Settings' hint bar** shows the row's explanation instead of key chips (A edit, B back). The keys are the same as everywhere else, and the explanation is the more useful line.
- Save and launch internals (save copy, slot store, scan, names, NOR writes) were out of scope.

## Needs a real cart

Nothing here touches the cart's hardware paths. Worth a look on the cart: that opening a big console list on a game near its end still feels instant (one index lookup per Recent entry, at most 32), and that the remembered rows feel right after a play session and a reboot (they last a session; the first open after a power-on uses Recent).
