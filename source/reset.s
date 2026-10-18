@---------------------------------------------------------------------------------
	.section	.iwram,"ax",%progbits


@;--------------------------------------------------------------
	.code 16
	.global	SoftReset_now
SoftReset_now:
	ldr r1,=SoftReset_now16
	bx  r1
	.code 32
SoftReset_now16:
	mov r0,#0x04000000		@;IME off: the clear below wipes the IRQ handler
	str r0,[r0,#0x208]
	@;Torii: the last step runs from a stub at the top of IWRAM, which the
	@;BIOS SoftReset clears, so the stub can clear all the rest of IWRAM,
	@;this code included, and the game finds IWRAM all zero
	adr r1,reset_stub
	adr r3,reset_stub_end
	mov r2,#0x03000000
	add r2,#0x7E00
copy_loop:
	ldr r0,[r1],#4
	str r0,[r2],#4
	cmp r1,r3
	blt copy_loop
	mov r0,#0x03000000
	add r0,r0,#0x7E00
	bx r0

@;Runs at 0x3007E00 (ARM): no literals, so it copies as it is. Clears
@;0x3000000-0x3007DFF, then does what SoftReset below does: 0x3007F00 = 1,
@;the restart flag at 0x3007FFA = 0 (so SWI 0 starts the cart), sp at
@;0x3007F00.
reset_stub:
	mov r2,#0x03000000
	add r3,r2,#0x7E00
	mov r0,#0
clear_iwram:
	str r0,[r2],#4
	cmp r2,r3
	blt clear_iwram
	add r2,r3,#0x100		@;0x3007F00
	mov r1,#1
	str r1,[r2]
	mov sp,r2
	str r0,[r2,#0xF8]		@;0x3007FF8-0x3007FFB, the flag at 0x3007FFA among them
	swi 0
reset_stub_end:
@----------------------------------------------
	.code 16

	.global RegisterRamReset
RegisterRamReset:
	swi		1
	bx		lr	
@----------------------------------------------	
	.global	SoftReset
SoftReset:
		ldr		r3, =0x04000208
		mov		r2, #0
		str		r2, [r3, #0]
		@;ldr		r3, =0x03007FFA
		@;mov		r0, #0x2 @;FD
		@;strb		r0, [r3, #0]
		@;swi		0x1
		ldr		r6, =0x03007F00
		mov		r5,#1
		str		r5, [r6]
		ldr		r6, =0x03007FFA
		mov		r7, #0x0
		str		r7, [r6, #0]
		ldr		r1, =0x03007f00
		mov		sp, r1
		swi		0
		
		
	@ldr		r3, =0x03007FFA		@ restart flag
	@strb	r0,[r3, #0]
	@ldr		r3, =0x04000208		@ REG_IME
	@mov		r2, #0
	@strb	r2, [r3, #0]
	@ldr		r1, =0x03007f00
	@mov		sp, r1
	@swi		1
	@swi		0

@----------------------------------------------
	.global	HardReset
HardReset:
	ldr		r3, =0x04000208
	mov		r2, #0
	str		r2, [r3, #0]
	ldr		r1, =0x3007f00
	mov		SP, r1
	swi     0x26
	
	.align
	.ltorg
SoftReset_end: