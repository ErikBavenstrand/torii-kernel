@ The overlay image's header, struct ingame_image (source/ingame_abi.h): the
@ first bytes of the image in cart SRAM and of the window once loaded. The
@ stub reads the sizes and the entry; the overlay fills in obj_bytes and
@ shown in its loaded copy.
	.section .ov_header,"aw",%progbits
	.global	ov_header
	.balign	4
ov_header:
	.word	0x31564F54			@ magic "TOV1"
	.hword	1				@ version
	.hword	ov_header_end - ov_header	@ header_bytes
	.word	__ov_image_end - 0x02000000	@ image_bytes
	.word	__ov_stack_top - 0x02000000	@ window_bytes
	.word	ov_entry			@ entry (Thumb: bit 0 set)
	.word	0				@ obj_bytes
	.word	0				@ shown
	.word	0				@ reserved
ov_header_end:
