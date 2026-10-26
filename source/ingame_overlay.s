@ The in-game menu's overlay (source/overlay), built by the Makefile before
@ the kernel: source/ingame.c writes it to cart SRAM at each launch
	.section .rodata
	.global	ingame_overlay, ingame_overlay_size

	.balign	4
ingame_overlay:
	.incbin	"overlay.bin"
	.balign	4, 0			@;sram_copy loads whole words
ingame_overlay_end:

	.balign	4
ingame_overlay_size:
	.word	ingame_overlay_end - ingame_overlay
