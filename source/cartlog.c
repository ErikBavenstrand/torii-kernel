#include <stdio.h>
#include <string.h>

#include <gba_base.h>
#include <gba_interrupt.h>

#include "Ezcard_OP.h"
#include "cartlog.h"
#include "tlog.h"

extern void SoftReset_now(void);
extern void torii_irq(void);
extern u8 GAMECODE[4];
extern u32 gl_currentpage;

volatile u32 torii_frames;

// --- What the blocks in cart RAM hold ---------------------------------------

#define STATE_MAGIC "TORIIST1"
#define CRASH_MAGIC "TORIICR1"
#define SNAPSHOT_MAGIC "TORII-DEBUGINFO1"  // gba_rts_patch.s writes it last

struct cartlog_state {
	char magic[8];
	u32 boot;  // boots with LOGGING on, counting this one
	u32 phase;
	u32 detail;
	char code[4];
	char game[100];
};

#define CRASH_STACK_WORDS 64
#define CRASH_LOG_BYTES 1024
struct cartlog_crash {
	char magic[8];
	u32 regs[17];  // r0-r15, cpsr
	u32 frames;
	u32 stack_base;
	u32 stack_words;
	u32 stack[CRASH_STACK_WORDS];
	u32 log_len;
	char log[CRASH_LOG_BYTES];  // the newest log lines
};

// The in-game menu's DEBUG entry (gba_rts_patch.s, debug_save), in cart RAM
// at CARTLOG_SNAPSHOT. Its offsets are in that file too.
struct cartlog_snapshot {
	char magic[16];
	u8 header[0x20];  // 0x010: the game's ROM header from 0xA0: title, code, maker, version
	u32 irq_hook;  // 0x030: the hook's address (RTS_irq in the game's ROM)
	u32 spare[3];
	u32 regs[16];  // 0x040: at the menu key: r4-r11, IRQ sp, lr, SPSR, -, system sp, system lr
	u32 irq_frame[8];  // 0x080: the BIOS's IRQ frame: r0-r3, r12, the game's pc + 4
	u32 hook[4];  // 0x0A0: 0x3007FF0-0x3007FFF: sound buffer, the game's handler, IRQ flags, vector
	u32 spare2[4];
	u8 io[0x60];  // 0x0C0: 0x4000000-0x400005F (with the menu's own DISPCNT)
	u8 io_irq[0x10];  // 0x120: 0x4000200-0x400020F: IE, IF, WAITCNT, IME
	u8 spare3[0xD0];
	u8 iwram_top[0x200];  // 0x200: 0x3007E00-0x3007FFF: stacks and the hook
	u8 stack[0x100];  // 0x400: from the game's sp
};

u32 crash_regs[17];
u32 crash_stack[128] EWRAM_BSS;
// The crash dump or the debug info, one at a time.
static union {
	struct cartlog_crash crash;
	struct cartlog_snapshot snapshot;
} block EWRAM_BSS;
#define crash block.crash
_Static_assert(CARTLOG_CRASH + sizeof(struct cartlog_crash) <= CARTLOG_SNAPSHOT, "crash dump size");

// --- Cart RAM access ----------------------------------------------------------

// Every access sets the page, then goes back to page 0, where the kernel's
// save code expects it.
static void cart_read(u32 address, void *data, u32 size)
{
	SetRampage(CARTLOG_PAGE);
	ReadSram(address, data, size);
	SetRampage(0);
}

static void cart_write(u32 address, const void *data, u32 size)
{
	SetRampage(CARTLOG_PAGE);
	WriteSram(address, (u8 *)data, size);
	SetRampage(0);
}

// Writes a block with its magic last, so a block cut off by power loss
// doesn't count.
static void cart_write_block(u32 address, const void *data, u32 size, u32 magic_len)
{
	static const u8 blank[16];
	cart_write(address, blank, magic_len);
	cart_write(address + magic_len, (const u8 *)data + magic_len, size - magic_len);
	cart_write(address, data, magic_len);
}

