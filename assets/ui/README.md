# Menu art

The menu's icons, carousel objects, ground shadows and placeholder box.
`just assets` converts them into `source/ui/art_data.c` and `art_ids.h`, and
`art.toml` says how; `source/ui/art.h` describes what each becomes. To redraw
one, edit its PNG and run `just assets`; `just shots-update art-sheet`
re-records the sheet that shows them all.

## Credits

The Game Boy Advance, Game Boy Color and Game Boy objects (`sprites/gba.png`,
`gbc.png`, `gb.png`) are redrawn at sprite size on the outlines of KyleBing's
retro game console icons (<https://github.com/KyleBing/retro-game-console-icons>),
licensed CC BY 4.0 (<https://creativecommons.org/licenses/by/4.0/>). The
Game Boy Advance object was then narrowed to fit a 64x64 sprite. The other
objects, the icons, the shadows and the box are Torii's own.
