@ The raster split's VCOUNT handler (gfx.h, gfx_set_split): libgba's
@ dispatcher calls it on the line before the split. It writes DMA0's and
@ DMA1's source, destination and control from gfx_split_dma, so both copy
@ in this line's HBlank. In IWRAM: it runs while the line draws, and is
@ kept short (the IWRAM margin over the deepest stack is about 2 KB).
@ gfx_split_vblank below runs in each VBlank while a split is on.

	.section .iwram,"ax",%progbits
	.arm
	.align 2
	.global gfx_split_irq
gfx_split_irq:
	ldr	r12, =gfx_split_dma
	ldr	r3, =0x040000B0		@ DMA0SAD; DMA1's registers follow DMA0CNT
	ldmia	r12!, {r0-r2}
	stmia	r3!, {r0-r2}
	ldmia	r12, {r0-r2}
	stmia	r3, {r0-r2}
	bx	lr

@ The split's VBlank handler: puts the parts' top values back with DMA0 and
@ DMA1 in immediate mode, from gfx_split_top, so a frame that misses its
@ commit (an overrun) still shows the split, not the split values from the
@ top. The commit sets gfx_split_top and copies the tops again itself.
	.global gfx_split_vblank
gfx_split_vblank:
	ldr	r12, =gfx_split_top
	ldr	r3, =0x040000B0		@ DMA0SAD
	mov	r0, #0
	str	r0, [r3, #8]		@ DMA0CNT off: an HBlank copy never armed
	str	r0, [r3, #20]		@ DMA1CNT off
	ldmia	r12!, {r0-r2}
	stmia	r3!, {r0-r2}
	ldmia	r12, {r0-r2}
	stmia	r3, {r0-r2}
	bx	lr
	.pool
