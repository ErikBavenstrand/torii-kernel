@ The built-in emulators, built from source by `just emulators`
@ (emulators/fetch.sh pins them): each is padded to where it looks for its
@ game, which the launch puts right after it (LoadEMU2PSRAM)
	.section .rodata
	.global	jagoombacolor_gba, jagoombacolor_gba_size
	.global	pocketnes_gba, pocketnes_gba_size

	.balign	4
jagoombacolor_gba:
	.incbin	"emulators/out/jagoombacolor.gba"
jagoombacolor_gba_end:

	.balign	4
pocketnes_gba:
	.incbin	"emulators/out/pocketnes.gba"
pocketnes_gba_end:

	.balign	4
jagoombacolor_gba_size:
	.word	jagoombacolor_gba_end - jagoombacolor_gba
pocketnes_gba_size:
	.word	pocketnes_gba_end - pocketnes_gba
