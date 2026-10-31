# Cheat choices and Run once

Two follow-ups to `cheat-guides.md` (owner, 2026-10-30): picking one of a
cheat's variants from a single row, and running a cheat once from the in-game
menu. A live value beside each in-game cheat was dropped as a maybe (roadmap).

## Choices (the Cheats screen)

Cheat files list variants one after another: "Wild Pokémon: Bulbasaur",
"Wild Pokémon: Ivysaur", ... each writing the same addresses with other values.
Picking two of them makes no sense (the last write wins), and a list of 400
buries everything else.

- **What is a choice:** a run of adjacent cheats, each decoded OK, whose
  programs have the same shape: the same ops, addresses, widths, counts and
  conditions, with only the written values different
  (`cheatfile_cheat.variant`: same shape as the cheat before). Two or more
  make a group. A larger file's windows split a group at their edge.
- **The row:** a group shows as one row in the full list, named by what its
  members' names share up to a word, without trailing separators
  ("Wild Pokémon"); with less than 3 characters shared, by its first member's
  name. Its right-hand detail is the picked member's own part ("Pikachu"),
  else the count ("400 choices"). Ticked when a member is picked.
- **Opening it:** A opens the group: the list shows only its members, each by
  its own part of the name, the title is the group's name, and B
  goes back to the group's row. The members act as any cheat: A, SELECT,
  START.
- **One at a time:** picking a member drops the group's picked member. If the
  new one doesn't fit the budget, the old pick stays.
- **A pick closes it** (2026-10-31): picking a member goes back to the
  group's row, its new pick at its right; dropping one stays. The title is
  the group's name, the game's (or its folder's, `cheat-folders.md`) faint
  after it.
- **On a group row:** SELECT does nothing, and START's menu has only the
  list's items (All off, Only picked). The hint bar shows "Open".
- **Only picked** lists the picks one by one, as before: no groups.
- Groups show once decoding has reached them: the list folds them in as the
  background decode ends, the cursor kept on its cheat or its group. A pick,
  and an opened run after an edit, decode the run to its end first.
- The file and the in-game menu don't change: a group is the screen's view.

## Run once (in game)

A cheat that gives something ("99 Rare Candy", "Heal the party") is wanted
once, not every frame.

- In the in-game Cheats list, on a cheat that is off and can run once,
  SELECT queues it to run once, and the footer says "Runs once as the game
  resumes"; SELECT again (or A) cancels it. On such a row the footer says
  "SELECT: run it once" in place of the activation. One cheat at a time: a
  second SELECT replaces the first.
- **Can run once:** a cheat in the interpreter's table whose own code waits
  on no buttons (derived Always) and that A could turn on now (no ROM
  patches, not past the toggles, not too slow beside the cheats on). A Hold
  or Press line's gate is skipped: it runs once whatever is held.
- **How:** on closing, the overlay writes the table cheat's number + 1 into
  the byte after CheatONOFF (0x3007FE1, `INGAME_CHEAT_ONCE`). The
  interpreter's next run reads and clears it, runs that cheat even though
  its toggle is off, and steps past its gate's ops (the header's bits 13-14:
  0, 1 for Hold, 2 for Press). The hook tests CheatONOFF's low byte only.
  A game on NOR written before this ignores the byte: Run once does nothing.
- Save state clears a request the game hasn't taken yet, so no state
  carries one.
