### Torii v0.4 UX: D-pad keyboard, settings, popups and lists, responsiveness

> **Answer:** A 10-column QWERTY grid (not a wheel) with wrap on both axes, L/R as cursor, SELECT as shift, B as backspace and START as OK suits 240x160. Settings, popups and the held-key curve below are ticket-ready numbers. Most of them come from published thresholds plus design inference, because console guidelines say little about D-pad specifics.
> **Confidence:** 🟡 medium. Motion and latency thresholds have owning sources. The keyboard layout comparison and the repeat curve are 🔴 inference from them. Nintendo, Sony and Valve guideline pages were not reachable through search.

Method note: standard level, done by one agent (no fan-out) with 12 searches and fetches. Searches for Switch/3DS/DSi keyboard mappings, TWiLight Menu++ and RetroArch OSK internals returned nothing usable. Claims about those are marked 🔴 "from memory, unverified" or left as open questions.

## Q1. On-screen keyboard for rename (B18)

**Findings**
- 🟢 Console keyboards are grids moved by D-pad or stick. The NES-era grid stayed ubiquitous. The Xbox 360 defaulted to ABC with QWERTY added later, and QWERTY is now the default almost everywhere [1].
- 🟡 Layout matters less than familiarity: on a touch/pointer study, QWERTY was fastest (median about 11.4 WPM), an ISPR layout about 8.0 and a radial layout slowest (about 5.1) [2]. This is indirect for a D-pad.
- 🟢 Microsoft's single-stick hunt-and-peck QWERTY measured 5.8 WPM, and a dual-stick split 6.4 [1]. A 9x9 two-stick grid beat on-screen QWERTY by about 2 WPM only after about 20 practice sessions [13]. EdgeWrite, date-stamp and selection keyboards on a joystick: 6.4, 4.4 and 6.2 WPM [13]. Expect roughly 5-8 WPM for a D-pad grid.
- 🟢 Wrap-around: the ABC grid wraps left to right, and the author notes that this helps only people who learned the layout [1]. Wrap is cheap to build, but it must never hide the keys' position.
- 🟢 Big Picture's alphabetical keyboard groups letters in clusters of four on the face buttons. It needs a large graphic and practice [1]. Not usable on the GBA's four-button layout without a stick.
- 🟡 Steam Deck's keyboard has shift, with shift+left/right acting as cursor keys (2023 beta) [10]. Shift and cursor on the shoulders and the extra buttons is therefore the established pattern.
- 🟢 libnds' default DS keyboard has four states (Lower, Upper, Numeric, Reduced), and only Lower and Upper were implemented [11]. The DS/DSi keyboard is touch-based, so it is no model for D-pad travel.
- 🔴 Switch system keyboard (from memory, unverified): B backspace, + OK, shoulders move cursor, ZL/ZR shift. This agrees with the pattern above, but I could not confirm it.
- 🔴 Wheel and "date-picker" columns lose for free text. Inference: with wrap, a 26-letter column needs about 6.5 UP/DOWN presses per character on average, plus one LEFT/RIGHT, so about 7.5 presses per character. A 10x4 QWERTY grid with horizontal wrap averages about 2.5 horizontal plus 0.9 vertical plus A, so about 4.4 presses per character. Columns win only for fixed-width fields such as Time and Date, which B6 already treats that way.
- 🟢 FAT long file names forbid `\ / : * ? " < > |`, and in practice names should not start or end with a space or end with a dot (Microsoft naming rules, from knowledge, not re-fetched). The keyboard should simply not offer those keys. Torii's FatFs uses UTF-8 LFN (project CLAUDE.md), so a name up to 255 UTF-16 units is storable.

**Open questions**
- ❔ Real Switch/3DS/Steam Deck button maps from Nintendo and Valve docs.
- ❔ Measured D-pad-grid WPM: no controlled study was found, only the stick figures above.

