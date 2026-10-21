# Menu art

The menu's icons, carousel objects, ground shadows and placeholder boxes.
`just assets` converts them into `source/ui/art_data.c` and `art_ids.h`, and
`art.toml` says how; `source/ui/art.h` describes what each becomes. To redraw
one, edit its PNG and run `just assets`; `just shots-update art-sheet`
re-records the sheet that shows them all.

The placeholder boxes in `boxes/` (one a console, `other.png` for the rest)
are drawn by `tools/mockup/boxes.py`: change it and run it, rather than
editing those PNGs by hand. `tools/mockup/icons_sheet.py` draws a before and
after sheet of the icons against any git revision, in both themes.

## Credits

The Game Boy Advance, Game Boy Color and Game Boy objects (`sprites/gba.png`,
`gbc.png`, `gb.png`) are redrawn at sprite size on the outlines of KyleBing's
retro game console icons (<https://github.com/KyleBing/retro-game-console-icons>),
licensed CC BY 4.0 (<https://creativecommons.org/licenses/by/4.0/>). The
Game Boy Advance object was then narrowed to fit a 64x64 sprite. The other
objects, the icons, the shadows and the boxes are Torii's own.
