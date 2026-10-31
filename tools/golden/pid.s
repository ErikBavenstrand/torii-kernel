@ New Pokémon's personality for the Pokémon cheats: shiny, nature and gender
@ together. CreateBoxMon's player-OT branch (wild, gift and egg Pokémon; not
@ trainers') calls it where it read the OT ID (0x08067C74); it returns the OT
@ ID in r1 as that code did, and draws a personality that keeps or sets the
@ nature (personality % 25), keeps or sets the gender byte (its low byte),
@ and is shiny (TID ^ SID ^ high ^ low < 8) when asked. Each cheat sets its
@ switch in free ROM, 0xFFFF (unpatched) meaning off:
@   SETTINGS+0 shiny: 1   +2 nature: 0-24   +4 gender byte: 0x00 female, 0xFF male
@ Lives over Unused_AdjustBgMosaic after the walls stub (walls.s).
@ Build: as -mcpu=arm7tdmi pid.s -o pid.o && ld -T pid.ld pid.o -o pid.elf && objcopy -O binary -j .stub pid.elf pid.bin
@ (emerald.py writes the hook, a bl and 8 nops, itself)
	.thumb
	.equ	SETTINGS, 0x08F1F000
	.section .stub, "ax"
	.global	pid
	.thumb_func
pid:
	push	{r4, r5, r6, r7, lr}
	sub	sp, #12
	ldrh	r4, [r2, #10]		@ TID
	ldrh	r5, [r2, #12]		@ SID
	lsl	r5, r5, #16
	orr	r5, r4			@ OT ID
	ldrh	r0, [r2, #12]
	eor	r4, r0			@ TID ^ SID
	ldr	r6, =SETTINGS
	ldr	r3, =0xFFFF
	ldrh	r0, [r6, #2]		@ nature: the setting, or the personality's
	cmp	r0, r3
	bne	1f
	ldrh	r0, [r7, #2]
	ldrh	r1, [r7, #0]
	bl	mod25
1:	str	r0, [sp, #0]
	ldrh	r0, [r6, #4]		@ gender byte: the setting, or the personality's
	cmp	r0, r3
	bne	2f
	ldrb	r0, [r7, #0]
2:	str	r0, [sp, #4]
	ldrh	r0, [r6, #0]		@ shiny
	str	r0, [sp, #8]
	cmp	r0, #1
	beq	again
	ldrh	r0, [r6, #2]
	cmp	r0, r3
	bne	again
	ldrh	r0, [r6, #4]
	cmp	r0, r3
	beq	done			@ nothing asked: the game's own personality
again:
	bl	Random
	mov	r1, #0xFF
	bic	r0, r1
	ldr	r1, [sp, #4]
	orr	r0, r1
	mov	r6, r0			@ low half: random, the gender byte at its bottom
	ldr	r0, [sp, #8]
	cmp	r0, #1
	bne	3f
	bl	Random
	mov	r1, #7
	and	r0, r1
	eor	r0, r4
	eor	r0, r6			@ high half: shiny with this low half
	b	4f
3:	bl	Random			@ high half: random
4:	lsl	r1, r0, #16
	orr	r1, r6
	str	r1, [r7, #0]		@ the personality, as SetBoxMonData stored it
	mov	r1, r6
	bl	mod25
	ldr	r1, [sp, #0]
	cmp	r0, r1
	bne	again
done:
	mov	r1, r5
	add	sp, #12
	pop	{r4, r5, r6, r7}
	pop	{r0}
	bx	r0

@ (r0 << 16 | r1) % 25, for halves under 0x10000: ((r0 % 25) * 11 + r1) % 25
	.thumb_func
mod25:
	push	{r4, lr}
	mov	r4, r1
	mov	r1, #25
	swi	6			@ BIOS Div: r1 = r0 % 25
	mov	r0, #11
	mul	r0, r1
	add	r0, r4
	mov	r1, #25
	swi	6
	mov	r0, r1
	pop	{r4}
	pop	{r1}
	bx	r1
	.pool

	.section .hook, "ax"
	bl	pid			@ at 0x08067C74, in place of reading the OT ID
	.rept	8
	nop
	.endr

	.section .ext, "ax"
	.thumb_func
Random:
