@ The glyph blitter, text_blit (text.h): 1bpp font rows into 4bpp
@ column-major tiles. ARM code in IWRAM.
@
@ A font row expands to a tile row word with its pixel i in nibble i: the
@ row's bits spread out by shifts and masks (the trick in libtonc's
@ chr4c_drawg_b1cts_fast, MIT licence, by J. Vijn), and times 15 that is the
@ mask of the inked pixels, times ink their colour. Drawn at pixel x of a
@ tile, the word shifts left by 4x and the bits shifted out go to the same
@ row of the next tile column, column_words words on. Each tile word is read
@ and written once per row, with 32-bit accesses only (VRAM ignores byte
@ writes), and blank rows are skipped.
@
@ Each glyph takes one of three loops: inside one tile column; 8 px or
@ narrower across two; wider (both bytes of its rows), across two or three.
@ A shift by a register holding 32 gives 0 in ARM code, so the wide loop
@ needs no case for x a multiple of 8 (unlike C, where it is undefined).
@
@ tests/kernel/test_text.c checks it against expected tiles and times it
@ against the alternatives in tests/kernel/text_bench.iwram.c.

@ struct text_run
	.equ	RUN_TILES, 0
	.equ	RUN_COLUMN_WORDS, 4
	.equ	RUN_FONT, 8
	.equ	RUN_GLYPHS, 12
	.equ	RUN_COUNT, 16
	.equ	RUN_X, 20
	.equ	RUN_Y, 24
	.equ	RUN_INK, 28
@ struct torii_font
	.equ	FONT_GLYPHS, 0
	.equ	FONT_BITS, 4
@ struct torii_glyph, 6 bytes
	.equ	GLYPH_OFFSET, 0
	.equ	GLYPH_ADVANCE, 2
	.equ	GLYPH_TOP, 3
	.equ	GLYPH_ROWS, 4
	.equ	GLYPH_WIDTH, 5
@ The stack frame
	.equ	SP_END, 0		@ end of the glyph indices
	.equ	SP_TABLE, 4		@ font->glyphs
	.equ	SP_BITS, 8		@ font->bits
	.equ	SP_BASE, 12		@ tiles + y words
	.equ	SP_RIGHT, 16		@ wide glyphs: px + width
	.equ	SP_X, 20		@ wide glyphs: r11 and r12, kept here
	.equ	SP_G, 24
	.equ	SP_SIZE, 28

