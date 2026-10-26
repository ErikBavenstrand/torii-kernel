@ Thumb routines for tests/roms/cheattest: two a ROM patch changes, and the
@ hook a master code names (mGBA runs hooked cheats when it executes it).
	.syntax unified
	.thumb
	.section .text.cheattest, "ax", %progbits

	.align 2
	.global get_magic
	.thumb_func
get_magic:
	movs	r0, #0x5A	@ a 16-bit ROM patch makes it 0x20NN: returns NN
	bx	lr

	.align 2
	.global get_magic2
	.thumb_func
get_magic2:
	ldr	r0, magic2_value
	bx	lr
	.align 2
	.global magic2_value
magic2_value:
	.word	0x13572468	@ a 32-bit ROM patch changes what it returns

	.align 2
	.global frame_hook
	.thumb_func
frame_hook:
	mov	r8, r8	@ the hook's address: a nop, which mGBA runs again after its breakpoint
	bx	lr
