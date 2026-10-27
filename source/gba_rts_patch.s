@;--------------------------------------------------------------------
@;Torii: the in-game code a BOOT WITH ADDON launch puts at the game's
@;trimmed ROM end (Patch_RTS_Cheat in GBApatch.c): the entry code, the IRQ
@;hook, sleep, and the stub of v0.8's in-game menu (ingame_stub), which runs
@;the overlay the kernel keeps in cart SRAM (docs/design/v0.8/ingame-menu.md,
@;source/ingame_abi.h). It is position-independent: it runs wherever the
@;game's ROM ends.
	.section   	.iwram,"ax",%progbits

	.global  RTS_ReplaceIRQ_start
	.global  RTS_ReplaceIRQ_end
	.global  RTS_Return_address_L
	.global  RTS_Sleep_key
	.global  RTS_Reset_key
	.global  RTS_Accent
	.global  RTS_Refuse_colours
	.global	 RTS_switch
	.global	 Debug_switch
	.global	 Cheat_switch

REG_BASE		= 0x4000000
REG_DISPCNT		= 0x00
REG_VCOUNT		= 0x06
REG_BLDCNT		= 0x50
REG_BLDALPHA	= 0x52
REG_SOUND1CNT_L	= 0x60
REG_SOUNDCNT_L	= 0x80
REG_SOUNDCNT_H	= 0x82
REG_SOUNDCNT_X	= 0x84
REG_DM0CNT_H	= 0xBA
REG_IE			= 0x200
REG_P1			= 0x130
REG_WAITCNT		= 0x204

@;The ABI's numbers (source/ingame_abi.h)
BK_EWRAM		= 0x20000	@;INGAME_BACKUP_EWRAM
BK_OBJ			= 0x28000	@;INGAME_BACKUP_OBJ
BK_OAM			= 0x2C000	@;INGAME_BACKUP_OAM
BK_OBJ_PAL		= 0x2C400	@;INGAME_BACKUP_OBJ_PAL
SRAM_IMAGE		= 0xB0000	@;INGAME_SRAM_IMAGE
IMAGE_MAGIC		= 0x31564F54	@;INGAME_IMAGE_MAGIC
EWRAM_BASE		= 0x02000000	@;INGAME_EWRAM
WINDOW_MAX		= 0x6000	@;INGAME_WINDOW_MAX
OBJ_VRAM		= 0x06014000	@;INGAME_OBJ_VRAM
OBJ_MAX			= 0x4000	@;INGAME_OBJ_MAX
ACT_RESUME		= 0		@;INGAME_ACT_*
ACT_SAVE		= 1
ACT_LOAD		= 2
ACT_RESET		= 3
ACT_SLEEP		= 4
ACT_TORII		= 5
ACT_DEBUG		= 6
CAN_STATES		= 1		@;INGAME_CAN_*
CAN_CHEATS		= 2
CAN_DEBUG		= 4
NOTICE_DEBUG	= 0x100
@;The stub's and the overlay's wait states (Torii, after v0.8's black screen
@;on the real cart): SRAM at 8 cycles, the slowest, as games with a flash
@;chip read and write it; ROM (the game's PSRAM, where the stub runs) at
@;WS0 3/1 with prefetch, as nearly every commercial game runs in game mode,
@;never faster (v0.8 had the kernel menu's 2/1, proven in kernel mode
@;only). The cart's registers at the BIOS's.
WAITCNT_STUB	= 0x4017
REFUSED_AT		= 0x1C00	@;CARTLOG_INGAME_REFUSED's offset in page 0xF0
REFUSED_PAGE	= 0xF0
REFUSED_MAGIC0	= 0x524E4954	@;INGAME_REFUSED_MAGIC0
REFUSED_MAGIC1	= 0x32444546
REFUSED_NO_IMAGE = 1		@;INGAME_REFUSED_*
REFUSED_BACKUP	= 2
REFUSED_IMAGE	= 3
REFUSED_SIZES	= 4
ACTS			= 7		@;INGAME_ACTS

@;The spend area (spend_0x80): the hook keeps r4-r11, sp, lr and SPSR at
@;0x00-0x2F, SAVE the system sp and lr at 0x30; the stub keeps these
SP_LR			= 0x38		@;its return into the hook
SP_SP			= 0x3C		@;the IRQ stack while the overlay runs on its own
SP_IME			= 0x40		@;halfwords: the game's registers
SP_WAITCNT		= 0x42
SP_SNDL			= 0x44		@;SOUNDCNT_L, then SOUNDCNT_H: SAVE writes the pair
SP_SNDH			= 0x46
SP_DMA			= 0x48		@;DMA0-3 CNT_H
SP_DISPCNT		= 0x50
SP_BLDCNT		= 0x52
SP_BLDALPHA		= 0x54
SP_PAL0			= 0x56		@;the backdrop colour (BG palette 0)
SP_ACT			= 0x58		@;the overlay's answer
SP_VCOUNT		= 0x5C		@;halfwords: VCOUNT at the stub's start, the backup's tries
SP_TRIES		= 0x5E
SP_HDR			= 0x60		@;the image header's 32 bytes (to 0x7F); a refusal's record
SP_MISS			= 0x70		@;sram_copy's compare: the first difference (struct ingame_refused's at)

@;The save-state buffer (INGAME_SRAM_STATE): the old menu's layout, which
@;the old /RTS files have, and v0.8's tag and flag at its end
ST_EWRAM		= 0x40000
ST_IWRAM		= 0x80000
ST_PAL			= 0x88000
ST_VRAM			= 0x90000
ST_OAM			= 0xA8000
ST_SPEND		= 0xA8400	@;the spend area, the system sp and lr in it
ST_IO			= 0xA9000
ST_FLAG			= 0xAFFD0	@;INGAME_SRAM_STATE_MARK: Torii's, never EZ-Flash's
							@;flag at 0xAFFF0, which the FPGA's write-back watches
TAG_PAGE		= 0xAF		@;INGAME_SRAM_STATE_TAG's page; its autoload byte at 0xFE6 in it
AUTOLOAD		= 0x7A000000	@;at 0x3007FE4: load the state at the next VBlank

