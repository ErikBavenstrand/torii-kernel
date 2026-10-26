@ The per-frame cheat interpreter (v0.7). Patch_RTS_Cheat (GBApatch.c)
@ copies Cheat_blob_start to Cheat_blob_end into the patch area at the
@ game's ROM end, right after the in-game menu's code, and the cheat table
@ right after it; the hook (RTS_irq, gba_rts_patch.s) calls Cheat_blob_start
@ on each VBlank IRQ while cheats are on. The code only reads the table and
@ the memory the cheats name: no RAM of its own, nothing but its 36 bytes of
@ the IRQ stack, and no kernel address (each literal is PC-relative).
@
@ The table's layout (cheatcode.h has the C side, cheat_lower writes it):
@ cheats one after another, each a header word, then its ops; a 0 word ends
@ the table.
@   header: bits 0-14 the ops (n), bit 15 "plain" (no conditionals), bits
@           16-31 the bytes of ops after the header
@   op word 0: bits 0-27 the address, bits 28-31 the kind K:
@     K 0, 1, 2  a write of 1, 2 or 4 bytes; word 1 the value (8 bytes)
@     K 3, 4, 5  an op of K words: word 1 the info (bits 0-3 the type, 4-5
@                the width as log2, 16-31 the count), word 2 the value,
@                word 3 (K >= 4) the address step, or a conditional's ELSE
@                ops, word 4 (K = 5) the value step
@     K 6, 7, 8  in plain cheats only: bits 0-27 are N, and N pairs of words
@                (address, value) follow: N writes of 1, 2 or 4 bytes
@   types: 0 write (fill, slide), 1 pointer write, 2 add, 3 or, 4 and,
@          5 nothing, 7 never, 8 eq, 9 ne, 10 lt, 11 gt (signed), 12 ult,
@          13 ugt, 14 and (true when not 0), 15 nand (true when 0)
@
@ The semantics are mGBA's mCheatRefresh (src/core/cheats.c), op by op: a
@ conditional covers the next count ops (skipped when false) and, with an
@ ELSE, the else ops after them (skipped when true), with the same index
@ arithmetic, so even odd layouts run as there. A plain cheat runs its ops
@ in order. Loads and stores are the CPU's own at their width, as mGBA's
@ bus reads and writes are: an odd halfword or word address is the
@ hardware's (and mGBA's) rotated load and aligned store.
@
@ Registers: r0 the op pointer, r6 the index of the op at r0, r7 endLoc,
@ r8 elseLoc (0: none), r10 the cheat's ops (0 in a plain cheat), r11 its
@ end; r1-r5, r9, r12 and lr are scratch.

	.section .text.cheat_blob,"ax",%progbits
	.arm
	.align 2
	.global Cheat_blob_start
	.global Cheat_blob_end
	.global Cheat_run
	.type Cheat_run, %function

@ The hook's entry: the table follows the code
Cheat_blob_start:
	adrl	r0, Cheat_blob_end
	@ falls into Cheat_run

@ void Cheat_run(const u32 *table): runs every cheat of the table once.
@ Callable from C (the kernel's tests time it).
Cheat_run:
	stmfd	sp!, {r4-r11, lr}

cheat_next:
	ldr	r1, [r0], #4
	movs	r10, r1, lsl #16	@ n << 16, the plain bit in bit 31
	beq	cheat_done
	add	r11, r0, r1, lsr #16
	movmi	r10, #0
	bmi	plain
	mov	r10, r10, lsr #16
	mov	r6, #0
	mov	r7, #0
	mov	r8, #0

@ --- a cheat with conditionals: one op at a time, by index ----------------
op_loop:
	ldmia	r0!, {r1, r2}
	add	r6, r6, #1
	mov	r3, r1, lsr #28
	bic	r1, r1, #0xF0000000
	cmp	r3, #3
	bhs	ext
	cmp	r3, #1
	strltb	r2, [r1]
	streqh	r2, [r1]
	strgt	r2, [r1]

@ After an op that isn't a conditional: the ELSE's jump, then the next op
post_plain:
	cmp	r8, #0
	bne	post_else
post_next:
	cmp	r6, r10
	blo	op_loop
	mov	r0, r11
	b	cheat_next
post_else:
	sub	r1, r6, #1		@ i
	cmp	r8, r1
	bne	post_next
	mov	r1, r7			@ i = endLoc
	mov	r7, #0
	add	r1, r1, #1
	b	walk

@ After a conditional: r3 its truth, r9 its count (> 0), r5 its ELSE ops
post_cond:
	sub	r1, r6, #1		@ i
	cmp	r8, #0
	beq	1f
	cmp	r8, r1
	moveq	r1, r7
	moveq	r7, #0
1:	cmp	r3, #0
	addeq	r1, r1, r9		@ false: past its ops
	beq	2f
	cmp	r5, #0
	addne	r8, r1, r9		@ elseLoc = i + count
	addne	r7, r8, r5		@ endLoc = elseLoc + else ops
2:	add	r1, r1, #1

@ On to op r1 (never before r6); past the last op ends the cheat
walk:
	cmp	r1, r10
	movhs	r0, r11
	bhs	cheat_next
3:	cmp	r6, r1
	beq	op_loop
	ldr	r2, [r0]
	mov	r2, r2, lsr #28
	cmp	r2, #3
	movlo	r2, #2
	add	r0, r0, r2, lsl #2
	add	r6, r6, #1
	b	3b

@ --- a plain cheat: its ops in order ---------------------------------------
plain:
	cmp	r0, r11
	bhs	cheat_next
	ldr	r1, [r0], #4
	mov	r3, r1, lsr #28
	bic	r1, r1, #0xF0000000
	cmp	r3, #6
	bhs	run
	ldr	r2, [r0], #4
	cmp	r3, #3
	bhs	ext
	cmp	r3, #1
	strltb	r2, [r1]
	streqh	r2, [r1]
	strgt	r2, [r1]
	b	plain

@ A run of r1 writes of the width K - 6 (r3): the odd ones, then four a
@ loop. r2-r9 are free in a plain cheat.
	.macro	run_loop store
	ands	r12, r1, #3
	beq	2f
1:	ldmia	r0!, {r2, r3}
	\store	r3, [r2]
	subs	r12, r12, #1
	bne	1b
2:	movs	r1, r1, lsr #2
	beq	plain
3:	ldmia	r0!, {r2-r9}
	\store	r3, [r2]
	\store	r5, [r4]
	\store	r7, [r6]
	\store	r9, [r8]
	subs	r1, r1, #1
	bne	3b
	b	plain
	.endm

run:	cmp	r3, #7
	beq	run16
	bhi	run32
run8:	run_loop strb
run16:	run_loop strh
run32:	run_loop str

@ --- an op of K (r3) words: r1 its address, r2 its info -----------------

@ A fill of r9 (> 0) values from r4 at r1, steps r5 and r12; one of a
@ constant value four a loop (the odd ones first)
	.macro	fill_loop store
	cmp	r12, #0
	bne	3f
	ands	lr, r9, #3
	beq	2f
1:	\store	r4, [r1], r5
	subs	lr, lr, #1
	bne	1b
2:	movs	r9, r9, lsr #2
	beq	ext_done
4:	\store	r4, [r1], r5
	\store	r4, [r1], r5
	\store	r4, [r1], r5
	\store	r4, [r1], r5
	subs	r9, r9, #1
	bne	4b
	b	ext_done
3:	\store	r4, [r1], r5
	add	r4, r4, r12
	subs	r9, r9, #1
	bne	3b
	b	ext_done
	.endm

ext:
	ldr	r4, [r0], #4		@ the value
	mov	r5, #0
	mov	r12, #0
	cmp	r3, #4
	ldrhs	r5, [r0], #4		@ the address step, or the ELSE ops
	cmp	r3, #5
	ldreq	r12, [r0], #4		@ the value step
	and	r3, r2, #15
	mov	r9, r2, lsr #16		@ the count
	cmp	r3, #7
	bhs	cond
	cmp	r3, #5
	beq	ext_done
	cmp	r3, #0
	bne	gen
	tst	r2, #0x20
	bne	fill32
	tst	r2, #0x10
	bne	fill16
fill8:	fill_loop strb
fill16:	fill_loop strh
fill32:	fill_loop str

@ Pointer writes, add, or, and: r3 the type
gen:	cmp	r3, #1
	bne	gen_read
	ldr	r1, [r1]		@ the pointer, plus the step
	add	r1, r1, r5
	mov	lr, r4
	b	gen_store
gen_read:
	tst	r2, #0x20
	ldrne	lr, [r1]
	bne	4f
	tst	r2, #0x10
	ldrneh	lr, [r1]
	ldreqb	lr, [r1]
4:	cmp	r3, #2
	addeq	lr, lr, r4
	cmp	r3, #3
	orreq	lr, lr, r4
	cmp	r3, #4
	andeq	lr, lr, r4
gen_store:
	tst	r2, #0x20
	strne	lr, [r1]
	bne	5f
	tst	r2, #0x10
	strneh	lr, [r1]
	streqb	lr, [r1]
5:	add	r1, r1, r5
	add	r4, r4, r12
	subs	r9, r9, #1
	bne	gen

ext_done:
	cmp	r10, #0
	beq	plain
	b	post_plain

@ A conditional: r3 the type, r9 its count, r5 its ELSE ops
cond:	subs	r3, r3, #8
	movmi	r3, #0			@ never: false, and nothing read
	bmi	post_cond
	tst	r2, #0x20
	ldrne	r12, [r1]
	bne	6f
	tst	r2, #0x10
	ldrneh	r12, [r1]
	ldreqb	r12, [r1]
6:	add	pc, pc, r3, lsl #4
	nop
	cmp	r12, r4			@ eq
	moveq	r3, #1
	movne	r3, #0
	b	post_cond
	cmp	r12, r4			@ ne
	movne	r3, #1
	moveq	r3, #0
	b	post_cond
	cmp	r12, r4			@ lt
	movlt	r3, #1
	movge	r3, #0
	b	post_cond
	cmp	r12, r4			@ gt
	movgt	r3, #1
	movle	r3, #0
	b	post_cond
	cmp	r12, r4			@ ult
	movlo	r3, #1
	movhs	r3, #0
	b	post_cond
	cmp	r12, r4			@ ugt
	movhi	r3, #1
	movls	r3, #0
	b	post_cond
	tst	r12, r4			@ and
	movne	r3, #1
	moveq	r3, #0
	b	post_cond
	tst	r12, r4			@ nand
	moveq	r3, #1
	movne	r3, #0
	b	post_cond

cheat_done:
	ldmfd	sp!, {r4-r11, lr}
	bx	lr
	.align 2
Cheat_blob_end:
	.size Cheat_run, Cheat_blob_end - Cheat_run
	.end
