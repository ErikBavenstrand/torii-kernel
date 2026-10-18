#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <gba_base.h>
#include <gba_interrupt.h>

#include "Ezcard_OP.h"
#include "cartlog.h"
#include "eepromprobe.h"
#include "ff.h"
#include "tlog.h"

extern void SoftReset_now(void);
extern void torii_irq(void);
extern u8 GAMECODE[4];
extern u32 gl_currentpage;

volatile u32 torii_frames;

// --- What the blocks in cart RAM hold ---------------------------------------

#define STATE_MAGIC "TORIIST2"
#define CRASH_MAGIC "TORIICR1"
#define SNAPSHOT_MAGIC "TORII-DEBUGINFO1"  // gba_rts_patch.s writes it last

struct cartlog_state {
	char magic[8];
	u32 boot;  // boots with LOGGING on, counting this one
	u32 phase;
	u32 detail;
	char code[4];
	char game[100];
	char title[16];  // a GB or GBC game's title, from its header (0x134)
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
// The crash dump, the debug info or the EEPROM probe's report, one at a time.
static union {
	struct cartlog_crash crash;
	struct cartlog_snapshot snapshot;
	struct eeprom_probe probe;
} block EWRAM_BSS;
#define crash block.crash
_Static_assert(CARTLOG_CRASH + sizeof(struct cartlog_crash) <= CARTLOG_SNAPSHOT, "crash dump size");
_Static_assert(CARTLOG_SNAPSHOT + CARTLOG_SNAPSHOT_SIZE <= CARTLOG_EEPROM_PROBE, "snapshot size");
_Static_assert(sizeof(struct eeprom_probe) == 0x200, "EEPROM probe report layout");

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

// True if there was a crash dump.
static int report_crash(void)
{
	cart_read(CARTLOG_CRASH, &crash, sizeof(crash));
	if (memcmp(crash.magic, CRASH_MAGIC, 8) != 0)
		return 0;
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
	tlog_flush();  // in parts: the whole report may not fit the ring
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
	tlog_flush();
	while (p < end) {
		char *nl = memchr(p, '\n', end - p);
		u32 len = nl ? (u32)(nl - p) : (u32)(end - p);
		tlog_force(TLOG_ERRORS, "crash: | %.*s", (int)len, p);
		p += len + 1;
	}
	tlog_flush();
	if (tlog_all_written())  // else the next boot tries again
		cart_clear_magic(CARTLOG_CRASH, 8);
	return 1;
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

static void probe_bytes(char *out, const u8 *d)
{
	snprintf(out, 24, "%02X%02X%02X%02X %02X%02X%02X%02X", d[0], d[1], d[2], d[3], d[4], d[5], d[6], d[7]);
}

// The EEPROM probe ROM's report (tests/roms/eeprom): each step, where each
// read's data came from, and what they add up to.
static void report_eeprom_probe(void)
{
	struct eeprom_probe *p = &block.probe;
	cart_read(CARTLOG_EEPROM_PROBE, p, sizeof(*p));
	if (memcmp(p->magic, EEPROM_PROBE_MAGIC, 16) != 0)
		return;
	cart_clear_magic(CARTLOG_EEPROM_PROBE, 16);
	u32 steps = p->steps < EEPROM_PROBE_STEPS ? p->steps : EEPROM_PROBE_STEPS;
	tlog_force(TLOG_INFO, "eeprom probe: a %lu-bit run, %lu steps (the save type is in the launch line before)",
		   (unsigned long)p->width, (unsigned long)steps);
	char data[24];
	for (u32 i = 0; i < steps; i++) {
		const struct eeprom_probe_step *s = &p->step[i];
		probe_bytes(data, s->data);
		if (s->op == EEPROM_PROBE_WRITE) {
			if (s->polls == EEPROM_PROBE_TIMEOUT)
				tlog_force(TLOG_INFO, "eeprom probe: %2lu: write block %u: %s, never finished",
					   (unsigned long)i, s->block, data);
			else
				tlog_force(TLOG_INFO, "eeprom probe: %2lu: write block %u: %s, ready after %lu polls",
					   (unsigned long)i, s->block, data, (unsigned long)s->polls);
			continue;
		}
		int src = eeprom_probe_source(p, i);
		u8 other[8];
		eeprom_probe_pattern(other, p->width == 6 ? 14 : 6, s->block);
		char what[48];
		if (src >= 0)
			snprintf(what, sizeof(what), "step %d's write (block %u)", src, p->step[src].block);
		else
			strcpy(what, !memcmp(s->data, other, 8) ? "an earlier run's other width (this block)"
								: "nothing written in this run");
		tlog_force(TLOG_INFO, "eeprom probe: %2lu: read block %u: %s, from %s", (unsigned long)i, s->block,
			   data, what);
	}
	u32 v = eeprom_probe_verdict(p);
	tlog_force(TLOG_INFO, "eeprom probe: %lu-bit addresses %s%s%s%s", (unsigned long)p->width,
		   (v & EEPROM_PROBE_WORKS) ? "work" : "don't read back what was written",
		   (v & EEPROM_PROBE_WRAPS) ? "; blocks wrap at 64: a 512-byte EEPROM" : "",
		   (v & EEPROM_PROBE_8K) ? "; 1024 blocks: an 8 KB EEPROM" : "",
		   (v & EEPROM_PROBE_TIMED_OUT) ? "; a write never finished" : "");
	tlog_flush();
}

static struct cartlog_state state EWRAM_BSS;
static u32 prev_magic_ok;  // the previous session's state, as cartlog_boot_start found it
static u32 prev_phase;
static u32 prev_detail;
static int prev_crashed;  // a crash dump was there at this boot

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
	prev_crashed = report_crash();
	report_snapshot();
	report_eeprom_probe();
	if (tlog_level == TLOG_OFF)
		return;

	// How the previous session ended, from the log on the card: its last
	// line is a launch's first if that launch never reached the game.
	const char *last = tlog_previous_line();
	if (last && strstr(last, "launch: loading "))
		tlog_error("previous session: a launch never reached the game; its last line was: %s", last);

	if (prev_crashed && prev_magic_ok) {
		tlog_error("previous session: the kernel crashed in %s (step %lu)%s%s", phase_name(prev_phase),
			   (unsigned long)prev_detail, state.game[0] ? "; the last game launched: " : "", state.game);
	} else if (prev_crashed) {
		tlog_error("previous session: the kernel crashed (cart RAM had no state saying where)");
	} else if (!prev_magic_ok) {
		tlog("previous session: nothing in cart RAM (the first boot with logging on, or cart RAM lost power)");
	} else {
		switch (prev_phase) {
		case PHASE_MENU:
			tlog("previous session: ended in the menu");
			break;
		case PHASE_GAME:
			tlog("previous session: ended in %s (%.*s), by power-off or the in-game menu's RESET",
			     state.game, state.title[0] ? (int)sizeof(state.title) : 4,
			     state.title[0] ? state.title : state.code);
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

static int is_gb_file(const char *name)
{
	const char *dot = strrchr(name, '.');
	return dot && (!strcasecmp(dot, ".gb") || !strcasecmp(dot, ".gbc"));
}

static FIL title_file EWRAM_BSS;
static u32 title_buf[4] EWRAM_BSS;  // word-aligned for f_read

// A GB or GBC game's title, from the header at 0x134: up to 16 characters,
// the GBC flag or padding ending it. Reads the file from the current folder.
static void read_gb_title(const char *game)
{
	UINT got;
	memset(state.title, 0, sizeof(state.title));
	if (f_open(&title_file, game, FA_READ) != FR_OK)
		return;
	if (f_lseek(&title_file, 0x134) == FR_OK && f_read(&title_file, title_buf, 16, &got) == FR_OK && got == 16) {
		const char *t = (const char *)title_buf;
		for (u32 i = 0; i < 16 && t[i] >= ' ' && t[i] < 0x7F; i++)
			state.title[i] = t[i];
	}
	f_close(&title_file);
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
		for (u32 i = 0; i < 4; i++)  // a GB ROM has no code there, but a title
			state.code[i] = (GAMECODE[i] >= ' ' && GAMECODE[i] < 0x7F) ? GAMECODE[i] : '?';
		if (is_gb_file(game))
			read_gb_title(game);
		else
			memset(state.title, 0, sizeof(state.title));
	}
	if (memcmp(state.magic, STATE_MAGIC, 8) != 0) {  // logging was off at boot
		memcpy(state.magic, STATE_MAGIC, 8);
		cart_write_block(CARTLOG_STATE, &state, sizeof(state), 8);
	} else {
		cart_write(CARTLOG_STATE + 12, &state.phase, sizeof(state) - 12);
	}
}