### Concrete keyboard layout (240x160, Galmuri9, 9 px glyphs) — 🔴 design inference

Screen: title bar 0-18, hints at y 147, content 20-143 (123 px). Use a centred card, 232 px wide.

```
y 22-36   text field: 12 px row, Galmuri9, caret, scrolls horizontally; counter "23/60" at the right in ink3
y 40-110  5 rows x 14 px (pill 12 px, r3), 10 columns x 21 px = 210 px, 11 px side margins
          1 2 3 4 5 6 7 8 9 0
          q w e r t y u i o p
          a s d f g h j k l -
          z x c v b n m . _ '
          [ abc/ABC ] [ 123 #+ ] [ àé ] [ ____ space ____ ] [ OK ]
y 147     hints: A type  B delete  L/R cursor  SELECT shift  START OK
```

- Wrap: LEFT at column 0 goes to column 9 of the same row, UP from row 1 goes to the function row, and so on (circular on both axes). Held repeat stops at edges instead of wrapping, so a held key does not spin (see Q3). A single press wraps.
- Column memory: moving UP/DOWN through the wide Space and OK keys returns to the origin column. Keep a "last grid column" so a zig-zag does not drift.
- Layers: lower, upper (SELECT cycles abc, then ABC, then back; one-shot capital: after the first letter of an empty field or a word start, go to upper automatically and return to lower after one letter). The 123 key opens digits and symbols `! # $ % & ( ) + , ; = @ [ ] ^ ~ { }`. The àé key opens the Latin-1 page: 16 columns x 2 rows per case, 14 px pitch (224 px), letters À-Ö Ø-Þ ß à-ö ø-ÿ (skips × and ÷), plus Ñ ç Ç Ý. The case follows shift. Matches the menu fonts' Latin-1 coverage.
- Keys: A types the key, B backspace (held repeats), L/R move the text cursor (held repeats), SELECT cycles the layer, START = OK, UP/DOWN/LEFT/RIGHT move the highlight. A on a space key types space. Highlight = tinted pill, the same as list rows (so no new look to maintain).
- Cancel: B at position 0 of an unchanged or empty field leaves, but only on a fresh press, never a repeat (otherwise holding B to erase everything would then cancel). If text was edited, B at position 0 does nothing special. Add an explicit Cancel pill at the end of the function row (shown as the row `[Cancel][OK]` on the right) with a "Discard changes?" confirm (Cancel preselected) only when text changed.
- Prefill with the current display name (the cleaned name from B4) and put the caret at the end. A "Reset" action (L+R held, or a pill on the àé page) restores the automatically cleaned name.
- Max length: 60 characters for a display name (the list shows about 28 px of name before truncation, so the long tail is cosmetic); for file rename lock the extension, edit the stem, cap at 100 so the `.sav` pair stays well under 255. Show the counter in ink3, danger ink at the limit; typing beyond the limit blinks the counter for 6 f and is ignored.
- Reject empty names, the forbidden FAT characters (keys are not offered; pasted/prefilled ones are stripped), and a duplicate within the same folder (show a one-line error where the counter sits, keep the field open).

## Q2. Settings page UX (B16)

**Findings**
- 🟢 Microsoft's gamepad guidance: map unused buttons to jump actions to speed up navigation (X jumps to the bottom of a page in its example), and a control that needs engagement takes A to engage and B to release [8]. This matches Torii's A edits « value », B cancels.
- 🟡 Apple HIG: prefer quick, precise animations, avoid motion on frequent interactions, make motion optional [5]. Applies to the in-place value change (no tween: swap the value immediately) and to the theme preview.
- 🟡 Hold-to-confirm suits irreversible, high-damage actions, while confirm dialogs on frequent actions teach users to click through. Friction should scale with the damage [9]. Undo beats confirmation where the action can be reversed [9].
- 🔴 In-place edit with LEFT/RIGHT is the Switch/DSi pattern for choice values (from memory). Keep a 1 px nudge: the value text slides 2 px in the direction of change for 3 f at most, or none.
- 🔴 Hotkey capture patterns (from memory of RetroArch binds and game controls menus): "press the combination", commit when all keys are released, timeout cancel. Not confirmed in sources found.

