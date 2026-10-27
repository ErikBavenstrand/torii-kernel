#include <stddef.h>
#include <stdio.h>
#include <string.h>
#include <strings.h>

#include <gba_base.h>
#include <gba_interrupt.h>

#include "Ezcard_OP.h"
#include "build_id.h"
#include "cartlog.h"
#include "eepromprobe.h"
#include "ingame_abi.h"
#include "ingameprobe.h"
#include "rumbleprobe.h"
#include "ff.h"
#include "tlog.h"

extern void SoftReset_now(void);
extern void torii_irq(void);
extern u8 GAMECODE[4];
extern u32 gl_currentpage;

volatile u32 torii_frames;

// --- What the blocks in cart RAM hold ---------------------------------------

#define STATE_MAGIC "TORIIST2"
#define CRASH_MAGIC "TORIICR2"
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
	u32 build_id;  // of the kernel that crashed, which may not be the one reporting it
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
	struct rumble_probe rumble;
	struct ingame_probe ingame;
} block EWRAM_BSS;
#define crash block.crash
_Static_assert(CARTLOG_CRASH + sizeof(struct cartlog_crash) <= CARTLOG_SNAPSHOT, "crash dump size");
_Static_assert(CARTLOG_SNAPSHOT + CARTLOG_SNAPSHOT_SIZE <= CARTLOG_EEPROM_PROBE, "snapshot size");
_Static_assert(sizeof(struct eeprom_probe) == 0x200, "EEPROM probe report layout");
_Static_assert(CARTLOG_EEPROM_PROBE + sizeof(struct eeprom_probe) <= CARTLOG_RUMBLE_PROBE, "EEPROM probe size");
_Static_assert(sizeof(struct rumble_probe) == 0x58, "rumble probe report layout");
_Static_assert(CARTLOG_RUMBLE_PROBE + sizeof(struct rumble_probe) <= CARTLOG_INGAME_PROBE, "rumble probe size");
_Static_assert(sizeof(struct ingame_probe) <= 0x100 && CARTLOG_INGAME_PROBE + 0x100 <= CARTLOG_INGAME_REFUSED,
	       "in-game probe report size");

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
	crash.build_id = torii_build_id;
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
	int old = memcmp(crash.magic, "TORIICR1", 8) == 0;  // v0.2.0's, before build IDs
	if (!old && memcmp(crash.magic, CRASH_MAGIC, 8) != 0)
		return 0;
	if (tlog_level == TLOG_OFF)  // with logging on, this boot's line is there already
		tlog_marker("a crash report", 1);
	if (old) {
		tlog_force(TLOG_ERRORS,
			   "crash: an older kernel crashed; its dump's layout isn't this one's, so it is skipped");
		tlog_flush();
		if (tlog_all_written())
			cart_clear_magic(CARTLOG_CRASH, 8);
		return 1;
	}
	const u32 *r = crash.regs;
	// First: tools/logs.py names the addresses after it with this build's symbols.
	tlog_force(TLOG_ERRORS, "crash: build id %08lX%s", (unsigned long)crash.build_id,
		   crash.build_id == torii_build_id ? "" : ", not this kernel's");
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

static const char *probe_variant(u32 variant)
{
	switch (variant) {
	case EEPROM_PROBE_POLL_AT_ONCE:
		return "polled at once";
	case EEPROM_PROBE_WAIT_THEN_POLL:
		return "waited, then polled";
	case EEPROM_PROBE_NO_POLL:
		return "no poll, waited 10 ms";
	case EEPROM_PROBE_EXTRA_BIT:
		return "an extra 0 bit, waited, then polled";
	default:
		return "an unknown way";
	}
}

// Whether a write step comes before step i: data from this run before any
// write is an earlier run's that happens to share its nonce.
static int written_before(const struct eeprom_probe *p, u32 i)
{
	while (i--)
		if (p->step[i].op == EEPROM_PROBE_WRITE)
			return 1;
	return 0;
}