static void cart_clear_magic(u32 address, u32 magic_len)
{
	static const u8 blank[16];
	cart_write(address, blank, magic_len);
}

// --- Crash capture --------------------------------------------------------------

void cartlog_install(void)
{
	INT_VECTOR = torii_irq;
}

static int IWRAM_CODE in_ram(u32 address)
{
	return (address >= 0x02000000 && address < 0x02040000) || (address >= 0x03000000 && address < 0x03008000);
}

void IWRAM_CODE torii_crash(const u32 *regs)
{
	u32 i;
	REG_IME = 0;
	SetRompage(gl_currentpage ? gl_currentpage : 0x8002);  // the kernel's ROM, for the calls below
	memset(&crash, 0, sizeof(crash));
	for (i = 0; i < 17; i++)
		crash.regs[i] = regs[i];
	crash.frames = torii_frames;
	u32 sp = regs[13] & ~3u;
	if (in_ram(sp)) {
		crash.stack_base = sp;
		for (i = 0; i < CRASH_STACK_WORDS && in_ram(sp + 4 * i); i++)
			crash.stack[i] = ((const u32 *)sp)[i];
		crash.stack_words = i;
	}
	crash.log_len = tlog_recent(crash.log, CRASH_LOG_BYTES);
	memcpy(crash.magic, CRASH_MAGIC, 8);

	// The same as cart_write_block, without calls into ROM code.
	static const u8 blank[8];
	SetRampage(CARTLOG_PAGE);
	WriteSram(CARTLOG_CRASH, (u8 *)blank, 8);
	WriteSram(CARTLOG_CRASH + 8, (u8 *)&crash + 8, sizeof(crash) - 8);
	WriteSram(CARTLOG_CRASH, (u8 *)&crash, 8);
	SetRampage(0);
	SoftReset_now();
	for (;;)
		;
}

// --- The boot report ------------------------------------------------------------

static void report_crash(void)
{
	cart_read(CARTLOG_CRASH, &crash, sizeof(crash));
	if (memcmp(crash.magic, CRASH_MAGIC, 8) != 0)
		return;
	if (tlog_level == TLOG_OFF)  // with logging on, this boot's line is there already
		tlog_marker("a crash report", 1);
	const u32 *r = crash.regs;
	tlog_force(TLOG_ERRORS, "crash: the kernel crashed %lu frames after boot: pc %08lX outside its code",
		   (unsigned long)crash.frames, (unsigned long)r[15]);
	for (u32 i = 0; i < 16; i += 4)
		tlog_force(TLOG_ERRORS, "crash: r%-2lu %08lX %08lX %08lX %08lX", (unsigned long)i, (unsigned long)r[i],
			   (unsigned long)r[i + 1], (unsigned long)r[i + 2], (unsigned long)r[i + 3]);
	tlog_force(TLOG_ERRORS, "crash: cpsr %08lX (%s)", (unsigned long)r[16], (r[16] & 0x20) ? "thumb" : "arm");
	tlog_force(TLOG_ERRORS, "crash: lr %08lX", (unsigned long)r[14]);
	tlog_force(TLOG_ERRORS, "crash: pc %08lX", (unsigned long)r[15]);
	if (crash.stack_words > CRASH_STACK_WORDS)
		crash.stack_words = CRASH_STACK_WORDS;
	for (u32 i = 0; i < crash.stack_words; i++) {
		u32 w = crash.stack[i];
		// Words that may be return addresses into the kernel; just logs names them.
		if ((w >= 0x08000000 && w < 0x08100000) || (w >= 0x03000000 && w < 0x03008000))
			tlog_force(TLOG_ERRORS, "crash: stack %08lX: %08lX", (unsigned long)(crash.stack_base + 4 * i),
				   (unsigned long)w);
	}
	if (crash.log_len > CRASH_LOG_BYTES)
		crash.log_len = CRASH_LOG_BYTES;
	// The log lines from before the crash, one at a time.
	char *p = crash.log, *end = crash.log + crash.log_len;
	if (p < end)
		tlog_force(TLOG_ERRORS, "crash: the log before it:");
	while (p < end) {
		char *nl = memchr(p, '\n', end - p);
		u32 len = nl ? (u32)(nl - p) : (u32)(end - p);
		tlog_force(TLOG_ERRORS, "crash: | %.*s", (int)len, p);
		p += len + 1;
	}
	tlog_flush();
	if (tlog_all_written())  // else the next boot tries again
		cart_clear_magic(CARTLOG_CRASH, 8);
}