@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	.arm
RTS_ReplaceIRQ_start:
	MOV             R0, #0x4000000
	ADR             R1, RTS_irq
	STR             R1, [R0,#-4] @; 0x3FFFFFC = RTS_irq;
	B				autoload_entry	@;Torii (v0.8); then the game, as the old code went on
	NOP								@;the return address and spend_0x80 where the tools look
	.align
RTS_Return_address_L:
	.word 0x12345678	@;return address need modify
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
spend_0x80:
	.word 0x0203FE00   @; default
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
@;Torii (v0.8): a launch into a save state. The kernel set the state tag's
@;autoload byte in cart SRAM (INGAME_SRAM_STATE_TAG); the game's start puts
@;AUTOLOAD at IWRAM 0x3007FE4 (the cheats' CheatRAN word), and the hook loads
@;the state at the first VBlank it sees, clearing both. A Reset game finds
@;the byte cleared. r0-r3 and lr are free at the game's start.
autoload_entry:
	mov		r0,#TAG_PAGE
	bl		SetRampage
	mov		r1,#0x0E000000
	orr		r1,r1,#0xF00			@;the byte's place in page TAG_PAGE's window
	orr		r1,r1,#0xE6
	ldrb	r12,[r1]			@;SetRampage keeps r12
	mov		r0,#0
	bl		SetRampage
	cmp		r12,#1
	mov		r0,#0x4000000
	moveq	r3,#AUTOLOAD
	streq	r3,[r0,#-0x1C]
	ldr		r0,RTS_Return_address_L
	bx		r0
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
RTS_irq:
	LDR			R1, [R0,#0x200]
	TST			R1, #0x10000
	TSTEQ		R1, #0x10000000
	LDREQ		PC, [R0,#-0xC]			@;old_interrupt_handler

	ldr			r1,[r0,#-0x1C]		@;Torii: a launch into a save state
	cmp			r1,#AUTOLOAD
	beq			autoload_now

	ldr 		r2,[r0,#REG_P1]
autoload_back:

	@; Torii: a hotkey is the mask of its keys (settings_hotkeys), and matches when
	@; they are all held (0 in REG_P1), whatever else is: an extra or stuck key
	@; used to block it. HOTKEY_OFF needs all ten held, which the D-pad can't.
	@;check ingamemenu
	adr 		r3,RTS_Reset_key
	ldr 		r3,[r3]
	tst 		r2,r3
	bne			check_sleep
	@;----------------------
	adrl		r12, spend_0x80
	ldr			r12,[r12]
	stmia		r12!,{r4-r11,sp,lr} @;0x0
	mrs	 		r2,SPSR
	stmia		r12!,{r2}       @;0x4

	stmfd   SP!, {LR}  			@;0x4000000,
	bl 			ingame_stub
	ldmfd   SP!, {LR}
	cmp			r1,#ACT_SLEEP		@;Torii: Sleep, picked in the menu
	beq			sleep_now
	ldr 		r2,[r0,#REG_P1]		@; the keys now, not the menu's r2
	@;----------------------
	@;check sleep
check_sleep:
	adr 		r3,RTS_Sleep_key
	ldr 		r3,[r3]
	tst 		r2,r3
	beq 		sleep_now

	@;check cheat. Torii: the word at CheatONOFF is 1 for CHEAT OFF, anything
	@;else on: the BIOS's soft reset clears it, so a game starts with its
	@;picked cheats running (v0.7); the in-game menu's Cheats row stops them.
	@;They run through the interpreter (cheatrun.s) that Cheat_switch says is
	@;at that offset from RTS_ReplaceIRQ_start: 0, no cheats.
	@;They run once per VBlank: on the first entry that sees the VBlank IRQ
	@;pending and enabled (IE & IF bit 0), then not again until the game has
	@;acknowledged it. The word after CheatONOFF (CheatRAN) says they ran; the
	@;hook calls the game's handler as a subroutine and clears it once that
	@;returns with IF's VBlank bit clear. A game whose handler serves one IRQ
	@;an entry (HBlank before VBlank, as pokeemerald's does) enters again with
	@;VBlank still pending: the cheats don't run twice, and cost that entry
	@;nothing, so a heavy set can't keep it from reaching VBlank. A nested
	@;entry (a handler that re-enables IRQs) keeps its state on the IRQ stack.
	@;A clear of IWRAM over CheatRAN costs or adds one run at most.
	adrl		r2,Cheat_switch
	ldr			r2,[r2]
	cmp			r2,#0
	beq			nocheat
	stmfd		sp!,{lr}			@;the BIOS's return
	ldr			r1,[r0,#0x200]
	and			r1,r1,r1,lsr #16	@;IE & IF
	tst			r1,#1
	beq			cheat_game
	@;r0 - 0x1C and - 0x20 are IWRAM's 0x3007FE4 (CheatRAN) and 0x3007FE0
	@;(CheatONOFF) through its mirror, as the BIOS reaches 0x3007FFC
	ldr			r1,[r0,#-0x1C]
	cmp			r1,#0
	bne			cheat_game
	mov			r1,#1
	str			r1,[r0,#-0x1C]
	ldr 		r1,[r0,#-0x20]
	cmp 		r1,#1
	beq 		cheat_game
	adrl		r3,RTS_ReplaceIRQ_start
	add			r3,r3,r2
	mov			lr,pc
	mov			pc,r3
	mov			r0,#0x4000000
cheat_game:
	mov			lr,pc
	ldr 		pc,[r0,#-(0x04000000-0x03FFFFF4)] @;the game's handler, as the BIOS calls it
	mov			r0,#0x4000000
	ldr			r1,[r0,#0x200]
	tst			r1,#0x10000			@;IF's VBlank
	moveq		r1,#0
	streq		r1,[r0,#-0x1C]		@;acknowledged: the next VBlank runs them
	ldmfd		sp!,{pc}
nocheat:
	ldr 		pc,[r0,#-(0x04000000-0x03FFFFF4)] @;to normal IRQ routine
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
@;Back to Torii: the kernel's ROM page, then a soft reset into it
reset_now:
	bl safe_stacks
	adr r1,reset_code
	adr r3,reset_end
	mov r2,#0x02000000
copy_loop:
	ldr r0,[r1],#4
	str r0,[r2],#4
	cmp r1,r3
	blt copy_loop
	mov r0,#0x02000000
	add r0,r0,#1
	bx r0
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
RTS_Sleep_key:
	.word 0x304 @;L+R+select
RTS_Reset_key:
	.word 0x308 @;L+R+Start
RTS_Accent:		@;Torii: the theme's accent (RGB15), shown as a refusal goes back to Torii
	.word 0x001F
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	.align
	.thumb
reset_code:
	mov r0,#0x20
	lsl r3,r0,#22 @;#0x8000000 r3
	lsl r0,r0,#12 @;#0x0020000
	add r4,r3,r0  @;#0x8020000 r4
	add r5,r4,r0  @;#0x8040000 r5
	lsl r1,r0,#8  @;#0x2000000
	add r2,r3,r1  @;#0xa000000
	lsr r1,r3,#4  @;#0x0800000
	sub r6,r2,r1  @;#0x9800000
	lsr r1,r1,#4  @;#0x0080000
	add r6,r6,r1  @;#0x9880000 r6
	sub r2,r2,r0  @;#0x9fe0000 r2
	sub r7,r2,r0  @;#0x9fc0000 r7

	mov r0,#210
	lsl r0,r0,#8  @;0xd200 r0
	mov r1,#21
	lsl r1,r1,#8  @;0x1500 r1

	strh r0,[r2]
	strh r1,[r3]
	strh r0,[r4]
	strh r1,[r5]

	lsr r0,r3,#12 @;#0x0008000 r0
	add r0,#2 		@;#0x0008002 r0

	strh r0,[r6]
	strh r1,[r7]

	lsl r1,r0,#11 @;#0x4000000
	sub r1,r1,#8  @;#0x3FFFFFA
	mov r0,#0xfc  @;#252 r0
	str r0,[r1]   @;#0x3FFFFFA (mirror of #0x3007FFA
	swi 0x01
	swi 0x00
	.align
reset_end:
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
	.arm
sleep_now:
	stmfd sp!,{r4-r11,lr}
	add r1,r0,#REG_SOUND1CNT_L
	@;copy and push 32 bytes
	ldmia r1!,{r2-r9}
	stmfd sp!,{r2-r9}
	@;copy and push 32 bytes
	ldmia r1!,{r2-r9}
	stmfd sp!,{r2-r9}
	@;r3 = contents of REG_SOUND3CNT_X

	@;save old io values
	add r1,r0,#REG_IE
	ldrh r4,[r1]
	ldr r5,[r0,#REG_P1]
	ldrh r6,[r0,#REG_DISPCNT]

	@;enable ints on Keypad only, every IRQ flag acknowledged
	ldr r1,=0xFFFF1000
	str r1,[r0,#REG_IE]
	@;KEYCNT: the IRQ (bit 14) when all of START and SELECT are held (AND,
	@;bit 15). Torii: the unused bits 10-13 clear, as GBATEK has them
	adr r2,RTS_Wakeup_key
	ldr r2,[r2]
	mvn r2,r2
	mov r2,r2,lsl #22
	mov r2,r2,lsr #6
	orr r1,r2,#0xC0000000
	str r1,[r0,#REG_P1]
	strh r0,[r0,#REG_SOUNDCNT_X]	@;sound off
	orr r1,r6,#0x80
	strh r1,[r0,#REG_DISPCNT]			@;LCD off

	swi 0x030000

	@;Every key up before the game goes on. Torii: START and SELECT too
	@;(the old loop waited only for the others, so the game saw the wake
	@;keys: Pokémon's START menu opened on waking)
	mov r0,#REG_BASE
loop:
	ldr r1,[r0,#REG_P1]
	mvn r1,r1
	movs r1,r1,lsl #22
	bne loop

	@;VCOUNT 159, 160, 159, 160, 159 in turn
	mov r7,#5
	mov r3,#159
spin2:
	ldrh r1,[r0,#REG_VCOUNT]
	cmp r1,r3
	bne spin2
	eor r3,r3,#(159^160)
	subs r7,r7,#1
	bne spin2

	@;restore interrupts
	add r1,r0,#REG_IE
	strh r4,[r1]
	@;restore joystick interrupt
	str r5,[r0,#REG_P1]
	mov r4,#0x1000 @;clear the damn joystick interrupt
	strh r4,[r1,#2]

	@;restore screen
	strh r6,[r0,#REG_DISPCNT]
	ldmfd sp!,{r2-r9}
	@;restore sound state
	str r3,[r0,#REG_SOUNDCNT_X]
	add r1,r0,#0x80
	stmia r1!,{r2-r9}
	add r1,r0,#0x60
	ldmfd sp!,{r2-r9}
	stmia r1!,{r2-r9}
	ldmfd sp!,{r4-r11,lr}
	@;spin until VCOUNT==160, triggers next vblank
spin3:
	ldrh r1,[r0,#REG_VCOUNT]
	cmp r1,#160
	bne spin3  @<insert ytmnd cliche here>
	@;all done!
	ldr pc,[r0,#-(0x04000000-0x03FFFFF4)] @to IRQ routine
	.align
RTS_Wakeup_key:
	.word 0x3F3 @;start and select
	.ltorg
@;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;;
@;Torii (v0.8): the in-game menu's stub. Called from the hook on the menu
@;key, in IRQ mode with r0 = 0x4000000 and the game's r4-r11 in the spend
@;area. It freezes the game (IME, DMA and the sound mixer off), backs up the
@;EWRAM window the overlay takes, OAM and the OBJ palette to cart SRAM,
@;loads the overlay from SRAM and runs it on a stack at the window's end. Its
@;answer (INGAME_ACT_*) decides what follows once everything is back: the
@;game again (r1 = 0, or ACT_SLEEP for the hook's sleep), SAVE or LOAD as
@;the old menu did them, a restart of the game, Back to Torii, or a DEBUG
@;snapshot and the menu again. Returns r0 = 0x4000000, r4-r11 the game's.
@;It needs the IRQ stack only for sram_copy's 44 bytes.
ingame_stub:
	adrl	r12,spend_0x80
	ldr		r12,[r12]
	str		lr,[r12,#SP_LR]
	mov		r9,#0				@;the overlay's notices
ingame_open:
	mov		r11,r12				@;r11: the spend area (the overlay keeps r4-r11)
	mov		r10,#REG_BASE		@;r10: I/O
	ldrh	r1,[r10,#REG_VCOUNT]	@;where the frame was, for a refusal's record
	strh	r1,[r11,#SP_VCOUNT]
	add		r8,r10,#0x200
	ldrh	r1,[r8,#8]			@;IME
	strh	r1,[r11,#SP_IME]
	mov		r1,#0
	strh	r1,[r8,#8]
	strh	r1,[r11,#SP_TRIES]
	ldrh	r1,[r8,#4]			@;WAITCNT
	strh	r1,[r11,#SP_WAITCNT]
	ldr		r1,=WAITCNT_STUB
	strh	r1,[r8,#4]
	@;Sound: the mixer's outputs off. SOUNDCNT_X's master switch stays on:
	@;turning it off resets the PSG channels, the noise after the old menu.
	ldrh	r1,[r10,#REG_SOUNDCNT_L]
	strh	r1,[r11,#SP_SNDL]
	ldrh	r2,[r10,#REG_SOUNDCNT_H]
	strh	r2,[r11,#SP_SNDH]
	mov		r1,#0
	strh	r1,[r10,#REG_SOUNDCNT_L]
	bic		r2,r2,#0x3300		@;DMA sound A and B to neither side
	bic		r2,r2,#0x8800		@;and no FIFO reset
	strh	r2,[r10,#REG_SOUNDCNT_H]
	@;DMA0-3: kept, then stopped
	add		r2,r10,#REG_DM0CNT_H
	add		r3,r11,#SP_DMA
	mov		r4,#4
1:	ldrh	r1,[r2]
	strh	r1,[r3],#2
	mov		r1,#0
	strh	r1,[r2],#0xC
	subs	r4,r4,#1
	bne		1b
	@;and stopped again on the next scanline (Torii, after Pokémon Emerald's
	@;refusal on the real cart, whose scanline effects run on HBlank DMA):
	@;a transfer its timing had already started may have run past the first
	@;stop, which nothing in mGBA models
	ldrh	r3,[r10,#REG_VCOUNT]
1:	ldrh	r1,[r10,#REG_VCOUNT]
	cmp		r1,r3
	beq		1b
	mov		r1,#0
	sub		r2,r2,#0x30			@;DMA0CNT_H again
	strh	r1,[r2]
	strh	r1,[r2,#0xC]
	strh	r1,[r2,#0x18]
	strh	r1,[r2,#0x24]
	ldrh	r1,[r10,#REG_DISPCNT]
	strh	r1,[r11,#SP_DISPCNT]
	ldrh	r1,[r10,#REG_BLDCNT]
	strh	r1,[r11,#SP_BLDCNT]
	ldrh	r1,[r10,#REG_BLDALPHA]
	strh	r1,[r11,#SP_BLDALPHA]
	mov		r2,#0x05000000		@;the backdrop, which a refusal's screen takes
	ldrh	r1,[r2]
	strh	r1,[r11,#SP_PAL0]
	@;The overlay's header, from cart SRAM: a launch from this kernel wrote it
	ldr		r0,=SRAM_IMAGE
	add		r1,r11,#SP_HDR
	mov		r2,#32
	mov		r3,#0
	bl		sram_copy
	mov		r7,#REFUSED_NO_IMAGE
	mov		r6,#0
	ldr		r1,[r11,#SP_HDR]
	ldr		r2,=IMAGE_MAGIC
	cmp		r1,r2
	bne		refuse_header
	mov		r7,#REFUSED_SIZES
	ldr		r4,[r11,#SP_HDR+12]	@;window_bytes
	ldr		r5,[r11,#SP_HDR+8]	@;image_bytes
	cmp		r4,#WINDOW_MAX
	bhi		refuse_header
	cmp		r5,r4
	bhi		refuse_header
	cmp		r5,#32
	bls		refuse_header
	tst		r4,#3
	tsteq	r5,#3
	bne		refuse_header
	@;The game's EWRAM window, OAM and OBJ palette to SRAM, each read back
	@;before anything of the game's is changed: if cart SRAM didn't keep
	@;them, the game goes on untouched (Torii: v0.8's menu on the real cart).
	@;Copies that don't read back are written and read once more.
bk_try:
	ldrh	r1,[r11,#SP_TRIES]
	add		r1,r1,#1
	strh	r1,[r11,#SP_TRIES]
	adrl	r8,bk_list
	mov		r7,#1
	bl		sram_list
	adrl	r8,bk_list
	mov		r7,#2
	bl		sram_list
	movs	r6,r0				@;the copy that differed
	beq		bk_ok
	ldrh	r1,[r11,#SP_TRIES]
	cmp		r1,#2
	blo		bk_try
	mov		r7,#REFUSED_BACKUP
	b		ingame_refuse
bk_ok:
	@;The overlay in, checked against the header's check word
	@;(ingame_image_check); a copy that doesn't check gives the window back
	ldr		r0,=SRAM_IMAGE
	mov		r1,#EWRAM_BASE
	mov		r2,r5
	mov		r3,#0
	bl		sram_copy
	add		r1,r5,#EWRAM_BASE		@;r1 the end, r2 a word
	mov		r2,#EWRAM_BASE
	add		r2,r2,#32
	mov		r0,#0
1:	ldr		r3,[r2],#4
	add		r0,r3,r0,ror #27
	cmp		r2,r1
	blo		1b
	ldr		r1,[r11,#SP_HDR+28]
	cmp		r0,r1
	beq		ingame_run
	str		r1,[r11,#SP_MISS+4]	@;the record's: the check word, the one computed
	str		r0,[r11,#SP_MISS+8]
	mov		r1,#0
	str		r1,[r11,#SP_MISS]
	ldr		r0,=BK_EWRAM
	mov		r1,#EWRAM_BASE
	mov		r2,r4
	mov		r3,#0
	bl		sram_copy
	mov		r7,#REFUSED_IMAGE
	mov		r6,#0
	b		ingame_refuse
ingame_run:							@;and its call on its own stack at the window's end
	str		sp,[r11,#SP_SP]
	add		sp,r4,#EWRAM_BASE
	adrl	r2,RTS_switch
	ldr		r1,[r2]
	cmp		r1,#1
	orreq	r9,r9,#CAN_STATES
	ldr		r1,[r2,#Cheat_switch-RTS_switch]
	cmp		r1,#0
	orrne	r9,r9,#CAN_CHEATS
	ldr		r1,[r2,#Debug_switch-RTS_switch]
	cmp		r1,#0
	orrne	r9,r9,#CAN_DEBUG
	adrl	r0,sram_copy
	stmfd	sp!,{r0,r9}			@;struct ingame_call
	mov		r0,sp
	ldr		r12,[r11,#SP_HDR+16]	@;the entry, Thumb
	mov		lr,pc
	bx		r12
	ldr		sp,[r11,#SP_SP]
	cmp		r0,#ACTS			@;an answer it can't have given: Resume
	movhs	r0,#ACT_RESUME
	str		r0,[r11,#SP_ACT]
	@;Everything back: the OBJ tiles the overlay drew over (its header says
	@;how many), the OBJ palette, OAM, the display, then the window
	ldr		r2,=EWRAM_BASE+20
	ldr		r2,[r2]
	cmp		r2,#OBJ_MAX
	movhi	r2,#OBJ_MAX
	ldr		r0,=BK_OBJ
	ldr		r1,=OBJ_VRAM
	mov		r3,#0
	bl		sram_copy
	adrl	r8,bk_list+12		@;OAM and the OBJ palette
	mov		r7,#0
	bl		sram_list
	ldrh	r1,[r11,#SP_BLDCNT]
	strh	r1,[r10,#REG_BLDCNT]
	ldrh	r1,[r11,#SP_BLDALPHA]
	strh	r1,[r10,#REG_BLDALPHA]
	ldrh	r1,[r11,#SP_DISPCNT]
	strh	r1,[r10,#REG_DISPCNT]
	ldr		r0,=BK_EWRAM+32		@;the header's 32 bytes last: while the
	ldr		r1,=EWRAM_BASE+32	@;window comes back its magic says the
	ldr		r2,[r11,#SP_HDR+12]	@;overlay is still there (hook_watch.lua)
	sub		r2,r2,#32
	mov		r3,#0
	bl		sram_copy
	ldr		r0,=BK_EWRAM
	mov		r1,#EWRAM_BASE
	mov		r2,#32
	mov		r3,#0
	bl		sram_copy
	b		ingame_back
refuse_header:						@;the record's: the header's magic and sizes as read
	ldr		r1,[r11,#SP_HDR]
	ldr		r2,[r11,#SP_HDR+8]
	ldr		r3,[r11,#SP_HDR+12]
	add		r0,r11,#SP_MISS
	stmia	r0,{r1,r2,r3}
@;The menu not opened (r7 the reason, r6 its detail, SP_MISS what the check
@;saw): a record for the kernel's next boot (struct ingame_refused, in page
@;0xF0), then the game as it was. Its header's place in the spend area
@;holds the record.
ingame_refuse:
	ldr		r1,=REFUSED_MAGIC0
	ldr		r2,=REFUSED_MAGIC1
	ldrh	r3,[r11,#SP_WAITCNT]
	orr		r3,r7,r3,lsl #16
	add		r0,r11,#SP_HDR
	stmia	r0,{r1,r2,r3,r6}
	ldr		r2,[r11,#SP_VCOUNT]	@;extra: VCOUNT, the backup's tries
	str		r2,[r11,#SP_HDR+28]
	ldr		r0,=(REFUSED_PAGE<<12)+REFUSED_AT
	add		r1,r11,#SP_HDR
	mov		r2,#32
	mov		r3,#1
	bl		sram_copy
	@;The screen the reason's colour (INGAME_REFUSED_COLOURS: the backdrop
	@;alone, the game's own screen stays in VRAM as it is; Torii, so the
	@;owner can tell why without a log) until a key answers, the way back
	@;to Torii that needs no cart SRAM (Pokémon Emerald's refusal on the
	@;real cart left only a power-off, which lost the record): A gives the
	@;game back; B, or the menu keys held for 2 s, shows the theme's accent
	@;and resets into Torii. A and B count only once pressed while it shows,
	@;so a menu key made of them doesn't answer at once.
	cmp		r7,#REFUSED_BACKUP	@;ingame_refused_colour
	addeq	r1,r6,#4
	movne	r1,r7
	adrl	r2,RTS_Refuse_colours
	add		r2,r2,r1,lsl #1
	ldrh	r1,[r2]
	mov		r2,#0x05000000
	strh	r1,[r2]
	mov		r1,#0
	strh	r1,[r10,#REG_BLDCNT]		@;no fade on the backdrop
	strh	r1,[r10,#REG_DISPCNT]		@;mode 0, no layers
	mov		r4,#120				@;frames left of the menu keys' hold
	ldr		r5,RTS_Reset_key
	ldr		r6,[r10,#REG_P1]	@;the keys a frame before (0 held)
1:	bl		wait_frame
	ldr		r2,[r10,#REG_P1]
	bic		r3,r6,r2			@;pressed since
	mov		r6,r2
	tst		r3,#1				@;A
	movne	r1,#ACT_RESUME
	bne		refuse_answer
	tst		r3,#2				@;B
	bne		refuse_torii
	tst		r2,r5				@;the menu keys all held: Z
	movne	r4,#120
	subeqs	r4,r4,#1
	bne		1b
refuse_torii:
	ldr		r1,RTS_Accent		@;the accent as the backdrop, nothing on it
	mov		r2,#0x05000000
	strh	r1,[r2]
	strh	r2,[r10,#REG_DISPCNT]	@;0: mode 0, no layers
	mov		r4,#20
2:	bl		wait_frame
	subs	r4,r4,#1
	bne		2b
	@;The record also in IWRAM under the BIOS's 0x200 bytes
	@;(INGAME_REFUSED_IWRAM), which the soft reset and the kernel's start
	@;keep (reset_now moves the stacks the BIOS's SWIs use off it): page
	@;0xF0 may not have taken it in game mode
	add		r0,r11,#SP_HDR
	ldmia	r0,{r1-r8}
	mov		r0,#0x03000000
	add		r0,r0,#0x7E00
	stmdb	r0,{r1-r8}
	mov		r1,#ACT_TORII
refuse_answer:
	str		r1,[r11,#SP_ACT]
ingame_back:
	ldrh	r1,[r11,#SP_WAITCNT]	@;the game's WAITCNT, then the save's page
	add		r2,r10,#0x200		@;again (at the BIOS's, as page_zero sets it)
	strh	r1,[r2,#4]
	bl		page_zero
	@;Two frames' starts: the frame between them shows the game's own screen
	bl		wait_frame
	bl		wait_frame
	ldr		r1,[r11,#SP_ACT]
	cmp		r1,#ACT_SAVE
	beq		state_save
	cmp		r1,#ACT_LOAD
	beq		state_load
	cmp		r1,#ACT_RESET
	beq		game_reset
	cmp		r1,#ACT_TORII
	beq		reset_now
	cmp		r1,#ACT_DEBUG
	beq		debug_now
ingame_resume:						@;r10 I/O, r11 the spend area
	@;The game frozen and muted until every key is up: it never sees the
	@;menu keys (START opened Pokémon's menu after the stub's refusal) or
	@;the A or B that closed the menu, and a key still held can't open the
	@;menu again at the next VBlank
1:	ldr		r2,[r10,#REG_P1]
	mvn		r2,r2
	movs	r2,r2,lsl #22
	bne		1b
	ldrh	r1,[r11,#SP_DISPCNT]	@;the screen back after a refusal's colour
	strh	r1,[r10,#REG_DISPCNT]
	ldrh	r1,[r11,#SP_BLDCNT]
	strh	r1,[r10,#REG_BLDCNT]
	ldrh	r1,[r11,#SP_PAL0]
	mov		r2,#0x05000000
	strh	r1,[r2]
	bl		ingame_thaw
	ldr		r1,[r11,#SP_ACT]
	cmp		r1,#ACT_SLEEP
	movne	r1,#0
	ldr		lr,[r11,#SP_LR]
	mov		r0,#REG_BASE
	ldmia	r11,{r4-r11}
	bx		lr
RTS_Refuse_colours:					@;INGAME_REFUSED_COLOURS (ingame_abi.h)
	.hword	0x7FFF,0x7C00,0x001F,0x03E0,0x7FE0,0x001F,0x03FF,0x7C1F
	.align	2

@;The sound mixer, DMA0-3 and IME as the game had them (r10 I/O, r11 the
@;spend area); uses r1-r4
ingame_thaw:
	ldrh	r1,[r11,#SP_SNDH]
	bic		r1,r1,#0x8800
	strh	r1,[r10,#REG_SOUNDCNT_H]
	ldrh	r1,[r11,#SP_SNDL]
	strh	r1,[r10,#REG_SOUNDCNT_L]
	add		r2,r10,#REG_DM0CNT_H
	add		r3,r11,#SP_DMA
	mov		r4,#4
1:	ldrh	r1,[r3],#2
	strh	r1,[r2],#0xC
	subs	r4,r4,#1
	bne		1b
	ldrh	r1,[r11,#SP_IME]
	add		r2,r10,#0x200
	strh	r1,[r2,#8]
	bx		lr

@;Waits for the next frame's start (VCOUNT 160); r10 I/O, uses r1
wait_frame:
1:	ldrh	r1,[r10,#REG_VCOUNT]
	cmp		r1,#160
	beq		1b
2:	ldrh	r1,[r10,#REG_VCOUNT]
	cmp		r1,#160
	bne		2b
	bx		lr

@;sram_copy(sram, ram, n, to_sram): n bytes (a multiple of 4) between cart
@;SRAM at byte offset sram and word-aligned RAM, to RAM when to_sram is 0
@;(struct ingame_call's). SRAM is an 8-bit bus: bytes one at a time, at the
@;WAITCNT_STUB; the page register at the BIOS's wait states, and the
@;caller's WAITCNT back at the end. Torii: to_sram 2 compares instead, and returns r0 non-zero
@;if SRAM differs (0 otherwise, and for the other modes). Leaves the last
@;page set. Keeps r4-r11 (AAPCS: the overlay calls it) and, but in mode 2,
@;r3 (DEBUG's code relies on it); 44 bytes of stack. Mode 2 is the stub's
@;alone (r11 its spend area), and leaves the first difference at SP_MISS.
sram_copy:
	stmfd	sp!,{r4-r8,lr}
	mov		r8,#REG_BASE
	add		r8,r8,#0x200		@;r8 + 4: WAITCNT
	ldrh	r7,[r8,#4]
	stmfd	sp!,{r7}
sc_chunk:
	mov		r8,#REG_BASE		@;again: sc_cmp has had the spend area in it
	add		r8,r8,#0x200
	cmp		r2,#0
	ble		sc_done
	mov		r4,#0
	strh	r4,[r8,#4]
	stmfd	sp!,{r0-r3}
	mov		r0,r0,lsr #12		@;the page: offset >> 12
	bl		SetRampage
	ldmfd	sp!,{r0-r3}
	ldr		r4,=WAITCNT_STUB
	strh	r4,[r8,#4]
	@;This chunk: up to the 64 KB window's end
	mov		r4,r0,lsl #20
	mov		r4,r4,lsr #20		@;offset & 0xFFF
	rsb		r5,r4,#0x10000
	cmp		r5,r2
	movgt	r5,r2
	add		r6,r4,#0x0E000000
	add		r0,r0,r5
	sub		r2,r2,r5
	@;The bytes themselves in Thumb: from the cart's 16-bit bus an ARM
	@;instruction takes two fetches, and these loops run twice as fast
	cmp		r3,#1
	adrlo	r12,sc_in+1
	adreq	r12,sc_out+1
	bls		2f
	adrl	r12,sc_cmp+1
	mov		r8,r11				@;mode 2 is the stub's: r11 its spend area
2:	mov		lr,pc
	bx		r12
	b		sc_chunk
sc_done:
	cmp		r3,#3				@;sc_cmp found a difference
	moveq	r0,#1
	movne	r0,#0
	ldmfd	sp!,{r7}
	strh	r7,[r8,#4]
	ldmfd	sp!,{r4-r8,lr}
	bx		lr
	.ltorg
	@;r5 bytes (a multiple of 4, not 0) between RAM at r1 and SRAM at r6;
	@;uses r4 and r7. Four words a pass while 16 bytes are left, then a
	@;word a pass: the loop's own instructions took a sixth of the time,
	@;and the menu's opening is these copies (v0.8 P4)
	.macro	sc_in_word at
	sc_get_word \at
	stmia	r1!,{r4}
	.endm
	.macro	sc_get_word at
	ldrb	r4,[r6,#\at]
	ldrb	r7,[r6,#\at+1]
	lsl		r7,r7,#8
	orr		r4,r7
	ldrb	r7,[r6,#\at+2]
	lsl		r7,r7,#16
	orr		r4,r7
	ldrb	r7,[r6,#\at+3]
	lsl		r7,r7,#24
	orr		r4,r7
	.endm
	.macro	sc_out_word at
	ldmia	r1!,{r4}
	strb	r4,[r6,#\at]
	lsr		r4,r4,#8
	strb	r4,[r6,#\at+1]
	lsr		r4,r4,#8
	strb	r4,[r6,#\at+2]
	lsr		r4,r4,#8
	strb	r4,[r6,#\at+3]
	.endm
	.thumb
sc_in:
	cmp		r5,#16
	blt		sc_in1
	sc_in_word 0
	sc_in_word 4
	sc_in_word 8
	sc_in_word 12
	add		r6,#16
	sub		r5,#16
	bne		sc_in
	bx		lr
sc_in1:
	sc_in_word 0
	add		r6,#4
	sub		r5,#4
	bgt		sc_in1
	bx		lr
sc_out:
	cmp		r5,#16
	blt		sc_out1
	sc_out_word 0
	sc_out_word 4
	sc_out_word 8
	sc_out_word 12
	add		r6,#16
	sub		r5,#16
	bne		sc_out
	bx		lr
sc_out1:
	sc_out_word 0
	add		r6,#4
	sub		r5,#4
	bgt		sc_out1
	bx		lr
sc_cmp:								@;r3 becomes 3 at a difference; the
	sc_get_word 0				@;first one's RAM address (past it), RAM
	ldmia	r1!,{r7}			@;word and SRAM word go to the spend
	cmp		r4,r7				@;area's SP_MISS (r8)
	beq		1f
	cmp		r3,#3
	beq		1f
	mov		r3,r8
	str		r1,[r3,#SP_MISS]
	str		r7,[r3,#SP_MISS+4]
	str		r4,[r3,#SP_MISS+8]
	mov		r3,#3
1:	add		r6,#4
	sub		r5,#4
	bgt		sc_cmp
	bx		lr
	.align	2
	.arm

@;sram_list: sram_copy for each {sram offset, RAM, bytes} of the list at
@;r8 (bytes 0: r4's) until a 0, in mode r7 (sram_copy's to_sram). Returns
@;r0: 0, or in mode 2 the entry (from 1) that differed. Keeps r4, r5 and
@;r9-r11; 4 bytes of stack and sram_copy's.
sram_list:
	stmfd	sp!,{lr}
	mov		r6,#0
1:	ldmia	r8!,{r0,r1,r2}
	cmp		r0,#0
	beq		2f
	add		r6,r6,#1
	cmp		r2,#0
	moveq	r2,r4
	mov		r3,r7
	bl		sram_copy
	cmp		r0,#0
	movne	r0,r6
	beq		1b
2:	ldmfd	sp!,{lr}
	bx		lr

@;Reset game: the patched game from its start, without the kernel. As a
@;launch leaves the machine (SetRompageWithHardReset): RAM and registers
@;cleared, WAITCNT the BIOS's, then SoftReset from the cart's ROM, whose
@;page is left on the game.
game_reset:
	bl		safe_stacks
	add		r8,r10,#0x200
	mov		r1,#0
	strh	r1,[r8,#4]			@;WAITCNT
	mov		r0,#0xFF			@;EWRAM, IWRAM (not its last 0x200), palette, VRAM, OAM, SIO, sound, the rest
	swi		0x010000
	ldr		r0,=0x03007FFA
	mov		r1,#0
	strb	r1,[r0]				@;0: SoftReset starts the ROM, not EWRAM
	swi		0x000000

@;DEBUG: the snapshot with the game's registers back, then the menu again
@;with a notice
debug_now:
	bl		ingame_thaw
	adrl	r0,Debug_switch
	ldr		r0,[r0]
	adrl	r8,RTS_ReplaceIRQ_start
	add		r8,r8,r0
	mov		r4,r11
	adrl	r5,RTS_irq
	adrl	r6,SetRampage
	adrl	r7,sram_copy
	mov		lr,pc
	mov		pc,r8
	adrl	r12,spend_0x80
	ldr		r12,[r12]
	mov		r9,#NOTICE_DEBUG
	b		ingame_open
@;Torii: the System and Supervisor stacks moved to the 0x200 bytes
@;SoftReset clears, before a SWI from IRQ mode that never returns to the
@;game. The real BIOS's SWI handler runs the function in System mode on
@;the caller's System stack: Emerald's, from 0x03007E40, put the frames of
@;RegisterRamReset and SoftReset over the refusal record at 0x03007DE0
@;(and under RegisterRamReset's IWRAM clear, for Reset game). Leaves IRQ
@;mode with IRQs off; uses r0. (The handler's own 4 words go on the
@;Supervisor stack, which games leave at the BIOS's 0x03007FE0.)
safe_stacks:
	mov		r0,#0x03000000
	add		r0,r0,#0x7F00
	msr		cpsr_c,#0xDF		@;System
	mov		sp,r0
	msr		cpsr_c,#0xD2		@;IRQ
	bx		lr
@;------------------------------------------------------
SetRampage:
	ldr 	r1,=0xD200
	ldr 	r2,=0x1500
	ldr 	r3,=0x9fe0000
	strh 	r1,[r3]
	mov 	r3,#0x8000000
	strh  r2,[r3]
	add 	r3,r3,#0x20000
	strh 	r1,[r3]
	add 	r3,r3,#0x20000
	strh  r2,[r3]
	ldr 	r3,=0x9C00000
	strh  r0,[r3]
	ldr 	r3,=0x9FC0000
	strh  r2,[r3]
	bx		lr
@;------------------------------------------------------
ReadSram: @;(u32 address, u8 *data, u32 size)
	ADDS    R2, R2, R0
	SUBS    R1, R1, R0
rSram_loop1:
	CMP     R0, R2
	BNE     rSram_loop
	BX      LR
rSram_loop:
	LDRB    R4, [R0]
	LDRB    R3, [R0,#1]
	ORR		R4,R4,R3,LSL #8
	LDRB    R3, [R0,#2]
	ORR		R4,R4,R3,LSL #16
	LDRB    R3, [R0,#3]
	ORR		R4,R4,R3,LSL #24
	STR     R4, [R1,R0]
	ADDS    R0, #4
	B       rSram_loop1
	.ltorg
@;===================================================
@;SAVE, as the old menu's: the game's memory and registers into the state
@;buffer (SRAM 0x40000-0xAFFFF, the old layout), with everything already
@;back but IME, DMA and the sound mixer (r11 the spend area, which goes in
@;too). The I/O copy has DMA stopped and IME 0, as the old one had; the
@;mixer's pair is the game's. Torii (v0.8): through sram_copy's Thumb loops,
@;twice as fast as the old byte loop; the overlay wrote the state's tag
@;(its slot, not kept) and cleared the flag, which goes last.
state_save:
	adrl	r8,st_list			@;EWRAM, IWRAM, palette, VRAM, OAM
	mov		r7,#1
	bl		sram_list

	@;The system-mode sp and lr into the spend area at 0x30
	mrs	  r0,CPSR
	mov		r1, #0xDF		@; Switch to system Mode
	msr		cpsr_cf, r1
	NOP
	mov		r2,sp
	mov		r3,lr
	msr 	cpsr_cf,r0	;@return IRQ mode
	NOP
	str		r2,[r11,#0x30]
	str		r3,[r11,#0x34]
	ldr		r0,=ST_SPEND
	mov		r1,r11
	mov		r2,#0x80
	mov		r3,#1
	bl		sram_copy

	@;04000000-040003FE   I/O Registers, with the game's sound mixer
	ldr		r0,=ST_IO
	mov		r1,#0x04000000
	mov		r2,#0x400
	mov		r3,#1
	bl		sram_copy
	ldr		r0,=ST_IO+REG_SOUNDCNT_L
	add		r1,r11,#SP_SNDL
	mov		r2,#4
	mov		r3,#1
	bl		sram_copy

	@;The flag last: a state cut short has none
	ldr		r0,=ST_FLAG
	adrl	r1,S_RTS_FLAG
	mov		r2,#0x10
	mov		r3,#1
	bl		sram_copy

	bl		page_zero			@;the page register at the BIOS's
	bl		wait_frame			@;the game goes on at a frame's start, as after LOAD
	b		ingame_resume
	.ltorg

@;Z set if the state buffer's flag is there. Leaves a page set; uses r0-r3.
state_flag_ok:
	stmfd	sp!,{lr}
	ldr		r0,=ST_FLAG
	adrl	r1,S_RTS_FLAG
	mov		r2,#0x10
	mov		r3,#2
	bl		sram_copy
	cmp		r0,#0
	ldmfd	sp!,{lr}
	bx		lr

@;The page back at 0, at the BIOS's wait states (r10 I/O, r11 the spend
@;area); uses r0-r3, r8
page_zero:
	stmfd	sp!,{lr}
	add		r8,r10,#0x200
	ldrh	r0,[r8,#4]
	strh	r0,[r11,#SP_WAITCNT]
	mov		r0,#0
	strh	r0,[r8,#4]
	bl		SetRampage
	ldrh	r0,[r11,#SP_WAITCNT]
	strh	r0,[r8,#4]
	ldmfd	sp!,{lr}
	bx		lr

@;LOAD: the state buffer back into the game's memory and registers, then
@;into the game's IRQ handler with the state's registers. Torii (v0.8): only
@;with the state's flag (the old code's check was commented out, and loading
@;a blank buffer crashed); IWRAM last, by a loop that needs no stack, as it
@;loads the stack too.
state_load:
	bl		state_flag_ok
	beq		state_load_go
	bl		page_zero
	b		ingame_resume			@;none: the game goes on
state_load_go:
	adrl	r8,st_list+24		@;palette, VRAM, OAM, then EWRAM (IWRAM below)
	mov		r7,#0
	bl		sram_list
	ldr		r0,=ST_EWRAM
	mov		r1,#0x02000000
	mov		r2,#0x40000
	mov		r3,#0
	bl		sram_copy

	@;03000000-03007FFF   WRAM - On-chip Work RAM   (32 KBytes)
	mov		r1,#0
	add		r8,r10,#0x200
	strh	r1,[r8,#4]		@;the page register at the BIOS's
	mov 	r0,#ST_IWRAM>>12
	bl 		SetRampage
	mov r0,#0x0E000000
	mov r1,#0x3000000
	mov r2,#0x8000
	bl 	ReadSram
	@;The cheats as launched: the toggles a state holds are for the table
	@;of the launch it was saved in (and CheatRAN as at a game's start)
	mov		r1,#0x04000000
	mov		r2,#0
	str		r2,[r1,#-0x1C]
	str		r2,[r1,#-0x18]
	str		r2,[r1,#-0x14]

	@;-------------------------------------
	mov		r0,#ST_IO>>12		@;the I/O copy and the spend area's, in page 0xA9's window
	bl		SetRampage
	mov r10,#0x4000000
	mov r11,#0x0E000000

	adr r9,register_list
register_list_loop:
	ldrh r2,[r9],#2
	cmp r2 ,#0xFF00
	beq register_list_end

	add r1,r11,r2	@;the register's two bytes from the I/O copy
	ldrb r3,[r1]
	ldrb r4,[r1,#1]
	orr r3,r3,r4,lsl #8
	strh r3,[r10,r2]
	b 	register_list_loop
register_list_end:
	add		r8,r10,#0x200
	ldrh	r9,[r8,#4]		@;the state's; the page register at the BIOS's
	mov		r1,#0
	add		r8,r10,#0x200
	strh	r1,[r8,#4]

	ldr r0,=0x4000202   @;0x4000202
	mov r1,#0
	strh r1,[r0]
	mov r1,#1			@;Torii: IME on; the state holds the menu's IME = 0
	strh r1,[r0,#6]

	mov		r0,#ST_SPEND>>12
	bl		SetRampage
	ldr 	r0,=0x0E000000+(ST_SPEND & 0xFFF)
	adrl 	r1, spend_0x80 @;temp buff
	ldr		r1,[r1]
	mov 	r2,#0x80
	bl 		ReadSram

	mrs	  r0,CPSR
	adrl	r7, spend_0x80
	ldr		r7,[r7]
	add		r7,#0x28      @;SPSR offset

	ldmia	r7!,{r2}			@;r7=0x2C
	msr		SPSR_cxsf,r2	@;restore SPSR_irq

	mov		r1, #0xDF		  @;Switch to systme Mode
	msr		cpsr_cf, r1
	NOP
	add		r7,#0x4					@;offset 0x30
	ldmia r7!,{r13-r14}

	msr 	cpsr_cf,r0	  @;restore IRQ
	NOP

	mov 	r0,#0x0
	bl 		SetRampage
	add		r8,r10,#0x200
	strh	r9,[r8,#4]		@;the state's, once the page is back

	@;spin until VCOUNT==160, triggers next vblank
	mov 	r0,#0x4000000
spin3_load:
	ldrh 	r1,[r0,#REG_VCOUNT]
	cmp 	r1,#160
	bne 	spin3_load

	adrl	r12, spend_0x80
	ldr		r12,[r12]
	ldmia r12!,{r4-r11,sp,lr}

	ldr 	pc,[r0,#-(0x04000000-0x03FFFFF4)] @;to normal IRQ routine
	.ltorg

@;Torii (v0.8): the launch into a save state, at the first VBlank: the
@;hook's registers kept as the menu's are, the autoload marks cleared, then
@;LOAD with the game frozen as the menu freezes it. Without a state's flag
@;the game goes on. r0 = 0x4000000.
autoload_now:
	mov		r1,#0
	str		r1,[r0,#-0x1C]
	adrl	r12,spend_0x80
	ldr		r12,[r12]
	stmia	r12,{r4-r11,sp,lr}
	mrs		r2,SPSR
	str		r2,[r12,#0x28]
	mov		r11,r12
	mov		r10,r0
	stmfd	sp!,{lr}
	bl		state_flag_ok
	bne		autoload_none
	add		r2,r10,#0x200			@;WAITCNT: the page register at the BIOS's; LOAD sets the state's
	mov		r1,#0
	strh	r1,[r2,#4]
	mov		r0,#TAG_PAGE			@;the tag's autoload byte cleared
	bl		SetRampage
	mov		r1,#0x0E000000
	orr		r1,r1,#0xF00
	orr		r1,r1,#0xE6
	mov		r2,#0
	strb	r2,[r1]
	ldmfd	sp!,{lr}
	add		r2,r10,#0x200
	mov		r1,#0
	strh	r1,[r2,#8]			@;IME
	add		r2,r10,#REG_DM0CNT_H	@;DMA0-3 stopped
	mov		r3,#4
1:	strh	r1,[r2],#0xC
	subs	r3,r3,#1
	bne		1b
	b		state_load_go
autoload_none:
	bl		page_zero
	ldmfd	sp!,{lr}
	mov		r0,#REG_BASE
	ldmia	r11,{r4-r11}
	ldr		r2,[r0,#REG_P1]
	b		autoload_back
	.ltorg
	.align
register_list:
	.hword 0x0000,0x0002,0x0004,0x0008,0x000A,0x000C,0x000E,0x0048
	.hword 0x004A,0x0050,0x0052
	.hword 0x0084
	.hword 0x0060,0x0062,0x0068,0x0070,0x0072,0x0078
	.hword 0x0080,0x0082,0x0088                 @;0x008C,0x008E,
	.hword 0x0090,0x0092,0x0094,0x0096,0x0098,0x009A,0x009C,0x009E

	.hword 0x00B8,0x00C4,0x00D0,0x00DC  @;DMA
	.hword 0x0120,0x0122,0x0124,0x0126,0x0128,0x012A,0x012C,0x0132,0x0134
	.hword 0x0140,0x0150,0x0154,0x0200,0x0204,0x0208,0xFF00
	.align
S_RTS_FLAG:		@;INGAME_STATE_MARK
	.ascii	"TORII-STATE-OK1."
	.align
@;Lists for sram_list: {sram offset, RAM, bytes (0: r4's)}, ended by 0
bk_list:							@;the stub's backups
	.word	BK_EWRAM, EWRAM_BASE, 0
	.word	BK_OAM, 0x07000000, 0x400
	.word	BK_OBJ_PAL, 0x05000200, 0x200
	.word	0
st_list:							@;the state buffer, as the old menu kept it
	.word	ST_EWRAM, 0x02000000, 0x40000
	.word	ST_IWRAM, 0x03000000, 0x8000
	.word	ST_PAL, 0x05000000, 0x400
	.word	ST_VRAM, 0x06000000, 0x18000
	.word	ST_OAM, 0x07000000, 0x400
	.word	0
RTS_switch:
	.word 0x00000000
Debug_switch:		@;Torii: DEBUG's code, from RTS_ReplaceIRQ_start; 0: no DEBUG
	.word 0x00000000
Cheat_switch:		@;Torii: the cheat interpreter, from RTS_ReplaceIRQ_start; 0: no cheats
	.word 0x00000000
	.align
RTS_ReplaceIRQ_end:

@;------------------------------------------------------
@;Torii: the in-game menu's DEBUG entry, appended to a game's patch after
@;its cheat table (or the menu, without cheats) when LOGGING is on (Patch_RTS_Cheat in GBApatch.c). It
@;writes the game's state to cart RAM, page 0xF0 at 0x1000, for the
@;kernel's next boot to log: struct cartlog_snapshot in source/cartlog.c
@;has the layout. The magic goes last, so a write cut off counts as none.
@;It reads memory and I/O only, and never selects the save chip's pages.
@;It sits at a different place in each game, so the stub passes what it
@;needs: r4 the hook's register save area (spend_0x80), r5 the hook's
@;address, r6 SetRampage, r7 sram_copy.
	.section .text.debug_blob,"ax",%progbits
	.arm
	.align 2
	.global	Debug_blob_start
	.global	Debug_blob_end
Debug_blob_start:
	stmfd	sp!,{lr}
	mrs		r0,CPSR		@;the system-mode sp and lr, where SAVE keeps them
	mov		r1,#0xDF
	msr		cpsr_cf,r1
	nop
	mov		r2,sp
	mov		r3,lr
	msr		cpsr_cf,r0
	nop
	str		r2,[r4,#0x30]
	str		r3,[r4,#0x34]

	mov		r0,#0xF0
	mov		lr,pc
	mov		pc,r6		@;SetRampage
	ldr		r0,=0x0E001000	@;no magic while the rest is written
	mov		r1,#0
	mov		r2,#16
debug_blank:
	strb	r1,[r0],#1
	subs	r2,r2,#1
	bne		debug_blank

	@;The rest through sram_copy (r7), to_sram 1 in r3, which it keeps;
	@;page 0xF0's window offset 0x1000 is cart SRAM's byte 0xF1000
	.macro	debug_copy dst, src, len
	ldr		r0,=\dst
	ldr		r1,=\src
	mov		r2,#\len
	mov		lr,pc
	mov		pc,r7		@;sram_copy
	.endm
	.macro	debug_copy_from dst, len	@;from r1
	ldr		r0,=\dst
	mov		r2,#\len
	mov		lr,pc
	mov		pc,r7
	.endm

	mov		r3,#1
	debug_copy 0xF1010, 0x080000A0, 0x20		@;the ROM header from 0xA0
	str		r5,[sp,#-4]!				@;the hook's address
	mov		r1,sp
	debug_copy_from 0xF1030, 4
	add		sp,sp,#4
	mov		r1,r4					@;r4-r11, sp, lr, SPSR, system sp and lr
	debug_copy_from 0xF1040, 0x40
	ldr		r1,[r4,#0x20]				@;the BIOS's IRQ frame: r0-r3, r12, pc + 4
	bic		r1,r1,#3
	debug_copy_from 0xF1080, 0x20
	debug_copy 0xF10A0, 0x03007FF0, 0x10		@;the game's handler, IRQ flags, the hook
	debug_copy 0xF10C0, 0x04000000, 0x60		@;I/O 0x000-0x05F
	debug_copy 0xF1120, 0x04000200, 0x10		@;IE, IF, WAITCNT, IME
	debug_copy 0xF1200, 0x03007E00, 0x200		@;IWRAM 0x3007E00-0x3007FFF
	ldr		r1,[r4,#0x30]				@;the game's stack
	bic		r1,r1,#3
	debug_copy_from 0xF1400, 0x100
	adr		r1,debug_magic
	debug_copy_from 0xF1000, 16			@;now the magic

	mov		r0,#0x00
	mov		lr,pc
	mov		pc,r6
	ldmfd	sp!,{pc}
	.ltorg
debug_magic:
	.byte	'T','O','R','I','I','-','D','E','B','U','G','I','N','F','O','1'
Debug_blob_end:
   .end
