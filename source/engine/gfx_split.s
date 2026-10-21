@ The raster split's VCOUNT handler (gfx.h, gfx_set_split): libgba's
@ dispatcher calls it on the line before the split. It writes DMA0's and
@ DMA1's source, destination and control from gfx_split_dma, so both copy
@ in this line's HBlank. In IWRAM: it runs while the line draws, and is
@ kept short (the IWRAM margin over the deepest stack is about 2 KB).

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
	.pool
