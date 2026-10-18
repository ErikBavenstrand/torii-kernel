# Galmuri

The menu fonts: Galmuri9 for body text and Galmuri11 Bold for headings, by Lee Minseo
(quiple), from Galmuri v2.40.4 (<https://github.com/quiple/galmuri>, release asset
`Galmuri-v2.40.4.zip`, SHA-256 `c8b3d9861a62ae73c8b1178091401cd79994812437ef386413f6dd54856e60e7`).
Licensed under the SIL Open Font License 1.1, in `LICENSE.txt`.

The BDF files are cut to the glyphs Torii uses, U+0020–U+00FF and U+FFFD, with
`tools/font.py --subset-bdf`; the glyphs themselves are unchanged. `just font` converts them
to C. `docs/research/fonts.md` explains the choice.