static void report_snapshot(void)
{
	struct cartlog_snapshot *s = &block.snapshot;
	_Static_assert(sizeof(struct cartlog_snapshot) == CARTLOG_SNAPSHOT_SIZE, "snapshot layout");
	cart_read(CARTLOG_SNAPSHOT, s, sizeof(*s));
	if (memcmp(s->magic, SNAPSHOT_MAGIC, 16) != 0)
		return;
	cart_clear_magic(CARTLOG_SNAPSHOT, 16);
	tlog_force(TLOG_INFO, "debug info from the in-game menu: %.12s, code %.4s, version %u, hook at %08lX",
		   (const char *)s->header, (const char *)s->header + 0xC, s->header[0x1C], (unsigned long)s->irq_hook);
	const u32 *r = s->regs;
	tlog_force(TLOG_INFO, "debug info: r4-r7 %08lX %08lX %08lX %08lX", (unsigned long)r[0], (unsigned long)r[1],
		   (unsigned long)r[2], (unsigned long)r[3]);
	tlog_force(TLOG_INFO, "debug info: r8-r11 %08lX %08lX %08lX %08lX", (unsigned long)r[4], (unsigned long)r[5],
		   (unsigned long)r[6], (unsigned long)r[7]);
	tlog_force(TLOG_INFO, "debug info: r0-r3 %08lX %08lX %08lX %08lX r12 %08lX", (unsigned long)s->irq_frame[0],
		   (unsigned long)s->irq_frame[1], (unsigned long)s->irq_frame[2], (unsigned long)s->irq_frame[3],
		   (unsigned long)s->irq_frame[4]);
	tlog_force(TLOG_INFO, "debug info: pc %08lX cpsr %08lX sp %08lX lr %08lX", (unsigned long)(s->irq_frame[5] - 4),
		   (unsigned long)r[10], (unsigned long)r[12], (unsigned long)r[13]);
	tlog_force(TLOG_INFO, "debug info: game IRQ handler %08lX, IRQ flags %08lX, IRQ vector %08lX (the hook is %s)",
		   (unsigned long)s->hook[1], (unsigned long)s->hook[2], (unsigned long)s->hook[3],
		   (s->hook[3] == s->irq_hook) ? "in place" : "gone");
	u16 io[8];
	memcpy(io, s->io_irq, sizeof(io));
	tlog_force(TLOG_INFO, "debug info: IE %04X IF %04X WAITCNT %04X IME %04X", io[0], io[1], io[2], io[4]);
	for (u32 i = 0; i < 0x60; i += 16) {
		const u8 *b = s->io + i;
		tlog_force(
			TLOG_INFO,
			"debug info: io %03lX: %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X %02X%02X",
			(unsigned long)i, b[1], b[0], b[3], b[2], b[5], b[4], b[7], b[6], b[9], b[8], b[11], b[10],
			b[13], b[12], b[15], b[14]);
	}
	for (u32 i = 0; i < 0x200; i += 32) {
		const u32 *w = (const u32 *)(s->iwram_top + i);
		tlog_force(TLOG_INFO, "debug info: %08lX: %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX",
			   (unsigned long)0x03007E00 + i, (unsigned long)w[0], (unsigned long)w[1], (unsigned long)w[2],
			   (unsigned long)w[3], (unsigned long)w[4], (unsigned long)w[5], (unsigned long)w[6],
			   (unsigned long)w[7]);
	}
	for (u32 i = 0; i < 0x100; i += 32) {
		const u32 *w = (const u32 *)(s->stack + i);
		tlog_force(TLOG_INFO, "debug info: sp+%03lX: %08lX %08lX %08lX %08lX %08lX %08lX %08lX %08lX",
			   (unsigned long)i, (unsigned long)w[0], (unsigned long)w[1], (unsigned long)w[2],
			   (unsigned long)w[3], (unsigned long)w[4], (unsigned long)w[5], (unsigned long)w[6],
			   (unsigned long)w[7]);
	}
	tlog_flush();
}