@ Registers in the loops:
@   r0  row bits, then the expanded row    r7  ink
@   r1  mask (expanded * 15)              r8  column_words * 4
@   r2  colour (expanded * ink)           r9  ls = 4 * (x & 7)
@   r3  src, the glyph's next row         r10 rs = 32 - ls
@   r4  dst, the tile word of that row    r11 pen x (next glyph's)
@   r5  rows left                         r12 next glyph index
@   r6  0x01010101                        lr  temporary

@ Spreads the 8 bits of \r: bit i to bit 4i; \t is clobbered
	.macro	SPREAD r, t
	orr	\r, \r, \r, lsl #12
	orr	\r, \r, \r, lsl #6
	and	\t, \r, r6, lsl #1
	and	\r, \r, r6
	orr	\r, \r, \t, lsl #3
	.endm

@ text_blit for a font with \stride byte rows
	.macro	BLIT name, stride
	.section .iwram,"ax",%progbits
	.arm
	.align	2
	.global	\name
	.type	\name, %function
\name:
	push	{r4-r11, lr}
	sub	sp, sp, #SP_SIZE
	ldr	r1, [r0, #RUN_FONT]
	ldr	r2, [r1, #FONT_GLYPHS]
	ldr	r3, [r1, #FONT_BITS]
	str	r2, [sp, #SP_TABLE]
	str	r3, [sp, #SP_BITS]
	ldr	r12, [r0, #RUN_GLYPHS]
	ldr	r1, [r0, #RUN_COUNT]
	add	r1, r12, r1
	str	r1, [sp, #SP_END]
	ldr	r1, [r0, #RUN_TILES]
	ldr	r2, [r0, #RUN_Y]
	add	r1, r1, r2, lsl #2
	str	r1, [sp, #SP_BASE]
	ldr	r8, [r0, #RUN_COLUMN_WORDS]
	mov	r8, r8, lsl #2
	ldr	r11, [r0, #RUN_X]
	ldr	r7, [r0, #RUN_INK]
	ldr	r6, =0x01010101

.L\name\()_glyph:
	ldr	r0, [sp, #SP_END]
	cmp	r12, r0
	bhs	.L\name\()_done
	ldrb	r0, [r12], #1
	ldr	r1, [sp, #SP_TABLE]
	add	r0, r0, r0, lsl #1
	add	r1, r1, r0, lsl #1	@ the glyph: table + 6 * index
	ldrb	r5, [r1, #GLYPH_ROWS]
	ldrb	r2, [r1, #GLYPH_ADVANCE]
	mov	r9, r11			@ its x
	add	r11, r11, r2
	cmp	r5, #0
	beq	.L\name\()_glyph	@ blank
	ldrh	r3, [r1, #GLYPH_OFFSET]
	ldrb	r0, [r1, #GLYPH_TOP]
	ldrb	r2, [r1, #GLYPH_WIDTH]
	ldr	lr, [sp, #SP_BITS]
	add	r3, lr, r3
	ldr	r4, [sp, #SP_BASE]
	add	r4, r4, r0, lsl #2
	mov	r0, r9, lsr #3
	mla	r4, r0, r8, r4		@ + its tile column
	and	r0, r9, #7		@ px
	mov	r9, r0, lsl #2
	rsb	r10, r9, #32
	add	r1, r0, r2		@ px + width
	cmp	r1, #8
	bls	.L\name\()_one
	cmp	r2, #8
	bls	.L\name\()_two

	@ Wider than 8 px: both bytes, two or three words a row
	str	r1, [sp, #SP_RIGHT]
	str	r11, [sp, #SP_X]
	str	r12, [sp, #SP_G]
.L\name\()_wide:
	ldrh	r0, [r3], #\stride
	cmp	r0, #0
	beq	.L\name\()_wide_next
	mov	r1, r0, lsr #8		@ pixels 8-15
	and	r0, r0, #0xFF		@ pixels 0-7
	SPREAD	r0, lr
	SPREAD	r1, lr
	mov	r2, r0, lsr r10		@ 0 when rs is 32
	orr	r2, r2, r1, lsl r9	@ the middle word
	rsb	lr, r0, r0, lsl #4
	mul	r11, r0, r7
	ldr	r12, [r4]
	bic	r12, r12, lr, lsl r9
	orr	r12, r12, r11, lsl r9
	str	r12, [r4]
	rsb	lr, r2, r2, lsl #4
	mul	r11, r2, r7
	ldr	r12, [r4, r8]
	bic	r12, r12, lr
	orr	r12, r12, r11
	str	r12, [r4, r8]
	ldr	r12, [sp, #SP_RIGHT]
	cmp	r12, #16
	bls	.L\name\()_wide_next	@ no third word (and rs may be 32)
	rsb	lr, r1, r1, lsl #4
	mul	r11, r1, r7
	ldr	r12, [r4, r8, lsl #1]
	bic	r12, r12, lr, lsr r10
	orr	r12, r12, r11, lsr r10
	str	r12, [r4, r8, lsl #1]
.L\name\()_wide_next:
	add	r4, r4, #4
	subs	r5, r5, #1
	bne	.L\name\()_wide
	ldr	r11, [sp, #SP_X]
	ldr	r12, [sp, #SP_G]
	b	.L\name\()_glyph

	@ Inside one tile column
.L\name\()_one:
	ldrb	r0, [r3], #\stride
	orrs	r0, r0, r0, lsl #12
	beq	.L\name\()_one_next
	orr	r0, r0, r0, lsl #6
	and	r1, r0, r6, lsl #1
	and	r0, r0, r6
	orr	r0, r0, r1, lsl #3
	rsb	r1, r0, r0, lsl #4
	mul	r2, r0, r7
	ldr	r0, [r4]
	bic	r0, r0, r1, lsl r9
	orr	r0, r0, r2, lsl r9
	str	r0, [r4]
.L\name\()_one_next:
	add	r4, r4, #4
	subs	r5, r5, #1
	bne	.L\name\()_one
	b	.L\name\()_glyph

	@ 8 px or narrower, across two tile columns (px is 1-7, rs 4-28)
.L\name\()_two:
	ldrb	r0, [r3], #\stride
	orrs	r0, r0, r0, lsl #12
	beq	.L\name\()_two_next
	orr	r0, r0, r0, lsl #6
	and	r1, r0, r6, lsl #1
	and	r0, r0, r6
	orr	r0, r0, r1, lsl #3
	rsb	r1, r0, r0, lsl #4
	mul	r2, r0, r7
	ldr	r0, [r4]
	bic	r0, r0, r1, lsl r9
	orr	r0, r0, r2, lsl r9
	str	r0, [r4]
	ldr	r0, [r4, r8]
	bic	r0, r0, r1, lsr r10
	orr	r0, r0, r2, lsr r10
	str	r0, [r4, r8]
.L\name\()_two_next:
	add	r4, r4, #4
	subs	r5, r5, #1
	bne	.L\name\()_two
	b	.L\name\()_glyph

.L\name\()_done:
	add	sp, sp, #SP_SIZE
	pop	{r4-r11, lr}
	bx	lr
	.ltorg
	.size	\name, . - \name
	.endm

	BLIT	text_blit, 2

#ifdef TORII_TEST_KERNEL
@ For a font with 1 byte rows (8 px glyphs at most): the benchmark compares
@ the two row sizes
	BLIT	text_blit_rows1, 1
#endif
