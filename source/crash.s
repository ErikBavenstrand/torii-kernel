@ The kernel's IRQ handler, in front of libgba's IntrMain (games install
@ their own). On each IRQ it counts VBlanks for the log's timestamps, then
@ checks where the IRQ interrupted the kernel: in the BIOS, IWRAM or the
@ kernel's own ROM code is normal. Anywhere else (EWRAM, VRAM, cart RAM, ROM
@ data past the code) the kernel has crashed: it saves r0-r15 and the CPSR
@ in crash_regs and calls torii_crash (source/cartlog.c), which keeps them
@ in cart RAM and restarts the kernel.
@
@ It runs on every IRQ, so it must stay short; and in IWRAM, because it also
@ runs while the ROM page points at a game being launched.

	.section .iwram,"ax",%progbits
	.arm
	.align 2
	.global torii_irq
torii_irq:
	@ The BIOS pushed r0-r3, r12 and lr (the interrupted pc + 4) on the IRQ
	@ stack; r0-r3 and r12 are ours to use.
	mov	r0, #0x04000000
	ldr	r1, [r0, #0x200]	@ IE, and IF above it
	tst	r1, #0x10000		@ IF: VBlank
	ldrne	r1, =torii_frames
	ldrne	r2, [r1]
	addne	r2, r2, #1
	strne	r2, [r1]
	@ The keys held now join input_keys_seen (input_hw.c): a tap during a
	@ write that blocks the menu for frames still reaches it
	addne	r3, r0, #0x100
	ldrneh	r3, [r3, #0x30]		@ KEYINPUT: a 0 bit is held
	ldrne	r1, =input_keys_seen
	ldrneh	r2, [r1]
	mvnne	r3, r3
	orrne	r2, r2, r3
	strneh	r2, [r1]

	ldr	r0, [sp, #20]
	sub	r0, r0, #4		@ where the IRQ came in
	cmp	r0, #0x4000		@ the BIOS: SWIs, such as VBlankIntrWait
	bcc	normal
	sub	r1, r0, #0x03000000	@ IWRAM
	cmp	r1, #0x8000
	bcc	normal
	sub	r1, r0, #0x08000000	@ the kernel's code in ROM
	ldr	r2, =__rom_end__ - 0x08000000
	cmp	r1, r2
	bcc	normal

	@ A crash. IRQs stay off from here on (we're in IRQ mode).
	ldr	r1, =crash_regs
	ldmia	sp, {r2, r3, r12}	@ r0-r2
	stmia	r1!, {r2, r3, r12}
	ldr	r2, [sp, #12]		@ r3
	str	r2, [r1], #4
	stmia	r1!, {r4-r11}
	ldr	r2, [sp, #16]		@ r12
	str	r2, [r1], #4
	mrs	r3, spsr
	mrs	r2, cpsr
	msr	cpsr_c, #0xDF		@ system mode, where the kernel runs: its sp and lr
	str	sp, [r1], #4
	str	lr, [r1], #4
	msr	cpsr_c, r2		@ back to IRQ mode
	str	r0, [r1], #4		@ pc
	str	r3, [r1]		@ cpsr
	@ A stack of its own: the IRQ stack is small, and the kernel's is saved.
	ldr	sp, =crash_stack + 512
	ldr	r0, =crash_regs
	ldr	r1, =torii_crash
	bx	r1

normal:
	ldr	pc, =IntrMain
	.pool