// The rumble probe ROM's report (tests/roms/rumble): each way it drove the
// motor in game mode, the user's answer, and what 0x08001000 read.
static void report_rumble_probe(void)
{
	static const char *const ways[] = {
		"?",
		"pulsed as Jagoomba does (0xF1 at 0x09E20000, then 2 and 0 at 0x08001000 each frame)",
		"0x08001000 held at 2 (after 0xF1 at 0x09E20000)",
		"pulsed as Jagoomba does, with 0x096A0000 at 0 (GPIO port off)",
		"GPIO pin 3 held on, as Drill Dozer does (enable 1, direction 8, data 8)",
	};
	static const char *const answers[] = {"skipped", "yes", "no"};
	struct rumble_probe *p = &block.rumble;
	cart_read(CARTLOG_RUMBLE_PROBE, p, sizeof(*p));
	if (memcmp(p->magic, RUMBLE_PROBE_MAGIC, 16) != 0)
		return;
	cart_clear_magic(CARTLOG_RUMBLE_PROBE, 16);
	u32 tries = p->tries < RUMBLE_PROBE_TRIES ? p->tries : RUMBLE_PROBE_TRIES;
	tlog_force(TLOG_INFO, "rumble probe: %lu tries in game mode", (unsigned long)tries);
	for (u32 i = 0; i < tries; i++) {
		const struct rumble_probe_try *t = &p->t[i];
		if (t->way == RUMBLE_GPIO_PIN3) {
			tlog_force(TLOG_INFO, "rumble probe: %s: %s", ways[t->way],
				   t->answer < 3 ? answers[t->answer] : "?");
			continue;
		}
		tlog_force(TLOG_INFO, "rumble probe: %s: %s; 0x08001000 read %04X, %04X after 2, %04X after 0",
			   t->way < sizeof(ways) / sizeof(*ways) ? ways[t->way] : ways[0],
			   t->answer < 3 ? answers[t->answer] : "?", t->before, t->on, t->off);
	}
}

// The in-game probe ROM's report copy in its own save (cart SRAM page 0)
#define SAVE_COPY (0x0E000000 + INGAME_PROBE_SAVE_OFFSET)

void cartlog_mount_failed(void)
{
	struct ingame_probe *p = &block.ingame;
	SetRampage(0);
	ReadSram(SAVE_COPY, (u8 *)p, sizeof(*p));
	if (memcmp(p->magic, INGAME_PROBE_MAGIC, 16) != 0 || p->mount_failed == 0xFF)
		return;
	p->mount_failed++;
	WriteSram(SAVE_COPY + offsetof(struct ingame_probe, mount_failed), &p->mount_failed, 1);
}