### Concrete recommendations (see the ticket section)
- Explanation line: the selected row's `en_*` string shows in the hint area, one line of at most 38 characters at Galmuri9 (check against `font.h` widths), changing instantly (no fade).
- Plain values: On/Off, never icons. Choices as words. Numbers with units ("30 s"). Show « » only while editing.
- Hotkey capture flow: A opens capture, the chip row shows "Press keys…" with a 5 s countdown bar (hold progress). While keys are down, chips fill live. When all are released (after at least 18 f of a stable set so near-simultaneous presses merge), commit the largest set held. Timeout or START+SELECT is not usable (they may be part of the combination), so cancel only by timeout (no key pressed for 5 s) or by B pressed alone (a single B press of one key is rejected as too short anyway: require at least 2 keys, apart from named exceptions). Rule: minimum 2 keys, maximum 4, must include a modifier-like key (L, R, START or SELECT) so a hotkey cannot be triggered by game play with a single button.
- Conflicts: on commit compare with the other hotkeys. Reject if equal, or if one is a subset of the other (the smaller fires first). Keep the old value and show one line "Already used by Sleep" in danger ink for 90 f.
- Swatches: 12x10 px chips with a 1 px edge, the selected one gets the tinted pill. Show the name in the hint line ("Pink") for people who do not distinguish colours.
- Live theme preview: switch at the first LEFT/RIGHT, restore on B, keep on A. A plain switch until B19's wipe.
- When to write: once on leaving the page (and on game launch if the page is somehow skipped), only if a value changed; one NOR write per visit as `tests/settings.sh` requires. Show no "Saved" toast; the Info or hint line may show "Saved" for 60 f after the write because NOR writes take visible time.
- Restore defaults: per section as an action row at the end of each card ("Reset Look"), with a centred confirmation, Cancel preselected. Avoid a global reset on the main page. Do not reset Library data or favourites.

## Q3. Popups, confirmations, letter jump, paging, held-key curves (B11, B13)

**Findings**
- 🟡 Destructive actions: confirmation friction should be proportional to damage [9]. Hold-to-confirm is a recognised pattern (macOS dock hold, iOS slide-to-power-off) [9], but it can be undermined by habit [9].
- 🟢 Desktop auto-repeat defaults for comparison: Linux kbdrate default 250 ms delay and 10.9 cps, the X server default "500 30" (500 ms delay, 30 repeats/s); Windows' speed range is 2-30 cps and its delay range 250 ms-1 s [7].
- 🔴 No console document publishes D-pad repeat values. Microsoft's guidance rests on jump buttons rather than a repeat curve [8]. Everything below is design inference, kept in the theme table so it can be tuned (project memory: look values stay cheap to change).
- 🟡 Keep the popup short: B11's spec of 6 frames open and 4 close is 100 ms and 67 ms, inside the 100-200 ms band Carbon gives for small components [4] and under Material's 195-225 ms for leaving/entering elements [3].

