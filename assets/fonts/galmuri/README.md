# Galmuri

The menu fonts: Galmuri9 for body text and Galmuri11 Bold for headings, by Lee Minseo
(quiple), from Galmuri v2.40.4 (<https://github.com/quiple/galmuri>, release asset
`Galmuri-v2.40.4.zip`, SHA-256 `c8b3d9861a62ae73c8b1178091401cd79994812437ef386413f6dd54856e60e7`).
Licensed under the SIL Open Font License 1.1, in `LICENSE.txt`.

The BDF files are cut to the glyphs Torii uses, U+0020–U+00FF and U+FFFD, with
`tools/font.py --subset-bdf`; the glyphs themselves are unchanged. `just font` converts them
to C. `docs/research/fonts.md` explains the choice.

`Galmuri9-hangul.bdf` holds the script glyphs of the Korean pack (`lang/ko.ini`) from the same
release: Galmuri9's 11,172 Hangul syllables (U+AC00-U+D7A3), compatibility jamo
(U+3131-U+318E) and kana (U+3041-U+30FF), unchanged. `tools/galmuri.py` cuts it from the
release zip after checking its SHA-256 (`just galmuri`; it fetches the zip into the git-ignored
`dist/galmuri-full/` if it isn't there, reads three members of it as bytes and runs nothing from
it). The full release (20 MB, with Hanja and the other sizes) is not vendored. The pack uses
Galmuri9's glyphs for both menu lines: Galmuri11 Bold has Hangul but no kana, and the syllables of
both fonts would take about 520 KB, over the 320 KB a pack's font has. The same licence
covers it (`LICENSE.txt`).
