@ Complete Pokédex for the field menu's UNLOCK entry, called by the script's
@ callnative: all 386 seen and caught (SaveBlock2's owned and seen bits and
@ SaveBlock1's two seen copies, 48 bytes of 0xFF then 0x03 each) and the
@ National Pokédex's magic byte. Position-independent: the menu blob places
@ it at any word.
@ Build: arm-none-eabi-as -mcpu=arm7tdmi dex.s -o dex.o && arm-none-eabi-objcopy -O binary dex.o dex.bin
	.thumb
	.thumb_func
dex:
	push	{r4, lr}
	ldr	r4, =0x03005D90		@ gSaveBlock2Ptr
	ldr	r4, [r4]
	mov	r0, #0xDA
	strb	r0, [r4, #0x1A]		@ pokedex.nationalMagic
	mov	r0, r4
	add	r0, #0x28		@ pokedex.owned
	bl	fill
	mov	r0, r4
	add	r0, #0x5C		@ pokedex.seen
	bl	fill
	ldr	r4, =0x03005D8C		@ gSaveBlock1Ptr
	ldr	r4, [r4]
	ldr	r0, =0x988		@ seen1
	add	r0, r4
	bl	fill
	ldr	r0, =0x3B24		@ seen2
	add	r0, r4
	bl	fill
	pop	{r4}
	pop	{r0}
	bx	r0
fill:
	mov	r1, #0xFF
	mov	r2, #48
1:	strb	r1, [r0]
	add	r0, #1
	sub	r2, #1
	bne	1b
	mov	r1, #3
	strb	r1, [r0]
	bx	lr
	.pool
