@ Walk through walls while R is held: the player's collision check only.
@ Lives over Unused_AdjustBgMosaic (0x08002170, never called); the call to
@ CheckCollision in CheckForObjectEventCollision (0x0808B0C0) comes here.
	.thumb
	.section .stub, "ax"
	.global walls, CheckCollision
	.thumb_func
walls:
	push	{r4, r5, lr}
	ldr	r4, =0x04000130
	ldrh	r4, [r4]
	lsl	r4, r4, #23		@ R (bit 8, 0 = held) into the sign
	bmi	1f
	ldr	r4, =0x02037590		@ gPlayerAvatar
	ldrb	r4, [r4, #5]		@ objectEventId
	lsl	r5, r4, #5
	lsl	r4, r4, #2
	add	r4, r4, r5		@ * 0x24
	ldr	r5, =0x02037350		@ gObjectEvents
	add	r4, r4, r5
	cmp	r4, r0
	bne	1f
	mov	r0, #0			@ COLLISION_NONE
	pop	{r4, r5, pc}
1:	bl	CheckCollision
	pop	{r4, r5, pc}
	.pool

	.section .hook, "ax"
	bl	walls

	.section .ext, "ax"
	.thumb_func
CheckCollision:
