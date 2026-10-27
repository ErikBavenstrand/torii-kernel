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
	.word	0				@ check: the kernel writes it (ingame_image_check)
ov_header_end:

@ ov_system_regs(out): the game's System-mode sp and lr into out[0] and out[1]
@ (the DEBUG snapshot's, ov.c); ARM, as a mode change needs. The overlay
@ runs in IRQ mode, which it goes back to.
	.section .text.ov_system_regs,"ax",%progbits
	.arm
	.balign	4
	.global	ov_system_regs
	.type	ov_system_regs,%function
ov_system_regs:
	mrs	r1,cpsr
	orr	r2,r1,#0x1F			@ System, IRQs as they were
	msr	cpsr_c,r2
	str	sp,[r0]
	str	lr,[r0,#4]
	msr	cpsr_c,r1
	bx	lr