// The in-game probe ROM's report (tests/roms/ingame, v0.8 P0): what the
// cart did in game mode, for the in-game menu
static void report_ingame_probe(void)
{
	static const char *const sd[] = {
		"not tried (no scratch file)",
		"wrote and read back, the kernel's bytes put back",
		"a read didn't answer",
		"the sector didn't hold the kernel's bytes",
		"a write didn't answer",
		"the probe's bytes didn't read back",
		"the kernel's bytes didn't read back after",
		"not run (the probe stopped in it, or the cart was off before A or B)",
		"skipped (B)",
	};
	static const char *const saved[] = {"didn't read back", "byte writes (SRAM)", "flash commands"};
	struct ingame_probe *p = &block.ingame;
	// Page 0xF0's copy, or, when a power cycle lost it, the one in the
	// probe's save (battery-backed); either is cleared once logged
	int from_save = 0;
	cart_read(CARTLOG_INGAME_PROBE, p, sizeof(*p));
	if (memcmp(p->magic, INGAME_PROBE_MAGIC, 16) != 0) {
		ReadSram(SAVE_COPY, (u8 *)p, sizeof(*p));  // page 0 is set
		if (memcmp(p->magic, INGAME_PROBE_MAGIC, 16) != 0)
			return;
		from_save = 1;
	} else {
		cart_clear_magic(CARTLOG_INGAME_PROBE, 16);
	}
	u8 save_returning = 0, save_mount_failed = 0;
	{
		struct ingame_probe s;
		ReadSram(SAVE_COPY, (u8 *)&s, sizeof(s));
		if (memcmp(s.magic, INGAME_PROBE_MAGIC, 16) == 0) {
			save_returning = s.returning;
			save_mount_failed = s.mount_failed;
			static const u8 blank[16];
			WriteSram(SAVE_COPY, (u8 *)blank, 16);
		}
	}
	if (from_save)
		tlog_force(TLOG_INFO,
			   "in-game probe: the report from its save: cart RAM page 0xF0 lost it, so the cart was off "
			   "since; A %s",
			   save_returning ? "was pressed (the soft reset back to the kernel began)"
					  : "wasn't pressed: the cart was turned off at the probe's screen");
	else
		tlog_force(TLOG_INFO, "in-game probe: the report from cart RAM page 0xF0 (the soft reset kept it)");
	if (save_mount_failed)
		tlog_force(TLOG_ERRORS, "in-game probe: %u boots since the probe found the card wouldn't mount",
			   save_mount_failed);
	if (!from_save)
		tlog_force(TLOG_INFO, "in-game probe: its copy in the save: %s",
			   p->saved < sizeof(saved) / sizeof(*saved) ? saved[p->saved] : "?");
	tlog_force(TLOG_INFO, "in-game probe: cart SRAM pages %02X-%02X: %u of %u read back (first bad %02X)",
		   INGAME_PROBE_FIRST_PAGE, INGAME_PROBE_LAST_PAGE, p->pages_ok, p->pages_tried, p->first_bad_page);
	tlog_force(
		TLOG_INFO,
		"in-game probe: SRAM wait states read back at 4 %s, 3 %s, 2 %s, 8 cycles %s; the page register reads "
		"%04X after 0x20, %04X after 0x55",
		(p->sram_waits_ok & 1) ? "yes" : "no", (p->sram_waits_ok & 2) ? "yes" : "no",
		(p->sram_waits_ok & 4) ? "yes" : "no", (p->sram_waits_ok & 8) ? "yes" : "no", p->page_readback[0],
		p->page_readback[1]);
	tlog_force(TLOG_INFO,
		   "in-game probe: flash bank: offset 0 reads %02X in bank 0, %02X in bank 1, %02X in bank 1 "
		   "after pages 0x20 and 0 (%s)",
		   p->bank0, p->bank1, p->bank1_after,
		   p->bank0 == p->bank1         ? "the banks look alike: no answer"
		   : p->bank1_after == p->bank1 ? "the bank stayed"
						: "the page register reset the bank");
	tlog_force(TLOG_INFO, "in-game probe: PSRAM 0x08010000 read %04X, %04X after writing A55A, %04X put back (%s)",
		   p->psram_before, p->psram_written, p->psram_after,
		   p->psram_written == 0xA55A ? "PSRAM writes land" : "PSRAM writes don't land");
	tlog_force(TLOG_INFO, "in-game probe: SD sector 0 %s; scratch sector %lu: %s",
		   p->sd_scratch >= INGAME_PROBE_SD_PENDING ? "not read"
		   : p->sd_mbr                              ? "read with its signature"
							    : "didn't read",
		   (unsigned long)p->scratch_lba, p->sd_scratch < sizeof(sd) / sizeof(*sd) ? sd[p->sd_scratch] : "?");
	tlog_force(TLOG_INFO,
		   "in-game probe: page 0x20: %u of %u bytes didn't read back (byte writes at 8 cycles); with the "
		   "flash bank switched it read %02X in bank 1, %02X in bank 0 (%s)",
		   p->bulk_bad, INGAME_PROBE_BULK, p->bank_page_b1, p->bank_page_b0,
		   p->bank_page_b1 == 0x3C && p->bank_page_b0 == 0xC3   ? "one byte: the bank doesn't reach it"
		   : p->bank_page_b1 != 0x3C && p->bank_page_b0 == 0x3C ? "two bytes: the bank shifts it"
									: "neither: see the bytes");
	const u8 *on = p->rtc_on, *off = p->rtc_off;
	tlog_force(TLOG_INFO,
		   "in-game probe: clock with 0x096A0000 at 1: %02X-%02X-%02X %02X:%02X:%02X; at 0: %02X-%02X-%02X "
		   "%02X:%02X:%02X",
		   on[0], on[1], on[2], on[4], on[5], on[6], off[0], off[1], off[2], off[4], off[5], off[6]);
}

