# v0.5 save screens: three directions

`saves-variant-a.png`, `-b.png` and `-c.png`, drawn by `tools/mockup/saves_variants.py` with the v0.4 kit (the menu's fonts, `palette.json`, `ui_theme.c`'s sizes). Each sheet walks the same jobs in dark, then its main screen in light: a game's saves, what a slot can do, backups and restoring one, choosing a slot at launch, and Settings > Saves. The model is the same in all three (see [saves.md](saves.md)): a game has slots, one is **active** and Play loads it, and every save keeps the one before it as a **backup**.

## A: Pages and popups

The Settings page's look: a *Slots* section card, the active slot dotted, last-saved times on the right, *+ New slot* last. A on a slot opens its popup beside the row (Play, Make active, Backups, Rename, Copy to new slot, Export, Delete); Backups is a page of its own. Play with two or more slots is a popup beside the game's row.

- **For:** nothing new to learn or build: section cards, row popups and confirmations exist, so it's the cheapest and the most consistent with Settings and the game popup.
- **Against:** backups are two steps away, and the page is mostly empty with few slots.

## B: List and detail

Slots in a narrow card on the left; the chosen slot on the right, as the list with art shows a game: its name in the heading font, an **Active** tag, when it was saved, and its backups. A plays, START opens the slot's popup, RIGHT moves into its backups (A restores), LEFT back. Play with two or more slots opens this same page as "Play with". Settings > Saves follows the NOR page: storage first (a bar with a legend), then the list.

- **For:** everything about a slot is visible at once, backups included; the launch choice and the manager are one screen, so there is one thing to learn.
- **Against:** a new two-pane layout (a new widget pairing); the right pane is narrow, so backup times are short ("5 h", "1 d"); names past about 12 characters are cut in the list.

## C: Slot tabs and a timeline

One slot at a time. The slots are pills across the top (like the SD page's path pills, with L and R chips at the ends); under them the slot's **current save** as a big row, then its backups as a timeline. L and R switch slots, A on the current save plays, A on a backup restores, START opens the slot's popup. Play with two or more slots is a small card of slot pills over the list (LEFT and RIGHT, A plays). Settings > Saves is rows that open what they count: *Without a game* and *Trash*, where a deleted slot can be brought back (new: today the trash can only be emptied).

- **For:** the most direct for the common job (play, or roll back a slot); backups need no extra page; bringing a slot back from the trash.
- **Against:** with many slots the pills scroll (L/R walks them one at a time); slots aren't seen side by side; the most new UI to build (tab strip, timeline, trash page).

## The START popup (all three)

`saves-popup.png` (`tools/mockup/saves_popup.py`): today's popups are the kernel's own screenshots, the proposals are drawn over the same lists.

- **Today:** the GBA popup has 8 items (Play, Favourite, Clean boot, Boot with addon, Saves, Rename, Game info, Delete), Saves sits among the boot choices, and with art the title no longer fits. On the SD page the file popup is already 9 items with addons on, so **Saves is left out**.
- **Proposal:** the two boot items become one: **Play « addons »**, where LEFT/RIGHT pick With addons or Clean for this start (A plays; the hint bar shows the arrows only on that row). **Saves** comes second and shows the active slot ("Main ›"); A opens the game's save page, which is where A, B and C differ. The GBA popup is 6 items and keeps its title everywhere; the SD popup is 7 (Launch « », Write to NOR, Saves, Add to favourites, Rename, File info, Delete), so Saves is always there. Write to NOR starts the way Launch's « » says.

## To decide

1. Whether the START popup merges the boot items into Play « » as proposed.
2. Which direction (or which parts of each: e.g. A's page with C's trash page, or B's launch-as-the-page).
3. Should Play ask at all with two or more slots, or always play the active slot (choosing only in Saves)?
