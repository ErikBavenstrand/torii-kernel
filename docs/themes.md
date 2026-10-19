# Themes

A theme changes how the menu looks and feels: its colours, fonts, row
height, spacing, fades, dialog slide, scrolling of long names, key repeat
timings, the layout of single screen elements, and a wallpaper behind the
menu. Themes live on the SD card, so making one needs no kernel build.

The screens on the v0.3 tile engine draw in the theme: the SD list, the
NOR list and Recently Played with their menus and questions, SETTINGS
(both pages), HELP, the cheat viewer, and the test kernel's UI demo.
Launching, WRITE TO NOR and Diagnostics still draw the old way and ignore
it until they are ported.

## Make one

```sh
just theme-new night light          # themes/night/theme.ini, every key at the light theme's value
$EDITOR themes/night/theme.ini      # change what you like, delete the rest
just theme-wallpaper art.png themes/night/wallpaper.tas --dither   # optional
just theme-check themes/night       # what the kernel would log, and preview PNGs
```

Then copy the folder to the card as `/torii/themes/night/` and pick it
in SETTINGS page 2, THEME:

- The row cycles DARK, LIGHT, then up to 8 folders of `/torii/themes`
  (sorted by name). Picking a folder loads the theme and previews it at
  once; one that fails to load is skipped, with the reason shown.
- OK writes the folder's name to `/torii/theme.txt`, then stores the
  choice in the settings. If the card write fails, the old choice stays.
- The kernel loads the theme at every boot. If it fails then, the boot
  screen says why, the menu uses Dark, and the row shows DARK.
- ACCENT applies over a theme too: THEME'S OWN keeps the theme's
  `accent`, and any other ACCENT replaces it.

- `just theme-new NAME [dark|light]` writes `themes/NAME/theme.ini` from a
  built-in theme, listing every key with that theme's value. A key you
  delete keeps the built-in's value, so a theme can be a few lines long.
- `just theme-wallpaper IN.png OUT.tas [--dither]` converts a picture into
  a wallpaper asset (below). Uncomment `file = wallpaper.tas` in the
  theme's `[wallpaper]` section to use it.
- `just theme-check DIR` checks the folder with the kernel's own parser,
  built for your computer from `source/ui/theme_ini.c`
  (`tools/themecheck.c`). It prints the lines the kernel would log when
  loading the theme from `/torii/themes/DIR's name`: every warning, and
  either `loaded` or the reason the theme is refused. It then decodes the
  wallpaper, boots the emulated cart with the theme, and writes PNGs of
  the UI demo (list, dialog, grid), the SD list, SETTINGS page 2, the
  game menu and the cheat viewer to `dist/theme-check/NAME/`, printing
  their paths. Kernel errors in these runs fail the check: a screen the
  theme breaks (a patch that pushes it off screen, say) falls back to the
  base theme for the rest of the session. On the card the previews
  name the folder `example`, as the test kernel's UI demo loads that one.
  Exits non-zero if the kernel would refuse the theme or a preview fails.

`themes/example/` is a complete example: its own colours and timings, a
small wallpaper, and a patch that gives the UI demo's list margins so the
wallpaper shows beside it.

## On the card

`/torii/themes/<folder>/theme.ini` plus the theme's files in the same
folder. A folder name is 1-23 bytes of letters, digits, space, `-`, `_`
and `.`, not starting or ending with `.` or a space; other folders aren't
listed. The display name comes from the `name` key, or the folder's name.

When a theme fails to load at boot (no `theme.ini`, a version other than
1, a broken or 8bpp wallpaper), the boot screen says why for two seconds,
the log keeps the reason, and the menu uses Dark. Warnings (unknown keys,
values out of range, a missing wallpaper file) go only to the log; with
LOGGING on, `just logs` shows them as `theme: ...` lines. If a theme's
patches break a screen, the menu falls back to the theme's base theme for
the session.

## The wallpaper

A TAS1 asset (`source/engine/asset.h`): 4bpp, at most 256 tiles of 8x8 px
(128x128 px, say, or 256x64), up to 15 colours plus colour 0. It is
repeated over the screen from the top left, behind every screen element,
and dims and fades with the menu. Colour 0 shows the theme's `bg`. It
is hidden while a screen uses plane 3 for itself (the game and NOR game
menus, the list's questions, the UI demo's dialog), and it shows only
where screens leave the background clear, so a theme that
wants it beside a list patches the list's rectangle (as the example does).
It never takes room a screen needs. The largest ones (over about 200
tiles, as 128x128 px) can go off behind a question with a lot of text and
come back when it closes; putting a large one back can take a frame or two.

The converter (`tools/asset.py wallpaper`) takes a PNG whose sides are
multiples of 8 and up to 256 px:

- Pixels with alpha under 128 become colour 0.
- Colours are rounded to the GBA's 15 bits, then, if more than 15 remain,
  reduced to 15 by median cut; `--dither` spreads the error
  (Floyd-Steinberg), which suits photos and gradients more than pixel art.
- An indexed PNG whose pixels use only its first 16 palette entries is
  taken as it is: its indices are kept and index 0 is colour 0. Use this
  to control the palette yourself.