void cartlog_ingame_refused_carry(void)
{
	volatile u32 *e = (volatile u32 *)INGAME_REFUSED_IWRAM;
	if (e[0] != INGAME_REFUSED_MAGIC0 || e[1] != INGAME_REFUSED_MAGIC1)
		return;
	u32 r[sizeof(struct ingame_refused) / 4];
	for (u32 i = 0; i < sizeof(r) / 4; i++)
		r[i] = e[i];
	e[0] = e[1] = 0;
	cart_write_block(CARTLOG_INGAME_REFUSED, r, sizeof(r), 8);
}

int cartlog_ingame_refused(struct ingame_refused *r)
{
	cart_read(CARTLOG_INGAME_REFUSED, r, sizeof(*r));
	return r->magic[0] == INGAME_REFUSED_MAGIC0 && r->magic[1] == INGAME_REFUSED_MAGIC1;
}

// The screen's colour the stub showed for a record, in words
static const char *ingame_refused_colour_name(const struct ingame_refused *r)
{
	static const char *const names[] = {"white", "blue", "red", "green", "cyan", "red", "yellow", "magenta"};
	_Static_assert(sizeof(names) / sizeof(*names) == 8, "INGAME_REFUSED_COLOURS");
	return names[ingame_refused_colour(r->reason, r->detail) & 7];  // under 8 already
}

// The in-game menu's stub didn't open the menu (gba_rts_patch.s,
// ingame_refuse) and gave the game back as it was: why, from its record
static void report_ingame_refused(void)
{
	static const char *const why[INGAME_REFUSED_REASONS] = {
		"?",
		"no overlay image in cart SRAM",
		"the game's memory didn't read back from cart SRAM",
		"the overlay image loaded from cart SRAM didn't check",
		"the overlay image's header has sizes it can't take",
	};
	static const char *const copy[] = {"", ": the EWRAM window", ": OAM", ": the OBJ palette"};
	struct ingame_refused r;
	if (!cartlog_ingame_refused(&r))
		return;
	cart_clear_magic(CARTLOG_INGAME_REFUSED, 8);
	tlog_force(TLOG_ERRORS, "in-game menu: the stub didn't open it, %s%s (the game's WAITCNT %04X; the screen %s)",
		   why[r.reason < INGAME_REFUSED_REASONS ? r.reason : 0],
		   copy[r.detail < sizeof(copy) / sizeof(*copy) ? r.detail : 0], r.waitcnt,
		   ingame_refused_colour_name(&r));
	char line[96];
	unsigned long at = r.at, expected = r.expected, read = r.read;
	if (r.reason == INGAME_REFUSED_BACKUP)
		snprintf(line, sizeof(line), "the first difference before %08lX: RAM %08lX, SRAM read %08lX", at,
			 expected, read);
	else if (r.reason == INGAME_REFUSED_IMAGE)
		snprintf(line, sizeof(line), "the check word %08lX, computed %08lX", expected, read);
	else
		snprintf(line, sizeof(line), "the header's magic %08lX, image %lu bytes, window %lu bytes", at,
			 expected, read);
	tlog_force(TLOG_ERRORS, "in-game menu: %s; %u tries, VCOUNT %u at the menu key", line, r.tries, r.vcount);
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
		if (s->op == EEPROM_PROBE_WAIT) {
			tlog_force(TLOG_INFO, "eeprom probe: %2lu: waited a frame", (unsigned long)i);
			continue;
		}
		if (s->op == EEPROM_PROBE_WRITE) {
			if (s->polls == EEPROM_PROBE_TIMEOUT)
				tlog_force(TLOG_INFO, "eeprom probe: %2lu: write block %u (%s): %s, never finished",
					   (unsigned long)i, s->block, probe_variant(s->variant), data);
			else if (s->variant == EEPROM_PROBE_NO_POLL)
				tlog_force(TLOG_INFO, "eeprom probe: %2lu: write block %u (%s): %s", (unsigned long)i,
					   s->block, probe_variant(s->variant), data);
			else
				tlog_force(TLOG_INFO,
					   "eeprom probe: %2lu: write block %u (%s): %s, ready after %lu polls",
					   (unsigned long)i, s->block, probe_variant(s->variant), data,
					   (unsigned long)s->polls);
			continue;
		}
		int near;
		int src = eeprom_probe_source(p, i, &near);
		char what[96];
		if (src >= 0) {
			const struct eeprom_probe_step *w = &p->step[src];
			u32 len = snprintf(what, sizeof(what), "step %d's write (block %u, %s)", src, w->block,
					   probe_variant(w->variant));
			if (near && len < sizeof(what))
				snprintf(what + len, sizeof(what) - len, ", except byte 7: wrote %02X, read %02X",
					 w->data[7], s->data[7]);
		} else if (written_before(p, i) && s->data[3] == (u8)(p->nonce >> 8) && s->data[4] == (u8)p->nonce) {
			snprintf(what, sizeof(what), "this run, but no write matches it");
		} else if (s->data[0] == 0xE6 || s->data[0] == 0xEE) {
			snprintf(what, sizeof(what), "an earlier %u-bit run", s->data[0] == 0xE6 ? 6 : 14);
		} else {
			snprintf(what, sizeof(what), "nothing written in this run");
		}
		tlog_force(TLOG_INFO, "eeprom probe: %2lu: read block %u: %s, from %s", (unsigned long)i, s->block,
			   data, what);
	}
	u32 v = eeprom_probe_verdict(p);
	tlog_force(TLOG_INFO, "eeprom probe: %lu-bit addresses %s%s%s%s%s", (unsigned long)p->width,
		   (v & EEPROM_PROBE_WORKS)            ? "work"
		   : (v & EEPROM_PROBE_LAST_BYTE_LOST) ? "lose the last byte written"
						       : "don't read back what was written",
		   (v & EEPROM_PROBE_WRAPS) ? "; blocks wrap at 64: a 512-byte EEPROM" : "",
		   (v & EEPROM_PROBE_8K) ? "; 1024 blocks: an 8 KB EEPROM" : "",
		   (v & EEPROM_PROBE_TIMED_OUT) ? "; a write never finished" : "",
		   (v & EEPROM_PROBE_WORKS) && (v & EEPROM_PROBE_LAST_BYTE_LOST) ? "; a write lost its last byte" : "");
	tlog_flush();
}

