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
- **Opening it:** A opens the folder: the list shows its cheats, the title's
  subtitle is the folder's name, and B goes back to the folder's row. Its
  cheats show without the folder's prefix: a name that starts with the
  folder's name followed by a separator (": ", " - ") shows from past them
  ("Master Ball ×99"); a name that doesn't, or would be left empty, shows
  whole.
- **Any number on:** inside a folder each cheat is picked on its own, as at
  the top. Only a choice group keeps one at a time.
- **Choice groups nest:** inside a folder its runs of variants fold into
  choice rows, as at the top, named without the folder's prefix
  ("Item ×99"). A opens one; B goes back to the folder, then to the top.
  A run of variants never crosses a folder's edge (`cheatfile_next` checks
  that the cheat before is in the same folder), so a group is always wholly
  inside one folder or wholly outside.
- **On a folder row:** SELECT does nothing, START's menu has only the
  list's items, and the hint bar shows "Open", as on a choice row.
  "+ Add code" shows at the top only.
- **Only picked** lists every pick flat, with its whole name, leaving any
  open folder.
- The in-game menu doesn't change: it lists the picks only. A possible
  follow-up is to show a pick's folder there, or to group the in-game list
  by folder; not done here.

## Larger files

A larger file is read in windows (`cheatstore.h`, `cheatindex.h`), and each
window is parsed alone. A folder split by a window's edge shows as a folder
row in each window, each with its own part of the cheats, as a split choice
group does. Inside an open folder the list doesn't step to the next window
at its ends: the window steps happen at the top only. Folder names are
looked up per window, so the 127-name cap counts a window's names, not the
file's.
