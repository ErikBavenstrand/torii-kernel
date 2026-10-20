# Settings page and rename keyboard: mockups for B16 and B18

The mockups T25 and T12 ask for before B16 and B18 build anything: the Settings page in `settings-light.png` and `settings-dark.png`, the rename keyboard in `keyboard-light.png` and `keyboard-dark.png`. They are drawn at 240x160 (shown at 2x) in the menu's own fonts, converted the way `just font` converts them, with `palette.json`'s colours and the sizes in `source/ui/ui_theme.c`. `tools/mockup/settings.py` and `keyboard.py` redraw them (`uv run --with pillow python -I tools/mockup/settings.py`). Each script keeps its numbers in one look table at its top (`SET`, `KB`); the drawing helpers are in `tools/mockup/gfx.py`.

The rows, labels, explanations and values are B6's: `source/settings_model.c` and the `en_set_*` strings in `source/lang.c`. Like every v0.4 mockup, they are a first target: expect to tune them on the cart.

All sizes are px, all times are frames at 60 fps, and y counts from the top of the screen.

## Shared look

These come from `ui_theme.c` (`LOOK`) and the existing mockups (#18 to #20), unchanged:

- Title bar 0-18: the item's 16x16 icon at (5, 1), the heading (Galmuri11 Bold, 16 px line) at x 25 with its line's top at y 1, a right-hand text (Galmuri9, ink2) ending at x 235 with its line's top at y 4.
- Content 20-143. Body text is Galmuri9 on a 12 px line; its capitals fill rows 2-10 of the line.
- Hint area: the line's top at y 147. Key chips are 10 px high (y 148-157), a soft pill with r3 corners, the key in ink2, at least 13 px wide, a word 3 px padded each side; the label 2 px after the chip in ink3; 8 px between items, 2 px between chips of one item; the whole row centred; at most 4 items.
- Cards: r4 corners, a 1 px shadow below in light and dark, no edge. Floating cards (the field editor) add a 1 px edge and a 2 px shadow below (1 px to the right), filled with the float colour (dark: the card mixed 5 % toward ink).
- Selection: a tint pill, 12 px high with r3 corners; the selected row's label turns ink and its value ink2. Unselected labels are ink2 and values ink3.
- Settings' accent is #637B9C (`set` in `palette.json`). The keyboard takes the accent of the page that opened it: the mockups show Game Boy Advance's indigo, as from a GBA list.

## Settings page (B16)

### Layout

One scrolling page of seven section cards in B6's order: Look (4 rows), Clock (4), Games (4), Cart (5, or 3 while LED is off), Hotkeys (4), Library (2), System (1). 24 rows with LED on. No card has more than 6 rows.

| What | Value |
| --- | --- |
| Section label | Its 12 px line sits on the paper above its card: the 8x8 icon (`icon8-look`, `clock`, `games`, `cart`, `keys`, `library`, and `chip` for System) at x 8, line top + 2; the name at x 20; both in the item's ink |
| Card | x 4, width 223 (to x 226), from the label's line top + 12; 2 px padding above the first row and below the last; height 4 + 12 x rows |
| Rows | 12 px pitch; label at x 13; values end at x 218 (exclusive), right-aligned |
| Pill | x 7, width 216, 12 px, r3 |
| Between sections | the card's 1 px shadow, then 2 px of paper, then the next label's line |
| Section block | 12 + 4 + 12 x rows + 3: Look, Clock, Games and Hotkeys 67, Cart 79, Library 43, System 31; the page is 421 px tall |
| View | y 20-143 (124 px); cards clip by the pixel; a label or row that does not fit whole is left out, so no half lines show at the edges |
| Scrollbar | on the paper: a 1 px track at x 231, y 24-141, in edge; a 3 px thumb (x 230-232, rounded ends) in the accent's base, as long as 124/421 of the track, placed by the scroll offset |
| Title right | the selected row's position among the visible rows, "1/24" (ink2) |

Scrolling: the page scrolls so the selected row stays inside the view with one row of room above and below it, and shows a section's label when its first row is selected. Pixel scrolling with B9's rows is fine; a jump of whole rows is fine too. Held UP/DOWN uses the list curve (16 f, then every 5 f, every 3 f after 45 f), stopping at the ends; a fresh press wraps from the last row to the first.

### The hint area

While browsing, the hint area shows the selected row's explanation (`settings_explain`), one line in ink2, centred, changing on the same frame as the selection (no fade). B6's strings are at most 38 characters, the widest 194 px, so they fit the 232 px line. There are no key chips while browsing: A and B work as on every page, and the explanations that need it say "A sets it". While a value is edited, the hint area shows the editing keys instead.

### Values

Plain words: On/Off, the choice's name, "10 min", "14:32", "2026-10-20". Actions (Rescan library) show a 3x5 chevron in the value's colour, 4 px from the value edge. Colours and hotkeys are drawn, as below.

### Screens (`settings-light.png`)

1. **The top, Theme selected.** Scroll 0. The hint area reads "Light paper or dark graphite."
2. **Start on, edited in place.** A on a choice, on/off or number row starts editing: the value shows as « value », the marks in the accent's ink with 3 px between each mark and the value, the value in ink. LEFT/RIGHT step it (wrapping; number rows repeat when held: 16 f, then 5 f, 3 f after 45 f), A keeps it, B restores the value from before A. The value swaps at once, no slide. Hints: LEFT/RIGHT "change", A "done", B "cancel".
3. **Theme previews live.** On the Theme row, the first LEFT/RIGHT switches the whole page to the other theme at once (B19's wipe later). B switches back, A keeps it. The screen is the page in dark while « Dark » is being chosen.
4. **Time: the field card (#20).** A on Time (or Date) opens a floating card beside the row: x 126, width 104, height 60, top at the row's y - 20, clamped to 21..(144 - 60). A 3 px notch on its left edge points at the row's middle; the row keeps its pill. Inside: the 8x8 clock icon at (6, 4) and "Time" at x 17 (line top 2) in ink, a hairline in edge at y 15 from x 4 to width - 4, then the fields in the heading font centred, their line top at y 29: "14 : 32" (24-hour), or "2 : 32 PM" with a third field (12-hour), or "2026-10-20" for Date (year, month, day). The active field sits in a tint box (the text + 3 px each side, 17 px high, r3) in ink, with 5x3 arrows in the accent's ink 2 px above and below the box; the others are ink2, the separators ink3. UP/DOWN change the field (wrapping, month lengths and leap years per B6; held: 16 f, then 6 f, 3 f after 60 f), LEFT/RIGHT move between fields (wrapping), A sets the clock (seconds go to 0), B closes without change. It opens and closes like a popup (B11: 6 f, ease-out quad, 8 px slide; close 4 f). Hints: UP/DOWN "change", LEFT/RIGHT "field", A "set", B "cancel".
5. **LED colour: swatches.** The value is a 12x10 swatch (r2, 1 px edge) and the colour's name, 4 px apart; Off is the card colour with a 1 px slash, both edged in ink3; the white swatch is edged in ink3 too, others in the colour 28 % toward black. Editing (A) shows all eight in value order (Off, Blue, Green, Cyan, Red, Magenta, Yellow, White), 2 px apart (110 px), between « », the chosen one ringed by a 1 px ink outline that touches it. LEFT/RIGHT move the ring and the hint area names the colour in place of "change" ("Magenta"), for anyone who can't tell the colours apart. The colours as drawn: blue #3973E7, green #39B54A, cyan #29BDD6, red #E73939, magenta #D642C6, yellow #EFC621, white #FFFFFF. The SD card light row works the same. LED off hides both rows (B6's `SET_WITH_LED`), which changes the count in the title.
6. **Sleep keys: press the combination.** Hotkey values are key chips (the hint chip style), right-aligned, 2 px apart, the keys high bit first as `settings_keys_list` gives them ("L R START"). A starts capturing: the value shows « Press keys... » until a key goes down, then the held keys as chips in the accent's base with `on` text, filling live. The hint area shows a 40x3 bar (r1, soft track, base fill) that empties over 5 s (300 f), 6 px before "Press keys... let go to set." in ink2. The bar restarts while keys change. Committing: when every key is up, the largest set held for at least 18 f in a row is offered to `settings_set_keys`. Nothing pressed for 5 s, or B pressed alone, cancels and keeps the old keys; B as part of a held set is a key like any other.
7. **Refused.** `settings_set_keys` refused the set (here L+R+START, the Reset keys). The old keys stay and the hint area shows the reason in danger ink for 90 f, then the explanation returns. The reasons: `SET_KEYS_TAKEN` "Already used by Reset keys." (or Sleep keys), `SET_KEYS_COUNT` "Use 2 or 3 keys.", `SET_KEYS_NO_MODIFIER` "Use L, R, START or SELECT too.", `SET_KEYS_WAKE` "START+SELECT wakes a sleeping game.", `SET_KEYS_SOFT_RESET` "Games reset on A+B+START+SELECT." (new `en_*` strings, each under 38 characters).
8. **The bottom: Library and System.** Scroll 297, the largest. Rescan library selected: A runs B3's rescan, its value turning to "Scanning..." (ink2) until it ends, then back to the chevron; the page stays usable.

### Screens (`settings-dark.png`)

1. Dim screen edited in place, « 10 min ».
2. Cart and Hotkeys: the LED colour row (White swatch) selected with its explanation, the hotkey chips below.

### Not in the mockups

- Leaving the page writes once, only when a value changed (B6's staging; `tests/settings.sh`). No "Saved" toast.
- The research's per-section "Reset Look" rows and an Animations row are not in B6's table, so they are not drawn.
- The Date editor is the Time card with three fields.

## Rename keyboard (B18)

### Layout

| What | Value |
| --- | --- |
| Title bar | the opening page's 16x16 icon, "Rename"; no right-hand text |
| Field card | x 4, y 21, 232x18, a page card |
| L and R chips | at the field's ends, x 8 and x 215, y 25: the shoulder keys move the caret, so their chips sit where the caret goes |
| Name | Galmuri9 in ink from x 25, line top y 24 |
| Counter | "14/60" in ink3, ending at x 210 (exclusive); danger ink at the limit |
| Name clip | from x 25 to 6 px before the counter; a name longer than that scrolls to keep the caret in view, and a cut start shows "..." in ink3 |
| Caret | 1x11, the accent's base, at line top + 1; the text after it moves right 2 px so it never covers a glyph; blinks 30 f on, 30 f off, and shows at once after any key |
| Keyboard card | x 4, y 43, 232x98 (to y 140, shadow y 141), a page card |
| Grid | 5 rows x 10 columns; a cell is 21x18; the grid starts at x 15 (11 px margins) and y 47 (4 px padding) |
| Key pill | the cell less 1 px each side: 19x16, r3 |
| Character keys | Galmuri11 Bold, centred in the cell, its line's top at the cell's top; letters in ink, digits and symbols in ink2 |
| Selected key | the tint pill (the list's selection), no animation: it jumps on the frame the key is read |
| Function keys | paper-coloured pills (the page's bg) with Galmuri9 words in ink2, line top + 3; the selected one tint |
| OK | a solid pill in the accent's base with "OK" in its `on` colour; selected, a 1 px ink ring around it |
| Hints | A "type", B "delete", SELECT "Aa", START "OK" |

The grid:

```
1 2 3 4 5 6 7 8 9 0
q w e r t y u i o p        shifted: Q W E R T Y U I O P
a s d f g h j k l -                 A S D F G H J K L -
z x c v b n m . _ '                 Z X C V B N M . _ '
[ shift ][ àé& ][ Space ][⌫][ Cancel ][OK]     columns 2, 2, 2, 1, 2, 1
```

The Latin-1 page replaces the four character rows (the function row stays):

```
à á â ã ä å æ ç è é        shifted: À Á Â Ã Ä Å Æ Ç È É
ê ë ì í î ï ð ñ ò ó                 Ê Ë Ì Í Î Ï Ð Ñ Ò Ó
ô õ ö ø ù ú û ü ý þ                 Ô Õ Ö Ø Ù Ú Û Ü Ý Þ
ß ÿ ! & ( ) + , ; @                 # $ % = [ ] ^ ~ { }
```

That is every Latin-1 letter the menu fonts draw (× and ÷ left out) plus the FAT-safe symbols. The keyboard never offers `\ / : * ? " < > |`, a tab or a control character; a prefilled name that holds one keeps it until the user deletes it, and OK strips it.

### Keys

| Key | Does |
| --- | --- |
| D-pad | moves the selection; a single press wraps on both axes (LEFT at column 0 goes to column 9, UP from the digits goes to the function row); held, it repeats (14 f, then every 6 f) and stops at the edges |
| UP/DOWN through the function row | keeps the column the selection came from, so a zig-zag through the wide keys returns to the same letter column |
| A | types the selected key, or runs a function key; held, it repeats like the D-pad |
| B | deletes the character before the caret; held, it repeats (16 f, then every 4 f) and never cancels |
| L / R | move the caret one character; held, they repeat (16 f, then every 4 f) |
| SELECT | toggles upper case (the shift key); after typing one capital at the start of the name or after a space, the case returns to lower by itself |
| START | OK |

Function keys: shift (an outline arrow and "abc", a filled arrow and "ABC" while upper case is on), àé& (opens the Latin-1 page; the key turns soft while it is open; pressing it again goes back), Space, ⌫ (as B), Cancel, OK.

OK saves the name (trimmed of spaces at the ends) and closes. An empty name, or a file name already in the folder, keeps the keyboard open with a one-line message in danger ink in the hint area for 90 f ("Type a name first.", "That name is taken."). Cancel, or B on an empty field that was never edited, closes; if the name changed, Cancel first asks "Discard changes?" in B11's centred confirmation with Cancel preselected.

### Screens (`keyboard-light.png`)

1. **Prefilled.** The field holds the current display name (B4's cleaned name, or the custom one), the caret at its end, the selection on q. The counter reads 14/60.
2. **Shifted.** SELECT: capitals; the shift key's arrow is filled and reads ABC. Digits and symbols stay as they are.
3. **Latin-1 page.** àé& opened it; the caret was moved back with L to type é inside "Pokémon".
4. **Too long.** At 60 characters the counter turns danger ink and a further key is ignored: the counter blinks (6 f off) and the hint area shows "Names stop at 60 letters." in danger ink for 90 f. The field shows the end of the name, its start cut with "...".

`keyboard-dark.png` is screen 1 in dark.

### Limits

- A game's display name (`/torii/names.txt`): at most 60 characters.
- An SD file's name: the stem only; the extension is locked and shows after the stem in ink3, not counted; at most 100 characters, so the `.sav` pair stays well under FAT's 255.
- One character is one code point: é counts once, though it is two bytes in UTF-8.

## Choices to look at

These differ from or add to the decisions, the spec or the research:

- The browsing hint area holds only the explanation, no key chips.
- Rows and labels cut by the view's edge are hidden, not drawn half.
- LED swatches are drawn as the LED looks, not as the theme's colours.
- The keyboard's L and R chips sit on the field, not in the hint row, so the hint row keeps 4 items.
- The keyboard uses the heading font for its character keys, and the function row has 2-column shift, àé& and Space keys rather than a 3-column Space.
- The Latin-1 page shares the 10-column grid (4x10, symbols on its fourth row) instead of the research's 16-column page, and there is no separate 123 page: digits stay on the main grid.