static struct cartlog_state state EWRAM_BSS;
_Static_assert(CARTLOG_STATE + sizeof(struct cartlog_state) <= CARTLOG_CRASH, "session state size");
static u32 prev_magic_ok;  // the previous session's state, as cartlog_boot_start found it
static u32 prev_phase;
static u32 prev_detail;
static int prev_crashed;  // a crash dump was there at this boot

static const char *phase_name(u32 phase)
{
	static const char *const names[] = {
		"nothing", "booting", "the menu", "launching", "a game", "Diagnostics", "Diagnostics' crash test"};
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
	report_rumble_probe();
	report_ingame_probe();
	report_ingame_refused();
	if (tlog_level == TLOG_OFF)
		return;

	// How the previous session ended, from the log on the card: its last
	// line is a launch's first if that launch never reached the game.
	const char *last = tlog_previous_line();
	if (last && strstr(last, "launch: loading "))
		tlog_error("previous session: a launch never reached the game; its last line was: %s", last);

	if (prev_crashed && prev_magic_ok && prev_phase == PHASE_CRASH_TEST) {
		tlog("previous session: Diagnostics' crash test, which crashes the kernel on purpose: the dump above "
		     "is its result, not a fault");
	} else if (prev_crashed && prev_magic_ok) {
		tlog_error("previous session: the kernel crashed in %s (step %lu)%s%s", phase_name(prev_phase),
			   (unsigned long)prev_detail, state.game[0] ? "; the last game launched: " : "", state.game);
	} else if (prev_crashed) {
		tlog_error("previous session: the kernel crashed (cart RAM had no state saying where)");
	} else if (!prev_magic_ok) {
		// Page 0xF0 keeps only through a soft reset: a power-off clears it
		tlog("previous session: nothing in cart RAM (the first boot with logging on, or the cart was off)");
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
