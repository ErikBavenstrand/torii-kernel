# Cheat folders

Headings for a cheat file (owner, 2026-10-31). A curated file such as
Pokémon Emerald's holds 60 to 100 cheats plus large choice groups; a flat list
of them, even with the choices folded (`cheat-variants-run-once.md`), is too
long to scan. The file names a heading per cheat, and the Cheats screen
folds each heading into one row.

## The key

- `cheatN_folder = "Items"`: the heading cheat N shows under. RetroArch and
  mGBA ignore it, as they ignore `cheatN_keys` and `cheatN_guide`, so a
  file keeps its prefix in each name ("Items: Master Ball ×99") to stay
  readable in their flat lists.
- Its value is 1 to 24 bytes (`CHEATFILE_FOLDER_MAX`) of whole UTF-8
  characters with no control character. Any other value (empty, too long,
  a character cut short, a control character) leaves the cheat in no
  folder; the last line of a cheat counts, as for every key.
- Set in the file only. The cart never writes it; an edit keeps it (its
  line is one of the cheat's, renumbered with it by a delete). A code added
  on the cart has none and shows at the top.
- `tools/cheats.py` keeps it (`KEPT_KEYS`). A merge takes the database's
  folder when the database gives one, else the card's copy's.

## Storage

The cheat table (`struct cheatstore`, in pReadCache) had about 2 KB left,
not the 4 KB a span per cheat would take. So a text's folder names are kept
once each, in `struct cheatfile.folders` (127 at most, `CHEATFILE_FOLDERS`,
each a packed offset and length into the text), and a cheat holds its
entry + 1 in 7 bits beside `patches` (which needs 9). A text with more than
127 names leaves the cheats of the rest in no folder. Two cheats are in the
same folder when their entries are equal.

## The screen

- **What is a folder:** a run of adjacent cheats with the same folder
  (`cheatfile_folder`). Known from the parse, so folders fold at once, not
  as decoding reaches them. One cheat alone in its folder is a folder too:
  the heading is the file's choice.
- **The row:** at the top, a folder shows as one row with the folder mark,
  named by the folder. Its right-hand detail is how many of its cheats are
  on ("2 on", the title's own words), else how many it holds ("3 cheats").
- **Opening it:** A opens the folder: the list shows its cheats, the title
  is the folder's name (see "The title" below), and B goes back to the
  folder's row. Its cheats show without the folder's prefix: a name that
  starts with the folder's name followed by a separator (": ", " - ")
  shows from past them ("Master Ball ×99"); a name that doesn't, or would
  be left empty, shows whole.
- **Any number on:** inside a folder each cheat is picked on its own, as at
  the top. Only a choice group keeps one at a time.
- **A folder that is one choice group** (every cheat in it one run of
  variants, "Starter: Treecko", "Starter: Torchic"...) shows as a choice row
  does once decoded: its pick at the right ("✓ Torchic"), else "3 choices".
  A opens its choices straight away, and B (or a pick) goes back to the top:
  a folder holding one row would be a step for nothing.
- **Choice groups nest:** inside a folder its runs of variants fold into
  choice rows, as at the top, named without the folder's prefix
  ("Item ×99"). A opens one; B goes back to the folder, then to the top.
  A run of variants never crosses a folder's edge (`cheatfile_next` checks
  that the cheat before is in the same folder), so a group is always wholly
  inside one folder or wholly outside.
- **A folder's state:** "2 on" has a tick before it, as a picked cheat
  has, so what is on shows from the top.
- **On a folder row:** SELECT does nothing, START's menu has only the
  list's items, and the hint bar shows "Open", as on a choice row.
  "+ Add code" shows at the top only.
- **Only picked** lists every pick flat, with its whole name, leaving any
  open folder.
- The in-game menu doesn't change: it lists the picks only. A possible
  follow-up is to show a pick's folder there, or to group the in-game list
  by folder; not done here.

## The title (UX review, 2026-10-31)

The title bar gives the heading and its faint after-text 151 px together.
With the game's name as the heading, a long one ("Pokémon Emerald")
left room for "Items" but cut "Legendaries" or a choice group's name off,
so a level down you couldn't see where you were. The cheat screens now
follow Info's pages: the heading is where you are, the faint after-text the
place one up.

| View | Heading | Faint |
| --- | --- | --- |
| The top | the game | none |
| A folder | its name | the game |
| A choice group | its name ("Pick one" if none) | its folder, else the game (also for a folder opened straight to it) |
| Picked | Picked | the game |
| Guide, Buttons | the page | the list's heading |

Dialogs and START's menu keep the game's name.

## Arrows, the folder icon and scrolling text (2026-10-31)

- **The arrow:** a row that opens a list or page has Settings' action
  chevron (3x5, now the theme's `chevron_*`) at its right, its detail left
  of it (`ui_item.more`): a cheat folder or choice row, an SD card folder,
  a folder in Game saves' file picker, a game in Game saves, and Info's
  rows. Rows whose A plays, picks, toggles or opens a menu have none.
- **The folder icon:** inside a folder (and a choice group in one) the
  title's 16x16 icon is a folder (`icons16/folder.png`, in the torii
  icon's palette bank) in place of the game's.
- **Scrolling:** a list's selected row already scrolled its cut name. Now a
  title's heading and after-text, a popup's title, and a label marked
  `UI_LABEL_MARQUEE` (the Buttons page's cheat name, a save page's heading,
  the Saves screen's path) scroll the same way when cut: after the theme's
  `marquee_delay` they move to their end, rest, and start over. Held still
  under a popup or while sliding in. Not done: a confirmation's centred
  title (it wraps), and the hint bar's note.

## Choices close on a pick

Picking one of a choice group's choices goes back to the group's row, its
new pick at its right, as a menu closes on its item: one press less, and
the pick shows where it will be seen. Dropping the picked one (A on it)
stays, so another can be picked. See `cheat-variants-run-once.md`.

## Larger files

A larger file is read in windows (`cheatstore.h`, `cheatindex.h`), and each
window is parsed alone. A folder split by a window's edge shows as a folder
row in each window, each with its own part of the cheats, as a split choice
group does. Inside an open folder the list doesn't step to the next window
at its ends: the window steps happen at the top only. Folder names are
looked up per window, so the 127-name cap counts a window's names, not the
file's.