### Concrete recommendations
- Placement: the card sits beside the selected row with a notch on it, and the row stays tinted (as B11). Prefer the side with more room, flip to the left or right of the row, clamp to 20-143. Row height 14 px, at most 7 rows (7x14+8 = 106 px).
- Choice order: the primary action first and preselected (Play). Destructive last, in danger ink, separated by a hairline (Delete). Never first.
- Confirmation: centred card, page dimmed by BLDY, text of at most 2 lines, two pills with Cancel on the left and preselected (solid), the destructive pill on the right in danger ink (outlined/tinted, not solid until selected). RIGHT or LEFT toggles. A confirms the selected one, B = Cancel, START does nothing.
- Hold A: only for Format all and any wipe of NOR (irreversible, many items). 30 f hold with a fill sweep across the pill, and a release before the end cancels. Plain Delete ROM keeps the one-press confirm because "the save stays" and the cost is smaller. 🔴 This is a judgment call.
- Letter card: 32x32 box, centred, Galmuri11b letter, shown instantly on LEFT/RIGHT and kept for 20 f (333 ms) after the last press, closing in 4 f. LEFT/RIGHT jump to the previous/next letter that has entries (skip empty ones; digits and symbols under "#"; wrap A to Z). Held LEFT/RIGHT repeats every 10 f after a 20 f delay (the card just updates). A-Z lists only, per B13.
- Paging: L/R move by rows-1 (7 rows) so one row overlaps, clamp at the ends (no wrap). The selected row keeps its screen position when possible. UP/DOWN single presses wrap at the ends, held repeat stops at the ends (resumes wrapping after releasing and pressing again).
- Held-key curve for lists and the keyboard (frames at 60 fps) 🔴 design inference:

| Context | First repeat after | Then every | Faster after | Notes |
| --- | --- | --- | --- | --- |
| List UP/DOWN | 16 f (267 ms) | 5 f (12 Hz) | 45 f: 3 f (20 Hz); 120 f: 2 f (30 Hz) | stop at ends |
| Letter jump LEFT/RIGHT | 20 f | 10 f | none | card updates |
| L/R paging | 20 f | 8 f | none | |
| Keyboard grid arrows | 14 f | 6 f | none | precision matters |
| Keyboard B (delete) and L/R cursor | 16 f | 4 f | none | |
| Settings value LEFT/RIGHT | 16 f | 5 f | 45 f: 3 f | number rows only |
| Time and Date fields | 16 f | 6 f | 60 f: 3 f | wrap, month lengths |

  The 250-500 ms delay range comes from desktop defaults [7], with the low end chosen for responsiveness and clear separation from a deliberate tap (a tap lasts about 80-120 ms, so 267 ms leaves room). Speeds above 30 Hz are pointless at 60 fps on 8 rows.

## Q4. Perceived responsiveness

**Findings**
- 🟢 Nielsen: 0.1 s is about the limit for the user to feel the system reacts instantly and directly manipulates the objects. 1 s keeps flow of thought [6]. At 60 fps this is 6 frames.
- 🟡 Gaming latency: significant degradation for target tasks was found only past about 100 ms (Claypool and Finkel); expert gamers perceive 15 ms; platformer performance was consistent to about 50 ms (Normoyle et al.) [12]. A menu is far less demanding than a game.
- 🟢 Material (archived v1): mobile transitions about 300 ms, entering 225 ms, leaving 195 ms, over 400 ms may feel too slow [3]. Carbon: most animations 100-300 ms, small components 100-200 ms, page transitions 500-700 ms [4]. Exits are typically shorter than entrances [3].
- 🟢 Apple HIG: prefer quick, precise animations, avoid motion in frequent interactions, make motion optional [5].
- 🔴 A community summary of Apple's WWDC 2018 "Designing Fluid Interfaces" advises responding on press, not on release [5b]. I could not read the transcript.
- 🔴 Conversion: 1 frame is 16.7 ms. 100 ms is 6 f. 195-225 ms is 12-13.5 f. 300 ms is 18 f. 400 ms is 24 f. Torii's spec (carousel 10 f, open 12 f, close 10 f, popup 6/4 f, wipe 16 f) sits at or below Material's 12-13.5 f and inside Carbon's small-component range. The 16 f wipe is a page-level effect and is fine.

