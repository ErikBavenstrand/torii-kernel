// What the kernel keeps in cart RAM for its next boot: where the previous
// session got to, a kernel crash dump, and the in-game menu's debug info.
// It all lives in one 64 KB page of the cart's SRAM that nothing else uses,
// far from the game saves (docs/research/omega-de-hardware.md, "Save and work
// RAM"). It survives a soft reset; whether it survives a power cycle is for
// the real cart to tell, and the boot report says when it found nothing.
#ifndef TORII_CARTLOG_H
#define TORII_CARTLOG_H

#include <gba_base.h>

// SRAM page 0xF0: offsets 0xF0000-0xFFFFF of the cart's 1 MB SRAM. The
// in-game menu's save states end at 0xAFFFF (pages 0x40-0xAF).
#define CARTLOG_PAGE 0xF0
#define CARTLOG_STATE 0x0E000000  // struct cartlog_state
#define CARTLOG_CRASH 0x0E000100  // struct cartlog_crash
#define CARTLOG_SNAPSHOT 0x0E001000  // the in-game menu's DEBUG snapshot (the overlay's, INGAME_SRAM_SNAPSHOT)
#define CARTLOG_SNAPSHOT_SIZE 0x500
#define CARTLOG_EEPROM_PROBE 0x0E001800  // the EEPROM probe ROM's report (source/eepromprobe.h)
#define CARTLOG_RUMBLE_PROBE 0x0E001A00  // the rumble probe ROM's report (source/rumbleprobe.h)
#define CARTLOG_INGAME_PROBE 0x0E001B00  // the in-game probe ROM's report (source/ingameprobe.h)
#define CARTLOG_INGAME_REFUSED 0x0E001C00  // why the in-game menu's stub didn't open it (struct ingame_refused)
// A launch's ROM patches of the picked cheats, from its first pass to its
// second (cheatstore_launch): not for the next boot, but in no one's way
#define CARTLOG_CHEAT_PATCHES 0x0E002000
#define CARTLOG_CHEAT_PATCHES_SIZE 0xC000
// Pages Diagnostics may write test patterns to: free SRAM past the states.
#define CARTLOG_TEST_PAGE 0xC0

// Where the kernel was, so the next boot can tell how the session ended.
enum {
	PHASE_NONE = 0,
	PHASE_BOOT = 1,  // started, the menu not reached yet
	PHASE_MENU = 2,
	PHASE_LAUNCH = 3,  // loading a game
	PHASE_GAME = 4,  // jumped to the game
	PHASE_DIAG = 5,  // in Diagnostics, at step `detail`
	PHASE_CRASH_TEST = 6,  // Diagnostics crashing the kernel on purpose
};

// Reads where the previous session got to and records that this one is
// booting (only with LOGGING on). Call it early: it needs no card.
void cartlog_boot_start(void);
// Logs the previous session's state, crash dump, in-game debug info and
// EEPROM probe report (all but the state even with LOGGING off) and clears
// them. Call it once the card is
// mounted, then cartlog_phase(PHASE_MENU) when the menu shows.
void cartlog_boot_report(void);

// The in-game menu's stub's refusal record, which it also leaves in IWRAM
// on its way back to Torii (INGAME_REFUSED_IWRAM, source/ingame_abi.h),
// into page 0xF0 for the boot to show and log. Call it first thing in
// main, before the stack can grow down to it.
void cartlog_ingame_refused_carry(void);
// 1 and the record if page 0xF0 holds one; cartlog_boot_report clears it
struct ingame_refused;
int cartlog_ingame_refused(struct ingame_refused *r);

// The boot found the card wouldn't mount: noted in the in-game probe's
// report copy in its save, if one is there, as nothing else can be logged
void cartlog_mount_failed(void);

// Records where the kernel is (only with LOGGING on). game is the file being
// launched, or NULL.
void cartlog_phase(u32 phase, u32 detail, const char *game);

// The kernel's IRQ handler (source/crash.s): counts frames and catches a
// crash. Installed by cartlog_install.
void cartlog_install(void);

// Called by source/crash.s with r0-r15 and the CPSR of the crashed code.
// Saves them and the stack in cart RAM, then restarts the kernel.
void IWRAM_CODE torii_crash(const u32 *regs) __attribute__((noreturn));

#endif