static struct cartlog_state state EWRAM_BSS;
static u32 prev_magic_ok;  // the previous session's state, as cartlog_boot_start found it
static u32 prev_phase;
static u32 prev_detail;

static const char *phase_name(u32 phase)
{
	static const char *const names[] = {"nothing", "booting", "the menu", "launching", "a game", "Diagnostics"};
	return phase < sizeof(names) / sizeof(*names) ? names[phase] : "an unknown phase";
}

void cartlog_boot_start(void)
{
	if (tlog_level == TLOG_OFF)
		return;
	cart_read(CARTLOG_STATE, &state, sizeof(state));
	prev_magic_ok = memcmp(state.magic, STATE_MAGIC, 8) == 0;
	if (!prev_magic_ok) {
		memset(&state, 0, sizeof(state));
		memcpy(state.magic, STATE_MAGIC, 8);
	}
	state.game[sizeof(state.game) - 1] = 0;
	prev_phase = state.phase;
	prev_detail = state.detail;
	state.boot++;
	state.phase = PHASE_BOOT;
	state.detail = 0;
	cart_write_block(CARTLOG_STATE, &state, sizeof(state), 8);
}

void cartlog_boot_report(void)
{
	report_crash();
	report_snapshot();
	if (tlog_level == TLOG_OFF)
		return;

	// How the previous session ended, from the log on the card: its last
	// line is a launch's first if that launch never reached the game.
	const char *last = tlog_previous_line();
	if (last && strstr(last, "launch: loading "))
		tlog_error("previous session: a launch never reached the game; its last line was: %s", last);

	if (!prev_magic_ok) {
		tlog("previous session: nothing in cart RAM (the first boot with logging on, or cart RAM lost power)");
	} else {
		switch (prev_phase) {
		case PHASE_MENU:
			tlog("previous session: ended in the menu");
			break;
		case PHASE_GAME:
			tlog("previous session: ended in %s (%.4s), by power-off or the in-game menu's RESET",
			     state.game, state.code);
			break;
		case PHASE_BOOT:
		case PHASE_LAUNCH:
		case PHASE_DIAG:
			tlog_error("previous session: ended abnormally in %s (step %lu)%s%s", phase_name(prev_phase),
				   (unsigned long)prev_detail, state.game[0] ? ": " : "", state.game);
			break;
		default:
			tlog("previous session: ended in %s", phase_name(prev_phase));
			break;
		}
	}
	tlog("boot %lu with logging on", (unsigned long)state.boot);
}

void cartlog_phase(u32 phase, u32 detail, const char *game)
{
	if (tlog_level == TLOG_OFF)
		return;
	if (state.phase == phase && state.detail == detail && !game)
		return;
	state.phase = phase;
	state.detail = detail;
	if (game) {
		strncpy(state.game, game, sizeof(state.game) - 1);
		state.game[sizeof(state.game) - 1] = 0;
		memcpy(state.code, GAMECODE, 4);
	}
	if (memcmp(state.magic, STATE_MAGIC, 8) != 0) {  // logging was off at boot
		memcpy(state.magic, STATE_MAGIC, 8);
		cart_write_block(CARTLOG_STATE, &state, sizeof(state), 8);
	} else {
		cart_write(CARTLOG_STATE + 12, &state.phase, sizeof(state) - 12);
	}
}