### Concrete recommendations
- Input to photon: read keys at VBlank and update the cursor in the same frame. The highlighted row or key changes on the next displayed frame (at most 1 frame, 17 ms, plus the GBA LCD's response). Do not animate cursor moves in lists and the keyboard: jump the pill, optionally a 2 f fade of the old pill. Reserve animation for context changes (open, close, popup, page change).
- Budget: any action shows a visible change within 6 f (100 ms) and work longer than that shows a loading card (B19 E9). Work beyond 1 s shows progress.
- Durations: small things 4-6 f (popup, chips, letter card), medium 10-12 f (carousel step, open), large 16 f (theme wipe, but allowed to skip). Nothing over 24 f. Exits at about 80% of the entrance (open 12 f, close 10 f already matches).
- Easing: ease-out for entering (as spec), ease-in or the same curve in reverse for leaving.
- Held keys: while a repeat is running or a key was pressed within the last 8 f, skip the carousel/page slide and snap (or run the animation at 2x). Inference from Apple's "avoid motion for frequent interactions" [5]. The carousel with a held LEFT/RIGHT should step at the repeat rate with a 4 f slide instead of 10 f, or snap after the third step.
- Input buffering during transitions: accept fresh presses (not auto-repeats) in a queue of at most 2 while an animation runs, apply them when it ends, and let B/START take effect immediately (B reverses a running open: the spec's 10 f reverse from the current progress, not from the end). Drop repeats generated during animation. 🔴 inference.
- Add "Animations: On/Off" (reduce motion) as a Settings row if cheap, per HIG "make motion optional" [5].

## Recommendations for tickets B11, B13, B16, B18

**B11 (popups, confirmations, letter card)**
- Popup card: 14 px rows, 1 px edge, notch at the row, 6 f open (ease-out quad, 8 px slide) and 4 f close. Max 7 rows. Primary first and preselected, destructive last with a hairline in danger ink.
- Confirmation: Cancel on the left, preselected solid pill; destructive on the right in danger ink; toggled by LEFT/RIGHT; B = Cancel. Hold A (30 f with a fill sweep) only for Format all.
- Letter card: 32x32, Galmuri11b, shows immediately, lingers 20 f, closes in 4 f. Skips letters with no entries, "#" for digits and symbols, wraps A to Z.
- Put the repeat table above into the theme or the popup's look table: delay 16 f, 5 f repeat, 3 f after 45 f, 2 f after 120 f for lists; letter jump 20 f delay and 10 f repeat.
- Goldens: add a hold-A fill frame set (`-H`) and the letter card with a skipped letter.

**B13 (game lists)**
- UP/DOWN with the list curve (stop at ends when held, wrap on a fresh press). L/R page by 7 rows (one row overlap) with a 20 f delay and an 8 f repeat. LEFT/RIGHT letter jump through non-empty letters with the card. A-Z lists only.
- Game popup order: Play, Favourite, Clean boot, Boot with addon, Rename, Game info, Delete last in danger ink. Delete confirm: "The save stays." with Cancel preselected. No hold.
- Do not animate the selection pill while a repeat runs.

**B16 (settings)**
- Explanation line in the hint area, at most 38 characters, instant. Plain values. Value edit: LEFT/RIGHT with 16 f delay, 5 f repeat, 3 f after 45 f (number rows only), A keeps, B restores (also restores the theme preview).
- Hotkey capture: A starts, "Press keys…" with a 5 s countdown bar, chips fill live, commit after all keys are released and at least 18 f of a stable set, 2 to 4 keys, must include L, R, START or SELECT. Reject equal or subset combinations with "Already used by X" in danger ink for 90 f (keep the old value). Cancel by timeout.
- Swatches: 12x10 px with the colour name in the hint line. One write on leaving, only when dirty. Per-section "Reset X" row with a confirmation (Cancel preselected). Optional "Animations" row.

**B18 (rename and its keyboard)**
- Mockup first (T12), using this layout: a 232 px card, field at y 22-36, 5 rows x 14 px at y 40-110, 10 columns x 21 px, hint chips at y 147.
- Controls: A type, B backspace (held repeat, never cancels on repeat), L/R cursor, SELECT shift (abc, ABC, back), START OK, UP/DOWN/LEFT/RIGHT wrap on a single press. Function row: Shift, 123, àé, wide Space, Cancel, OK.
- Latin-1 on its own page of 16 columns x 2 rows per case (14 px pitch). Prefill with the cleaned name, caret at the end, a Reset action. Max 60 characters (display name), file rename keeps the extension and caps the stem at 100, the counter turns danger ink at the limit.
- Do not offer `\ / : * ? " < > |`. Reject empty or duplicate names with a one-line error. Cancel with a "Discard changes?" confirm only when text changed.
- Expect about 5-8 WPM (so a 20-character name from scratch takes about 40-60 s), which is why prefill and cursor editing matter more than layout.

---

**Sources**
1. [Towards better gamepad text input](https://www.gamedeveloper.com/design/towards-better-gamepad-text-input) · undated · secondary (fetched)
2. [Secure Text Entry using a Virtual Radial Keyboard (arXiv 2601.05516)](https://arxiv.org/pdf/2601.05516) · 2026 · primary (search summary only)
3. [Material Design (v1) Motion: Duration & easing](https://m1.material.io/motion/duration-easing.html) · archived · primary (fetched)
4. [Carbon Design System v9 motion guidelines](https://v9.carbondesignsystem.com/guidelines/motion) · archived · primary (search snippet only)
5. [Apple HIG: Motion](https://developer.apple.com/tutorials/data/design/human-interface-guidelines/motion.json) · current · primary (search snippet). 5b. [WWDC18 803 Designing Fluid Interfaces](https://developer.apple.com/videos/play/wwdc2018/803/) · 2018 · primary (landing page only; claim from a derivative summary)
6. [Time Scales of UX (Nielsen)](https://jakobnielsenphd.substack.com/p/time-scale-ux) · secondary, quoting Nielsen's 1993 thresholds
7. [kbdrate(8) man page](https://unix.com/man-page/linux/8/KBDRATE/), [Raymond Chen, The Old New Thing, 2009-12-28](https://devblogs.microsoft.com/oldnewthing/20091228-00/?p=15503), [Microsoft Q&A registry keyboard defaults](https://learn.microsoft.com/en-us/answers/questions/1124213/registry-keyboard-response-default-values) · primary/secondary (search snippets)
8. [Microsoft: Gamepad and remote interactions](https://learn.microsoft.com/en-my/WINDOWS/APPS/develop/input/gamepad-and-remote-interactions) · primary (search snippet)
9. [UX Movement: destructive actions](https://uxmovement.com/buttons/how-to-design-destructive-actions-to-prevent-data-loss/), [dev.to: hold vs dialogs](https://dev.to/tomj/why-holding-buttons-is-superior-to-confirmation-dialogs-in-ux-design-1fic), [SaaS UI: destructive patterns](https://www.saasui.design/blog/saas-destructive-actions-confirmation-ux-patterns) · secondary/blog
10. [GamingOnLinux: Steam Deck beta OSK fixes](https://www.gamingonlinux.com/2023/01/steam-desktop-and-steam-deck-beta-fixes-up-more-on-screen-keyboard-issues) · 2023 · secondary
11. [libnds Keyboard struct](https://libnds.devkitpro.org/structKeyboard.html) · primary (search snippet)
12. [Input lag](https://en.wikipedia.com/wiki/Input_lag), [Blur Busters: human reflex](https://www.blurbusters.com/human-reflex/) · secondary (search summary citing Claypool and Finkel, Normoyle et al.)
13. [Wobbrock, EdgeWrite on a joystick (CHI 2004)](https://faculty.washington.edu/wobbrock/pubs/chi-04.01.pdf), [TwoStick, Engadget](https://www.engadget.com/2007-06-21-twostick-system-offers-quicker-on-screen-text-entry.html) · primary/secondary (search summary)

*standard · 1 round · about 12 sources read or skimmed (3 fetched in full) · no researcher fan-out (direct work)*
