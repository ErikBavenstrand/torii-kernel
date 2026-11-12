# Galmuri

The menu fonts: Galmuri9 for body text and Galmuri11 Bold for headings, by Lee Minseo
(quiple), from Galmuri v2.40.4 (<https://github.com/quiple/galmuri>, release asset
`Galmuri-v2.40.4.zip`, SHA-256 `c8b3d9861a62ae73c8b1178091401cd79994812437ef386413f6dd54856e60e7`).
Licensed under the SIL Open Font License 1.1, in `LICENSE.txt`.

The BDF files are cut to the glyphs Torii uses, U+0020–U+00FF and U+FFFD, with
`tools/font.py --subset-bdf`; the glyphs themselves are unchanged. `just font` converts them
to C. `docs/research/fonts.md` explains the choice.

`Galmuri9-extra.bdf` and `Galmuri11-extra.bdf` hold the kernel's glyphs beyond Latin-1, from the
same release: the code points `EXTRA_RANGES` in `tools/font.py` names (extended Latin, Greek,
Cyrillic, Vietnamese, punctuation and symbols), unchanged. `tools/galmuri.py` cuts them from
the release zip after checking its SHA-256 (`just galmuri`; it fetches the zip into the
git-ignored `dist/galmuri-full/` if it isn't there, reads the two members as bytes and runs
nothing from it). The full release (20 MB, with Hangul, Hanja and the other sizes) is not
vendored. `just font` turns them into `source/engine/font_extra.c`: Galmuri9's for the body
line, and Galmuri11's made bold (each glyph smeared a pixel right) for headings, as Galmuri11
Bold has none of them. The same licence covers them (`LICENSE.txt`).
