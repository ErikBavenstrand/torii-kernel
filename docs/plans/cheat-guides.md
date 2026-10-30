# Cheat guides and activation

Agreed with the user on 2026-10-29 (the interview's decisions D1-D14). Each cheat gets two optional fields in its `.cht` file: how to activate it, and a guide on what it does and how to use it. Both are shown with care for the 240x160 screen, and editable on the cart.

## The file

```
cheat3_desc = "Warp: Mossdeep City"
cheat3_code = "..."
cheat3_keys = "hold L"
cheat3_guide = "Walk through any door to arrive in Mossdeep City. Let go of L to use doors normally."
```

- `cheatN_keys`: `always`, `hold B1+B2...`, `press B1+B2...`, or `launch`. Buttons are `A B Select Start Up Down Left Right L R`, case-insensitive, joined by `+`. Anything else reads as not set.
- `cheatN_guide`: free text up to 250 characters, as written (never translated). A literal `\n` starts a new paragraph.
- RetroArch and mGBA ignore both keys. Files without them work as before.
- `tools/cheats.py` is not changed (the user may drop it for a hand-curated golden set).

## The activation

- From `cheatN_keys` when set. Otherwise derived from the decoded code:
  - button checks on KEYINPUT (`0x04000130`): an `IF_EQ` means the clear bits are held together; an `IF_NAND` means that button is held, in any combination; `IF_BUTTON` means R+Select (`CHEAT_BUTTON_KEYS`). One combination across the cheat gives **Hold** that combination; different combinations give none (unknown);
  - no button check, and it runs in game: **Always** (other conditions are game state, which needs no buttons);
  - only ROM patches: **Launch**.
- Derived is always Hold; Press only comes from the file or the cart. Unknown shows nothing.
- Once decoded, a line the code can't keep gives way to what the code does: "launch" on a cheat that runs in game, Always on one whose code waits on its own buttons, Hold or Press on one that can't wait on buttons (`cheatfile_activation`, `CHEATFILE_ACT_UNGATED`).
- Shown as a sentence, from `lang/*.ini` (English and Swedish), in a launch section, since the in-game menu shows it too: "Always on", "Hold L+SELECT", "Press SELECT+Up", "Set when the game starts". Button names come from the strings too.

## The Cheats screen (source/screens/cheats.c)

- **Rows:** a short dim tag at each row's right for Hold and Press cheats: the first button ("L", "SEL", "R"), with "+1", "+2" and so on when there are more. None for Always or Launch.
- **Bottom bar:** for the highlighted cheat, its activation sentence left of the key hints, as a greyed cheat's reason shows today (`ui_hints_note` with `beside`). Always-on shows nothing there. A greyed cheat's reason wins over its activation.
- **Guide page:** what is now Details, renamed Guide, opened from the START menu and with SELECT on a cheat:
  - at the top, a tinted box with the activation sentence (or a greyed cheat's reason);
  - then the guide, word-wrapped;
  - then a divider and the existing technical details;
  - all of it scrolling. Without a guide it opens on the details as today, under the activation box.
- **START menu:** two new rows, Buttons and Guide (edit):
  - **Buttons:** a small page with Always, Hold, Press and From the code. Hold and Press capture the combination as Settings' hotkey capture does (hold it for about a second). From the code removes `cheatN_keys`. A cheat with ROM patches shows From the code ticked "At launch", locked. A combination that holds the in-game menu's keys, or Sleep's while Sleep is on, is refused in red in the hint bar ("L+R+START opens the in-game menu."): the hotkey would fire instead. One held inside a hotkey is allowed. B held alone is a combination (Hold B); a tap of B cancels the capture. A cheat that patches the ROM and also runs in game can't wait on buttons (a ROM write can't go under a condition): Hold and Press are greyed, the hint bar says so, and a Hold or Press line in its file is kept but not used.
  - **Guide:** the rename keyboard, with a field that wraps over two lines and scrolls to follow the caret, up to 250 bytes as the file holds them (é and a line break count 2; the counter counts the same), over the standard grid at 16 px a row. Its keys are for prose: `, . ? '` beside the letters, `! : ; - / ( ) &` on the Latin-1 page (not `"`, which the file turns into `'`). A name that isn't a file's has `: ? /` on the Latin-1 page's upper case; a file name's keys stay FAT-safe. An empty guide removes `cheatN_guide`.

## The in-game menu (source/overlay)

- The cheat list shows only the cheats picked at launch: no more unpicked extras up to 64 (each still toggles off and on as today).
- The highlighted row scrolls its name sideways when it's cut; the others end in "...".
- The status line under the list shows the highlighted cheat's activation sentence (or "Always on"), unless it shows a reason the cheat can't change.
- No guide in game, and no SELECT action.

## Look and checks

- It must look good and fit: every new text checked in English and Swedish goldens for cuts and spacing.
- Host tests for parsing, formatting, deriving and editing; screen goldens for every new view; the in-game goldens; `just check`.