- Tiles are stored whole, in order, LZ77-compressed: repeated tiles cost
  little after compression, so there is no tile map to dedupe into.

The limits (4bpp, 256 tiles, 8 KB of tiles, 16 KB per file) are read from
the kernel's headers, so the converter and the kernel never disagree. The
same picture gives the same file on every machine.

## theme.ini reference (version 1)

A UTF-8 text file (a BOM is skipped; LF, CRLF or CR line ends; at most 8 KB; lines over 255 bytes are skipped with a warning). `key = value` lines, `[section]` lines, and comments: a line starting with `;` or `#`, `;` after a blank at the end of a value (not `#`, which starts colours), or `;` or `#` after a section line. Section and key names are case-insensitive. A later line wins over an earlier one.

| Section | Key | Value | Default |
|---|---|---|---|
| (top) | `version` | `1` (required; anything else refuses the file) | none |
| (top) | `name` | shown in settings, up to 23 bytes (cut at a character) | the folder's name |
| (top) | `base` | `dark` or `light`: the built-in that missing keys come from | `dark` |
| `[colours]` (or `[colors]`) | `bg`, `panel`, `text`, `dim`, `accent`, `accent_text`, `border`, `track`, `shadow`, `selection`, `selection_text`, `icon0` ... `icon5` | `#RRGGBB`, `0xRRGGBB` or `RRGGBB`; each channel's top 5 bits (as `tools/asset.py`) | the base's |
| `[fonts]` | `body`, `heading` | `body` or `heading` (the built-in fonts) | `body`, `heading` |
| `[layout]` | `row_h` | `16` or `32` (list rows) | 16 |
| `[layout]` | `pad` | 0-16 px text inset | 6 |
| `[effects]` | `modal_dim`, `idle_dim` | 0-16 (BLDY) | dark 6, light 5; 11 |
| `[effects]` | `fade_frames` | 1-120 | 8 |
| `[effects]` | `slide` | 0-96 px a dialog slides up from | 24 |
| `[effects]` | `marquee_delay` | 0-255 frames before a cut name scrolls | 45 |
| `[effects]` | `marquee_speed` | 1-60 px a second | 60 |
| `[effects]` | `marquee_pause` | 0-255 frames it rests at the end | 60 |
| `[effects]` | `marquee_gap` | 0-120 px it scrolls past the name's end | 8 |
| `[effects]` | `page_fade` | 0-30 frames a list fades in after a page change (0: none) | 4 |
| `[input]` | `delay`, `interval`, `page_delay`, `page_interval` | 1-255 frames (`engine/input.h`): a held d-pad repeats after `delay`, first every `interval` frames | 15, 4, 20, 6 |
| `[input]` | `ramp` | 0-255 frames from the first repeat to top speed, along a smooth curve (0: top speed at once) | 90 |
| `[input]` | `top_speed` | 1-60 repeats a second at the end of the ramp (60: every frame) | 60 |
| `[input]` | `wrap_pause` | 0-255 frames | 15 |
| `[wallpaper]` | `file` | a file name in the theme's folder (no `/`, `\`, `:`, not starting with `.`, under 32 bytes): a 4bpp TAS1 asset | none |
| `[patches]` | `VIEW.NODE.FIELD` | see below; at most 16 (a repeated target replaces its patch) | none |

Patches: `VIEW` is a view id (1-255) or a name from `theme_view_names[]` in `source/ui/theme_ini.c` (`sd-list` 10, `nor-list` 11, `recent` 12, `file-menu` 13, `nor-menu` 14, `settings1` 20, `settings2` 21, `help` 22, `dialog` 30, `progress` 31, `diag` 32, `diag-demo` 33, `cheats` 34, and the UI demo's `ui-demo` 1, `ui-dialog` 2, `ui-grid` 3; that table is the list to trust); `NODE` is a node id (1-255, `ui_spec.id` in the screen's view spec, e.g. `source/screens/lists.c`, `source/settings.h` or `tests/kernel/ui_demo.c`); `FIELD` and its values:

- `x`, `y`: -240 to 240 px; `w`, `h`: 0 to 240 px. Rounded towards zero to a multiple of 8, with a warning. Their meaning follows the node's anchor (`ui.h`).
- `anchor`: words joined by `+`, `|`, `,` or blanks: `left`, `right`, `hfill`, `hcenter`, `top`, `bottom`, `vfill`, `vcenter`, `fill` (both fills), `center` (both centres); or a number 0-15.
- `hidden`: `yes`/`no`, `true`/`false`, `on`/`off`, `1`/`0`.
- `ink`, `paper`: a role, `none`, `bg`, `panel`, `text`, `dim`, `accent`, `accent_text`, `border`, `track`, `shadow`, `icon0`-`icon5`, or 0-15.

Out of range numbers are clamped, with a warning; values that can't be read (a colour that isn't one, `row_h = 20`, an unknown font or field) are ignored, with a warning, and the key keeps the base's value. Unknown sections and keys are warnings. Only a missing or wrong `version` (or a file over 8 KB) refuses the file.
