// The in-game menu's ABI (docs/design/v0.8/ingame-menu.md): what the kernel,
// the stub in the game's patch area (ingame_stub, source/gba_rts_patch.s)
// and the overlay (source/overlay) agree on. Hardware-free, so the host tests
// check its layout; the stub's copy of the numbers is in gba_rts_patch.s.
//
// The overlay is Thumb code linked at INGAME_EWRAM. The kernel writes its
// image to cart SRAM at INGAME_SRAM_IMAGE at each launch (source/ingame.c),
// and the session block to INGAME_SRAM_SESSION. On the menu key the stub
// backs up the EWRAM the overlay takes, OAM, the OBJ palette and the OBJ
// tiles it draws over to INGAME_SRAM_BACKUP, copies the image in, and calls
// its entry with a struct ingame_call on its stack; the entry returns an
// INGAME_ACT_*. The stub then puts all of it back.
#ifndef TORII_INGAME_ABI_H
#define TORII_INGAME_ABI_H

#include <stddef.h>
#include <stdint.h>

// --- Cart SRAM (byte offsets in the cart's 1 MB; page register = offset >> 12)
#define INGAME_SRAM_BACKUP 0x20000  // the stub's backups, below
#define INGAME_SRAM_STATE 0x40000  // the save-state buffer (to 0xAFFFF, unchanged)
// Torii's mark that the buffer holds a whole state (INGAME_STATE_MARK),
// written last by the stub's SAVE and by the kernel once it put a state
// there; LOAD and the launch into a state need it
#define INGAME_SRAM_STATE_MARK 0xAFFD0
#define INGAME_STATE_MARK "TORII-STATE-OK1."  // 16 bytes, no NUL in cart SRAM
#define INGAME_SRAM_STATE_TAG 0xAFFE0  // struct ingame_state_tag (P3)
// EZ-Flash's flag, "EZ-OmegaRTCFILE." at the buffer's end (an /RTS file's
// last 16 bytes): what the FPGA's write-back of the buffer to the card
// watches (docs/research/omega-de-hardware.md). Torii never writes it set,
// only clears it: on the real cart (v0.8, Pokémon Emerald launched into a
// state) the stub's verify of its backups failed with it set, likely the
// write-back running under the stub's own cart SRAM reads.
#define INGAME_SRAM_RTS_FLAG 0xAFFF0
#define INGAME_STATE_BYTES 0x70000  // the buffer, tag and flag included
#define INGAME_SRAM_IMAGE 0xB0000  // the overlay's image
#define INGAME_IMAGE_MAX 0x6000  // the window takes no more (INGAME_WINDOW_MAX)
#define INGAME_SRAM_SESSION 0xB6000  // struct ingame_session
#define INGAME_SESSION_MAX 0xA000
#define INGAME_SRAM_SHOT 0xC0000  // the screenshots (P4): records of struct ingame_shot, below
// The cheat search (v0.9, docs/design/v0.9/cheat-search.md) borrows the
// save-state buffer: the searched RAM as last compared (EWRAM, then IWRAM
// to 0x03007E00: source/cheatsearch.h's run), the candidates a bit per
// element, and struct ingame_search, its magic last
#define INGAME_SRAM_SEARCH_SNAP 0x40000
#define INGAME_SRAM_SEARCH_BITS 0x88000
#define INGAME_SRAM_SEARCH 0x91000
// The cheats the search found, for Back to Torii to file: struct
// ingame_found, in the backups' page window after the screenshot's backup
#define INGAME_SRAM_FOUND 0x2E000
#define INGAME_SHOTS_END 0xF0000  // their room's end: the cart log's page follows

// The backups, from INGAME_SRAM_BACKUP: all in one 64 KB page window
#define INGAME_BACKUP_EWRAM 0x20000  // the EWRAM window, window_bytes
#define INGAME_BACKUP_OBJ 0x28000  // OBJ tiles from INGAME_OBJ_VRAM, obj_bytes
#define INGAME_BACKUP_OAM 0x2C000  // 1 KB
#define INGAME_BACKUP_OBJ_PAL 0x2C400  // 512 bytes
// The game's IWRAM from its start, while a screenshot's packing borrows it
// for its code (ARM, from the image), table and buffer (the overlay's own
// backup): from EWRAM it took 40 frames
#define INGAME_BACKUP_SHOT 0x2C800
#define INGAME_SHOT_IWRAM 0x03000000
#define INGAME_BACKUP_SHOT_BYTES 0x1200  // the table, the buffer and up to 2 KB of code

// --- The cheat search (v0.9)
#define INGAME_SEARCH_MAGIC 0x31524553u  // "SER1"
#define INGAME_SEARCH_LIST 32  // candidates the list shows: Show them is greyed above
struct ingame_search {
	uint32_t magic;  // INGAME_SEARCH_MAGIC once the snapshot is whole
	uint8_t size;  // an element's bytes: 1, 2 or 4
	uint8_t reserved[3];
	uint32_t left;  // the candidates left
	uint32_t passes;  // the filters applied since the start
	uint32_t value;  // the Equals row's number, as last edited
	uint32_t launch_id;  // the session's: a search of an earlier launch is none
};

#define INGAME_FOUND_MAGIC 0x31444E46u  // "FND1"
#define INGAME_FOUND_MAX 16
#define INGAME_FOUND_ON 0x01  // flags: picked, and on now (live) or set once in game
struct ingame_found_cheat {
	uint32_t address;  // in EWRAM or IWRAM
	uint32_t value;  // cut to size
	uint8_t size;  // 1, 2 or 4
	uint8_t flags;  // INGAME_FOUND_*
	uint16_t reserved;
};
struct ingame_found {
	uint32_t magic;  // INGAME_FOUND_MAGIC; the overlay writes a record, then this head
	uint16_t count;  // records filled
	uint16_t reserved;
	struct ingame_found_cheat cheat[INGAME_FOUND_MAX];
};

// The live block: constant writes the cheat interpreter (cheatrun.s) makes
// every frame before its table, so a found cheat runs from the next frame.
// After the hook's spend area, at the game's old stack top - INGAME_LIVE_BYTES
// (Get_spend_address lowers the stack by INGAME_SPEND_BYTES + INGAME_LIVE_BYTES);
// the kernel patches its address into the interpreter (Cheat_live) and the
// session (live_at). Words: the check, then INGAME_LIVE_PAIRS of (address
// with its width in bits 28-31 as the table's write ops have it, value); an
// address 0 is a pair not used. The interpreter writes nothing unless the
// check matches, and only into EWRAM and IWRAM.
#define INGAME_SPEND_BYTES 0x80
#define INGAME_LIVE_BYTES 0x20
#define INGAME_LIVE_PAIRS 3
#define INGAME_LIVE_MAGIC 0x45564C54u  // "TLVE"
static inline uint32_t ingame_live_check(const uint32_t *pairs)
{
	return INGAME_LIVE_MAGIC ^ pairs[0] ^ (pairs[1] >> 7 | pairs[1] << 25) ^ (pairs[2] >> 13 | pairs[2] << 19) ^
	       (pairs[3] >> 19 | pairs[3] << 13) ^ (pairs[4] >> 23 | pairs[4] << 9) ^ (pairs[5] >> 29 | pairs[5] << 3);
}

// --- The game's memory the overlay borrows
#define INGAME_EWRAM 0x02000000  // the window's start: the overlay is linked here
#define INGAME_WINDOW_MAX 0x6000  // 24 KB: image, bss and stack
#define INGAME_OBJ_VRAM 0x06014000  // OBJ tile 512: valid in every mode, bitmap ones too
#define INGAME_OBJ_TILE 512
#define INGAME_OBJ_MAX 0x4000  // tiles 512-1023

// --- The image: its first bytes, in SRAM and once loaded at INGAME_EWRAM
#define INGAME_IMAGE_MAGIC 0x31564F54u  // "TOV1"
#define INGAME_IMAGE_VERSION 1
struct ingame_image {
	uint32_t magic;  // INGAME_IMAGE_MAGIC
	uint16_t version;  // INGAME_IMAGE_VERSION
	uint16_t header_bytes;  // sizeof(struct ingame_image)
	uint32_t image_bytes;  // bytes to load from INGAME_SRAM_IMAGE, a multiple of 4
	uint32_t window_bytes;  // EWRAM taken from INGAME_EWRAM: image, bss, stack (multiple of 4)
	uint32_t entry;  // the Thumb entry's address, bit 0 set: u32 entry(const struct ingame_call *)
	uint32_t obj_bytes;  // OBJ VRAM drawn over from INGAME_OBJ_VRAM, a multiple of 4
	uint32_t shown;  // 0; 1 once the panel shows, 2 once its text does (tests/hook_watch.lua)
	uint32_t check;  // ingame_image_check of the image after the header, the kernel's at launch
};

// The image's check word: every word of the image after its header (which
// the overlay changes as it runs), rotated in. The stub computes it over
// the copy it loaded and runs the overlay only if it matches.
static inline uint32_t ingame_image_check(const uint32_t *words, uint32_t n)
{
	uint32_t s = 0;
	for (uint32_t i = 0; i < n; i++)
		s = (s << 5 | s >> 27) + words[i];
	return s;
}

// --- Why the stub didn't open the menu, for the kernel's next boot: in cart
// SRAM page 0xF0 (source/cartlog.h, CARTLOG_INGAME_REFUSED), 32 bytes. The
// stub then shows the reason's colour (ingame_refused_colour) until A (the
// game) or B, or the menu keys held for 2 s (Back to Torii); going back, it
// also puts the record in IWRAM just under the 0x200 bytes the BIOS's
// SoftReset clears, where the kernel's start (crt0 clears EWRAM and IWRAM's
// bss only; main's frame ends near 0x03007ED0) leaves it: page 0xF0 may not
// take writes in game mode (a flash or EEPROM save's cart SRAM, Pokémon
// Emerald on the real cart). Before its BIOS calls the stub moves the
// System and Supervisor stacks to the 0x200 bytes SoftReset clears: the real
// BIOS's SWIs run on the caller's System stack, and Emerald's (from
// 0x03007E40) put their frames over the record (v0.8 on the real cart).
#define INGAME_REFUSED_IWRAM 0x03007DE0u
#define INGAME_REFUSED_MAGIC0 0x524E4954u  // "TINR"
#define INGAME_REFUSED_MAGIC1 0x32444546u  // "FED2" (FED1 had 16 bytes)
enum {
	INGAME_REFUSED_NO_IMAGE = 1,  // no overlay image in cart SRAM: its magic
	INGAME_REFUSED_BACKUP = 2,  // the game's memory didn't read back from cart SRAM
	INGAME_REFUSED_IMAGE = 3,  // the loaded image's check word didn't match
	INGAME_REFUSED_SIZES = 4,  // the image's header has sizes the stub can't take
	INGAME_REFUSED_REASONS = 5,
};
struct ingame_refused {
	uint32_t magic[2];  // INGAME_REFUSED_MAGIC0, MAGIC1
	uint16_t reason;  // INGAME_REFUSED_*
	uint16_t waitcnt;  // the game's WAITCNT when the menu key came
	uint32_t detail;  // INGAME_REFUSED_BACKUP: which copy (1 EWRAM, 2 OAM, 3 OBJ palette); else 0
	// What the check saw. BACKUP: the RAM address just past the first word
	// that differed, the word in RAM, the word cart SRAM read back (the last
	// try's). NO_IMAGE and SIZES: the header's magic, image_bytes and
	// window_bytes as read. IMAGE: at 0, the header's check word, the one
	// computed.
	uint32_t at, expected, read;
	uint16_t vcount;  // VCOUNT when the stub started
	uint16_t tries;  // the backup's tries: 2 when its retry failed too, 0 before it
};
// The screen's backdrop while the stub waits on a refusal (RGB15): by
// reason, and for BACKUP by its copy (docs/release-checklist.md lists them).
#define INGAME_REFUSED_COLOURS \
	{0x7FFF /* white: none */, \
	 0x7C00 /* blue: no image */, \
	 0x001F /* red */, \
	 0x03E0 /* green: image check */, \
	 0x7FE0 /* cyan: header sizes */, \
	 0x001F /* red: EWRAM */, \
	 0x03FF /* yellow: OAM */, \
	 0x7C1F /* magenta: OBJ palette */}
// The colour's index for a record: its reason, or 4 + its copy for BACKUP
static inline uint32_t ingame_refused_colour(uint32_t reason, uint32_t detail)
{
	uint32_t i = reason == INGAME_REFUSED_BACKUP ? 4 + detail : reason;
	return i < 8 ? i : 0;
}

// --- The call: on the overlay's stack, from the stub
// Flags: what the game's patch offers (the stub's switches, which a NOR copy
// keeps from when it was written)
#define INGAME_CAN_STATES (1u << 0)  // SAVE and LOAD (RTS_switch: SAVESTATE on)
#define INGAME_CAN_CHEATS (1u << 1)  // the cheat interpreter is in (Cheat_switch)
struct ingame_call {
	// Copies n bytes (a multiple of 4) between cart SRAM at sram (a byte
	// offset) and word-aligned RAM: to RAM when to_sram is 0. It leaves the
	// SRAM page set; the stub puts page 0 back.
	void (*sram_copy)(uint32_t sram, void *ram, uint32_t n, uint32_t to_sram);
	uint32_t flags;  // INGAME_CAN_*
	uint32_t hook;  // the hook's address (RTS_irq in the game's patch), for the DEBUG snapshot
	// The hook's register save area (spend_0x80): the game's r4-r11, IRQ sp
	// and lr, SPSR, then the stub's copies of the registers it changed
	const uint32_t *spend;
};
// The spend area's words and halfwords the DEBUG snapshot reads (byte offsets)
#define INGAME_SPEND_IRQ_SP 0x20  // the IRQ sp at the hook: the BIOS's frame
#define INGAME_SPEND_SYS_SP 0x30  // where the snapshot's copy has the System sp and lr
#define INGAME_SPEND_IME 0x40  // halfword: the game's IME (the stub's SP_IME)
#define INGAME_SPEND_WAITCNT 0x42  // halfword: the game's WAITCNT (SP_WAITCNT)

// The DEBUG snapshot (struct cartlog_snapshot in source/cartlog.c), which
// the overlay writes to cart SRAM page 0xF0 at 0x1000, its magic last
#define INGAME_SRAM_SNAPSHOT 0xF1000
#define INGAME_SNAPSHOT_MAGIC "TORII-DEBUGINFO1"

// What the entry returns
enum {
	INGAME_ACT_RESUME = 0,
	INGAME_ACT_SAVE = 1,  // today's SAVE: the state into SRAM 0x40000-0xAFFFF
	INGAME_ACT_LOAD = 2,  // today's LOAD
	INGAME_ACT_RESET = 3,  // restart the game (SWI 0 with the ROM page left)
	INGAME_ACT_SLEEP = 4,
	INGAME_ACT_TORII = 5,  // back to the kernel (today's RESET)
	INGAME_ACTS
};

// The halfword the hook reads before the cheat interpreter (source/cheatrun.s):
// 1 stops it. The halfword after it is the interpreter's (CHEAT_KEYS_NOW in
// cheatcode.h: the keys at its run), as is CheatRAN's high half (the word
// after: CHEAT_KEYS_BEFORE) during a run.
#define INGAME_CHEAT_ONOFF 0x03007FE0
// Run once (v0.10): the byte after CheatONOFF's (the hook tests only its low
// byte) is a table cheat's toggle bit + 1, which the interpreter's next run
// takes, running that cheat once past its gate (source/cheatrun.s); 0: none
#define INGAME_CHEAT_ONCE 0x03007FE1
// The per-cheat toggles (P2): 64 bits, bit n flips the table's cheat n
// against the launch (source/cheatrun.s). In IWRAM's last 0x200 bytes, past
// the BIOS's SVC stack (from 0x3007FE0 down) and before its words at
// 0x3007FF0: a game's own code leaves them alone, and the BIOS's SoftReset
// zeroes them, back to the launch's cheats.
#define INGAME_CHEAT_TOGGLES 0x03007FE8

// --- The screenshots (P4), from INGAME_SRAM_SHOT
// The overlay's records of the game's screen as it was under the menu, one
// after another (each 4-byte aligned), which the kernel's next boot draws
// (source/ppu_render.c) and writes to the card as BMPs (source/shotstore.c):
// a header, then the palettes (BG, then OBJ; 1 KB), OAM (1 KB) and VRAM
// (96 KB), packed (source/shotpack.h) as three parts. The overlay puts the
// game's OBJ palette, OAM and OBJ tiles back from the stub's backups before
// it packs them. Its magic is written last; the kernel marks a record it
// filed INGAME_SHOT_DONE, and clears the first one's magic once all are.
#define INGAME_SHOT_MAGIC 0x32485354u  // "TSH2"
#define INGAME_SHOT_DONE 0x44485354u  // "TSHD": on the card
#define INGAME_SHOT_VERSION 3
#define INGAME_SHOTS_MAX 5  // a session's at most
#define INGAME_SHOT_IO 0x30  // halfwords of I/O from 0x04000000: the display's
#define INGAME_SHOT_FILE 112  // the file's name, the kernel's, once picked
struct ingame_shot {
	uint32_t magic;  // INGAME_SHOT_MAGIC
	uint16_t version;  // INGAME_SHOT_VERSION
	uint16_t bytes;  // sizeof(struct ingame_shot)
	uint32_t packed;  // the packed bytes after the header, a multiple of 4
	// Where the overlay found the game's copy of the display registers in
	// RAM, which gave the scroll; 0 if it found none (for the log)
	uint32_t shadow;
	// The clock as the screenshot was taken, its 7 BCD bytes in rtc_get's
	// order, and clock[7] 1 once read; all 0 when the game hasn't the clock
	// or was in the middle of reading it
	uint8_t clock[8];
	// The display's registers as the game left them: those the hardware
	// reads back, and the scroll from the game's copy if found; the other
	// write-only ones (the BG matrices, the windows, MOSAIC, BLDY) 0
	uint16_t io[INGAME_SHOT_IO];
	// The kernel writes the name it picked here before it writes the
	// file: a power cut gives the same name again, never a second file
	char file[INGAME_SHOT_FILE];
};
#define INGAME_SHOT_PAL_HALVES 0x200  // the parts: the palettes, OAM, VRAM
#define INGAME_SHOT_OAM_HALVES 0x200
#define INGAME_SHOT_VRAM_BYTES 0x18000
// The packed parts at most, and a record
#define INGAME_SHOT_PACKED_MAX \
	(2 * (2 * (INGAME_SHOT_PAL_HALVES + 1) + INGAME_SHOT_VRAM_BYTES / 2 + INGAME_SHOT_VRAM_BYTES / 2 / 0x7FFF + \
	      1) + \
	 4)
// An unpacked dump (tools/ppu_shot's file, which tests/ppu_render.sh makes
// from mGBA's memory): the header, the palettes at 0x100, OAM at 0x500 and
// VRAM at 0x1000; offsets from the dump's start
#define INGAME_DUMP_PAL 0x100
#define INGAME_DUMP_OAM 0x500
#define INGAME_DUMP_VRAM 0x1000
#define INGAME_DUMP_BYTES (INGAME_DUMP_VRAM + INGAME_SHOT_VRAM_BYTES)

// --- The session block, at INGAME_SRAM_SESSION
#define INGAME_SESSION_MAGIC 0x31535354u  // "TSS1"
#define INGAME_SESSION_VERSION 10
#define INGAME_NAME_BYTES 48
#define INGAME_CHEATS 64  // the picks the in-game list shows; the toggles' bits
#define INGAME_ACT_LINES 16  // the cheats' distinct activation sentences the list's footer shows
#define INGAME_PATH_BYTES 288  // a ROM's path (GAMESAVE_PATH_BYTES)
#define INGAME_SAVE_SLOT_BYTES 100  // a save slot's name (SAVESLOT_NAME_BYTES, 97), word-rounded
#define INGAME_PALETTE 16

// The palette's roles the overlay uses (enum ui_role in source/ui/ui.h; source/ingame.c checks)
#define INGAME_ROLE_TEXT 6
#define INGAME_ROLE_FAINT 8
#define INGAME_ROLE_TINT 10
#define INGAME_ROLE_ACCENT_INK 11
#define INGAME_ROLE_FLOAT 15

// Session flags
#define INGAME_SLEEP_KEY_ON (1u << 0)  // the sleep key is on in Settings
#define INGAME_DEBUG_ON (1u << 1)  // LOGGING is on: the menu offers the DEBUG snapshot
#define INGAME_CLOCK_ON (1u << 2)  // the game has the cart's clock: a screenshot reads it

// The menu's text, drawn by the kernel (the engine's body font) so the
// overlay needs no font: each line 1bpp, INGAME_TEXT_ROWS rows of
// INGAME_TEXT_W pixels, bit 0 of a row's first byte its leftmost pixel
#define INGAME_TEXT_W 176
#define INGAME_ROW_W 160  // the cheat list's and the state slots' rows: their lines, left of a check box
#define INGAME_TEXT_ROWS 12
enum ingame_text {
	INGAME_TEXT_TITLE,  // the game's name, cut to fit
	INGAME_TEXT_RESUME,
	INGAME_TEXT_SAVE,
	INGAME_TEXT_LOAD,
	INGAME_TEXT_CHEATS_ON,
	INGAME_TEXT_CHEATS_OFF,
	INGAME_TEXT_RESET,
	INGAME_TEXT_SLEEP,
	INGAME_TEXT_TORII,
	INGAME_TEXT_DEBUG,
	INGAME_TEXT_DEBUG_SAVED,  // the notice after a DEBUG snapshot
	// P2: the cheat list
	INGAME_TEXT_CHEATS,  // the row that opens it, and its title
	INGAME_TEXT_ALL_OFF,
	INGAME_TEXT_AS_LAUNCHED,
	INGAME_TEXT_KEPT,  // the list's footer: the picks are kept on Back to Torii
	INGAME_TEXT_WHY_ROM,  // why a row is greyed (enum ingame_why)
	INGAME_TEXT_WHY_LOCKED,
	INGAME_TEXT_WHY_CANT,
	INGAME_TEXT_WHY_ROOM,
	INGAME_TEXT_WHY_SLOW,
	INGAME_TEXT_ONCE_HINT,  // the footer on a cheat that can run once: SELECT does it
	INGAME_TEXT_ONCE_QUEUED,  // and once SELECT asked for it
	// P3: the state slots' pages' footers
	INGAME_TEXT_STATE_KEEP,  // a save is kept on the card on Back to Torii
	INGAME_TEXT_STATE_REPLACES,  // saving here drops the state not kept yet
	INGAME_TEXT_STATE_LOAD,  // A loads it
	INGAME_TEXT_STATE_ELSEWHERE,  // another slot: load it from Torii
	INGAME_TEXT_STATE_EMPTY,  // nothing in this slot
	// P4 and the polish
	INGAME_TEXT_SHOT,  // the Screenshot row
	INGAME_TEXT_SHOT_SAVED,  // the menu's footer once one is taken
	INGAME_TEXT_SLEEP_WAKES,  // the menu's footer on the Sleep row: the keys that wake it
	INGAME_TEXT_HINT,  // the menu's footer: the keys that close it
	INGAME_TEXT_SHOT_FULL,  // the menu's footer when no screenshot more fits
	INGAME_TEXT_STATE_SAVED,  // the menu's footer while a state saved in game isn't kept yet
	INGAME_TEXT_SHOT_COUNT,  // the Screenshot row once n are taken: "Screenshot n/max", n from 1
	// What waits in cart memory for Back to Torii, which files it: the
	// Back to Torii row then, the Save state and Screenshot rows' footer at
	// the session's first opening, and the menu's footer while a state saved
	// in game (STATE) or n screenshots (SHOTS + n - 1), or both, wait
	INGAME_TEXT_TORII_SAVE = INGAME_TEXT_SHOT_COUNT + INGAME_SHOTS_MAX,
	INGAME_TEXT_FIRST_SAVE,
	INGAME_TEXT_UNSAVED_STATE,
	INGAME_TEXT_UNSAVED_SHOTS,
	INGAME_TEXT_UNSAVED_BOTH = INGAME_TEXT_UNSAVED_SHOTS + INGAME_SHOTS_MAX,
	// The cheat search (v0.9). A line whose text has a number in it comes
	// as two, the text before the number and after it (_AFTER)
	INGAME_TEXT_SEARCH = INGAME_TEXT_UNSAVED_BOTH + INGAME_SHOTS_MAX,  // the menu's row and the page's title
	INGAME_TEXT_SEARCH_SIZE,  // "Size: 8 bits", then 16 and 32
	INGAME_TEXT_SEARCH_START = INGAME_TEXT_SEARCH_SIZE + 3,
	INGAME_TEXT_SEARCH_SAME,  // the filters, in enum cheatsearch_op's order
	INGAME_TEXT_SEARCH_CHANGED,
	INGAME_TEXT_SEARCH_UP,
	INGAME_TEXT_SEARCH_DOWN,
	INGAME_TEXT_SEARCH_EQUALS,  // "Equals " before the number
	INGAME_TEXT_SEARCH_EQUALS_AFTER,
	INGAME_TEXT_SEARCH_SHOW,
	INGAME_TEXT_SEARCH_NEW,
	INGAME_TEXT_SEARCH_LEFT,  // the footer: "{n} left"
	INGAME_TEXT_SEARCH_LEFT_AFTER,
	INGAME_TEXT_SEARCH_NONE,
	INGAME_TEXT_SEARCH_BUSY,
	INGAME_TEXT_SEARCH_HINT,  // the footer on Start and Size
	INGAME_TEXT_SEARCH_DROPS,  // on Start, with a kept state in the buffer
	INGAME_TEXT_SEARCH_STATE_FIRST,  // on Start, with a state not kept yet
	INGAME_TEXT_SEARCH_FEW,  // on Show them while too many are left
	INGAME_TEXT_SEARCH_EDIT,  // while a number is edited
	INGAME_TEXT_SEARCH_VALUE,  // a candidate's page: "Value: " before the number
	INGAME_TEXT_SEARCH_VALUE_AFTER,
	INGAME_TEXT_SEARCH_KEEP,
	INGAME_TEXT_SEARCH_KEEP_ON,  // with the live block
	INGAME_TEXT_SEARCH_KEEP_SET,  // without: set once as the game resumes
	INGAME_TEXT_SEARCH_KEPT,
	INGAME_TEXT_SEARCH_LIVE_FULL,
	INGAME_TEXT_SEARCH_FOUND_FULL,
	INGAME_TEXT_SEARCH_PICK,  // the list's footer
	INGAME_TEXT_SEARCH_SAVE_ENDS,  // the Save state row's footer while a search runs
	INGAME_TEXT_UNSAVED_FOUND,  // the menu's footer while found cheats wait
	INGAME_TEXTS
};
_Static_assert(INGAME_TEXT_SEARCH_SAME + 4 == INGAME_TEXT_SEARCH_EQUALS, "the filters' rows in cheatsearch_op's order");

// The glyphs the overlay draws numbers with (the kernel's body font): 0-9,
// A-F, then the thousands separator (none: width 0), at INGAME_SRAM_GLYPHS
#define INGAME_GLYPHS 17
#define INGAME_GLYPH_THOUSANDS 16
struct ingame_glyph {
	uint8_t width;  // its advance, at most 8
	uint8_t reserved[3];
	uint8_t rows[INGAME_TEXT_ROWS];  // bit 0 its leftmost pixel
};
struct ingame_line {
	uint16_t width;  // inked pixels from the left, at most INGAME_TEXT_W
	uint16_t reserved;
	uint8_t bits[INGAME_TEXT_ROWS][INGAME_TEXT_W / 8];
};

// --- Save states (P3): the buffer's slot
// What the buffer holds, beside its flag: written by the kernel when it
// preloads a slot (kept: it is on the card), and by the overlay before SAVE
// (not kept: the kernel's next boot files it). The stub's entry code reads
// autoload, at INGAME_SRAM_STATE_TAG + 6 (gba_rts_patch.s's TAG_PAGE).
#define INGAME_STATE_TAG_MAGIC 0x31545354u  // "TST1"
#define INGAME_STATE_SLOTS 8  // slots 1 to 8
struct ingame_state_tag {
	uint32_t magic;  // INGAME_STATE_TAG_MAGIC
	uint8_t slot;  // 1 to INGAME_STATE_SLOTS
	uint8_t kept;  // 1: on the card as it is here
	uint8_t autoload;  // 1: the game's start loads it (a launch into the slot)
	uint8_t reserved;
	uint32_t rom_crc;  // the session's rom_crc: the game it is for
	// With kept, the data_crc of the slot's file it came from (the kernel's,
	// at a launch): a launch whose slot's file still has it skips putting
	// the state back. 0 otherwise.
	uint32_t data_crc;
};

// What the state is of (v0.8.1): which game, and the buffer's format,
// written by the overlay before SAVE in the buffer's free room after the
// I/O copy. The kernel loads a state from the card only into the game it
// names (a ROM swapped at the same path has the same rom_crc) and of a
// format it knows; a state from v0.8.0 has none, and its tag's rom_crc
// alone says whose it is. The overlay's Load needs the same.
#define INGAME_SRAM_STATE_INFO 0xAFF80
#define INGAME_STATE_INFO_MAGIC 0x31495354u  // "TSI1"
// 1: v0.8.0's layout (no format said: 0); LOAD points the IRQ vector at the
// launch's hook and takes the display's registers in the VBlank it gives
// the game back in. A later layout LOAD can't read gets a higher number.
#define INGAME_STATE_FORMAT 1
struct ingame_state_info {
	uint32_t magic;  // INGAME_STATE_INFO_MAGIC
	uint16_t format;  // INGAME_STATE_FORMAT
	uint16_t bytes;  // sizeof(struct ingame_state_info)
	char code[4];  // the ROM header's game code (0xAC)
	uint8_t version;  // the header's version (0xBC)
	uint8_t check;  // the header's check byte (0xBD)
	uint16_t reserved;
	uint32_t rom_bytes;  // the session's: the ROM file's size
	uint32_t hook;  // the IRQ hook of the launch it was saved in (RTS_irq)
	uint32_t reserved2[2];
};

// Why a cheat's toggle can't change in game (struct ingame_cheat's flags)
enum ingame_why {
	INGAME_WHY_NONE,  // it can
	INGAME_WHY_ROM,  // off: it changes the ROM, which is patched at launch only
	INGAME_WHY_LOCKED,  // on until Back to Torii: it changes the ROM, or is past the toggles' 64
	INGAME_WHY_CANT,  // Torii can't run it (unreadable, a placeholder, too big alone)
	INGAME_WHY_ROOM,  // no room in the launch's table, or picked and left out at launch
	INGAME_WHY_SLOW,  // the overlay's own: on, the cheats on would pass the cycle cap
	INGAME_WHYS
};
#define INGAME_CHEAT_WHY 0x07  // flags: enum ingame_why
#define INGAME_CHEAT_ON 0x08  // flags: on at launch (picked, and running or patched in)
#define INGAME_CHEAT_TABLE 0x10  // flags: in the interpreter's table, its toggle bit in bit
#define INGAME_CHEAT_CUT 0x20  // flags: its name is cut in its row: whole at INGAME_SRAM_WIDE (the marquee)
#define INGAME_CHEAT_RUN_ONCE \
	0x40  // flags: in the table, and its code waits on no buttons of its own: SELECT runs it once

// A cheat of the in-game list (P2), the kernel's at launch
struct ingame_cheat {
	struct ingame_line line;  // its name
	uint16_t cost;  // its cycles a frame while on (cheatcode.h's model), with INGAME_CHEAT_TABLE
	uint16_t number;  // its cheatN_ number in the file
	uint16_t index;  // its place in the file's cheats (cheatfile.h), for Back to Torii
	uint8_t flags;  // INGAME_CHEAT_*
	uint8_t bit;  // its toggle (the table's cheat), with INGAME_CHEAT_TABLE
	// Its activation's sentence (cheatkeys.h): act_text[act - 1]; 0 for
	// none (not known, or past INGAME_ACT_LINES distinct ones)
	uint8_t act;
	uint8_t reserved[3];
};

// A cheat's name whole, for the marquee of its highlighted row (one with
// INGAME_CHEAT_CUT): as struct ingame_line, but INGAME_WIDE_W pixels wide
// (a longer name ends in "..." there). Entry n's at INGAME_SRAM_WIDE + n *
// sizeof(struct ingame_wide), in cart SRAM's page 0x30 (free since v0.8:
// the stub's backups end at 0x2DA00).
#define INGAME_SRAM_WIDE 0x30000
#define INGAME_WIDE_W 512
struct ingame_wide {
	uint16_t width;  // inked pixels from the left, at most INGAME_WIDE_W
	uint16_t reserved;
	uint8_t bits[INGAME_TEXT_ROWS][INGAME_WIDE_W / 8];
};

// The overlay's answer, for the kernel's next boot (P2 and P3 fill it)
#define INGAME_OUT_MAGIC 0x3254554Fu  // "OUT2"
struct ingame_out {
	uint32_t magic;  // INGAME_OUT_MAGIC once written
	uint16_t action;  // the last INGAME_ACT_*
	uint16_t pending_state;  // a state slot to write to SD (P3); 0xFFFF none
	uint16_t screenshot;  // the screenshots taken in the session, at INGAME_SRAM_SHOT (P4)
	uint16_t cheats_changed;  // 1 once the list changed a toggle: cheat_on holds the picks
	uint32_t cheat_on[2];  // the listed cheats on at the last close, bit n for list entry n
};

struct ingame_session {
	uint32_t magic;  // INGAME_SESSION_MAGIC
	uint16_t version;  // INGAME_SESSION_VERSION
	uint16_t bytes;  // sizeof(struct ingame_session)
	uint32_t rom_crc;  // CRC32 of rom_path then save_slot: whose a state tag is (P3)
	uint32_t rom_bytes;  // the ROM file's size
	char name[INGAME_NAME_BYTES];  // the game's name (UTF-8, cut, NUL-ended)
	uint16_t palette[INGAME_PALETTE];  // the theme's UI bank 0 (enum ui_role), RGB15
	uint16_t menu_key;  // the menu key: a mask of KEYINPUT bits (1 = part of it)
	uint16_t sleep_key;  // the sleep key, the same way
	uint16_t rtc_status;  // the launch's 0x096A0000 value
	uint16_t flags;  // INGAME_SLEEP_KEY_ON...
	uint8_t state_slots;  // P3: INGAME_STATE_SLOTS, or 0 for the old Save and Load rows
	uint8_t state_have;  // bit n - 1: slot n has a state on the card
	// The clock at the launch (as struct ingame_shot's clock), which a
	// screenshot's time must follow
	uint8_t launch_clock[8];
	// P2: the cheat list; costs are cheatcode.h's loads (CHEAT_LOAD)
	uint16_t cheat_count;  // the list's cheats (0: the old Cheats on/off row)
	uint16_t cheat_table;  // the cheats in the interpreter's table
	uint16_t cheat_call;  // the interpreter's call, the table's end included
	uint16_t cheat_skip;  // a cheat of the table that is off
	uint16_t cheat_max;  // the cap on them all
	// The cheat search (v0.9): the live block's address (0: none, set once
	// instead)
	uint32_t live_at;
	uint32_t launch_id;  // differs at each launch: a search is this launch's only (struct ingame_search)
	// The check word (ingame_image_check) of the image's bytes past
	// image_bytes: the search's code, which the overlay loads itself
	uint32_t ext_check;
	// Each text line's width (struct ingame_line's), so the overlay lays
	// out the menu without reading the lines from cart SRAM
	uint16_t text_width[(INGAME_TEXT_SEARCH + 1) & ~1];  // the search's lines' are in their lines
	struct ingame_line text[INGAME_TEXT_SEARCH];  // the overlay reads one at a time; the rest at INGAME_SRAM_TEXTS
	struct ingame_cheat cheats[INGAME_CHEATS];
	// The cheats' activation sentences ("Hold L+SELECT"), each once
	struct ingame_line act_text[INGAME_ACT_LINES];
	// P3: each slot's row as the card has it ("Slot 1 · 27 Oct 14:03",
	// "Slot 2 · empty"), and with a state saved in game not kept yet
	struct ingame_line state_kept[INGAME_STATE_SLOTS];
	struct ingame_line state_unkept[INGAME_STATE_SLOTS];
	char save_slot[INGAME_SAVE_SLOT_BYTES];  // the game's save slot, whose states these are
	char rom_path[INGAME_PATH_BYTES];  // the ROM's path from the card's root, for the kernel's next boot
	struct ingame_out out;  // written by the overlay
};

// The part the overlay reads at every opening; the lines and cheats it reads one at a time
#define INGAME_SESSION_HEAD offsetof(struct ingame_session, text)
#define INGAME_SESSION_TEXT(n) (offsetof(struct ingame_session, text) + (n) * sizeof(struct ingame_line))
// Text n's line in cart SRAM: the session block's, and from the cheat
// search's (v0.9) on, the block after the whole names, which the session
// block has no room for
#define INGAME_SRAM_TEXTS 0x3C400
#define INGAME_SRAM_GLYPHS 0x3F000  // struct ingame_glyph[INGAME_GLYPHS]
#define INGAME_TEXT_SRAM(n) \
	((n) < INGAME_TEXT_SEARCH ? INGAME_SRAM_SESSION + INGAME_SESSION_TEXT(n) \
				  : INGAME_SRAM_TEXTS + ((n) - INGAME_TEXT_SEARCH) * sizeof(struct ingame_line))
#define INGAME_SESSION_CHEAT(n) (offsetof(struct ingame_session, cheats) + (n) * sizeof(struct ingame_cheat))
#define INGAME_SESSION_ACT(n) (offsetof(struct ingame_session, act_text) + (n) * sizeof(struct ingame_line))
#define INGAME_SESSION_PATH offsetof(struct ingame_session, rom_path)
#define INGAME_SESSION_SAVE_SLOT offsetof(struct ingame_session, save_slot)
#define INGAME_SESSION_STATE(n, kept) \
	(((kept) ? offsetof(struct ingame_session, state_kept) : offsetof(struct ingame_session, state_unkept)) + \
	 ((n) - 1) * sizeof(struct ingame_line))
#define INGAME_SESSION_OUT offsetof(struct ingame_session, out)

_Static_assert(sizeof(struct ingame_image) == 32, "the stub reads the header's words");
_Static_assert(offsetof(struct ingame_image, image_bytes) == 8, "stub: image_bytes");
_Static_assert(offsetof(struct ingame_image, window_bytes) == 12, "stub: window_bytes");
_Static_assert(offsetof(struct ingame_image, entry) == 16, "stub: entry");
_Static_assert(offsetof(struct ingame_image, obj_bytes) == 20, "stub: obj_bytes");
_Static_assert(offsetof(struct ingame_image, shown) == 24, "hook_watch.lua: shown");
_Static_assert(offsetof(struct ingame_image, check) == 28, "stub: check");
_Static_assert(sizeof(struct ingame_refused) == 32 && offsetof(struct ingame_refused, reason) == 8 &&
		       offsetof(struct ingame_refused, waitcnt) == 10 && offsetof(struct ingame_refused, at) == 16 &&
		       offsetof(struct ingame_refused, vcount) == 28,
	       "the stub writes the refusal record");
_Static_assert(sizeof(void *) != 4 || sizeof(struct ingame_call) == 16, "the stub builds it (on the GBA)");
_Static_assert(INGAME_SESSION_HEAD % 4 == 0 && INGAME_SESSION_OUT % 4 == 0, "sram_copy takes words");
_Static_assert(sizeof(struct ingame_out) % 4 == 0, "sram_copy takes words");
_Static_assert(sizeof(struct ingame_line) % 4 == 0 && INGAME_SESSION_TEXT(0) % 4 == 0, "sram_copy takes words");
_Static_assert(sizeof(struct ingame_cheat) % 4 == 0 && INGAME_SESSION_CHEAT(0) % 4 == 0, "sram_copy takes words");
_Static_assert(INGAME_SESSION_ACT(0) % 4 == 0 && sizeof(struct ingame_wide) % 4 == 0, "sram_copy takes words");
_Static_assert(INGAME_SRAM_WIDE >= INGAME_BACKUP_SHOT + INGAME_BACKUP_SHOT_BYTES &&
		       INGAME_SRAM_WIDE + INGAME_CHEATS * sizeof(struct ingame_wide) <= INGAME_SRAM_STATE &&
		       INGAME_SRAM_WIDE % 0x10000 == 0,
	       "the whole names: after the backups, before the state buffer, in one 64 KB page window");
_Static_assert(INGAME_SESSION_PATH % 4 == 0 && INGAME_PATH_BYTES % 4 == 0, "sram_copy takes words");
_Static_assert(INGAME_WHYS <= INGAME_CHEAT_WHY + 1, "a reason fits its bits");
_Static_assert(sizeof(struct ingame_state_tag) == 16 && offsetof(struct ingame_state_tag, autoload) == 6 &&
		       INGAME_SRAM_STATE_MARK + 16 == INGAME_SRAM_STATE_TAG &&
		       INGAME_SRAM_STATE_TAG + 16 == INGAME_SRAM_RTS_FLAG && sizeof(INGAME_STATE_MARK) == 17,
	       "the stub reads the tag's autoload byte at 0xAFFE6");
_Static_assert(INGAME_SESSION_SAVE_SLOT % 4 == 0, "sram_copy takes words");
_Static_assert(sizeof(struct ingame_state_info) == 32 && INGAME_SRAM_STATE_INFO % 4 == 0 &&
		       INGAME_SRAM_STATE_INFO >= INGAME_SRAM_STATE + 0x69400 &&
		       INGAME_SRAM_STATE_INFO + sizeof(struct ingame_state_info) <= INGAME_SRAM_STATE_MARK,
	       "the info sits after the stub's I/O copy (ST_IO + 0x400), before the mark");
_Static_assert(sizeof(struct ingame_session) % 4 == 0 && sizeof(struct ingame_session) <= INGAME_SESSION_MAX,
	       "the session block's room");
_Static_assert(INGAME_BACKUP_EWRAM + INGAME_WINDOW_MAX <= INGAME_BACKUP_OBJ &&
		       INGAME_BACKUP_OBJ + INGAME_OBJ_MAX <= INGAME_BACKUP_OAM &&
		       INGAME_BACKUP_OAM + 0x400 <= INGAME_BACKUP_OBJ_PAL &&
		       INGAME_BACKUP_OBJ_PAL + 0x200 <= INGAME_BACKUP_SHOT &&
		       INGAME_BACKUP_SHOT + INGAME_BACKUP_SHOT_BYTES <= INGAME_BACKUP_EWRAM + 0x10000,
	       "the backups share one 64 KB page window and don't overlap");
_Static_assert(INGAME_SRAM_IMAGE + INGAME_IMAGE_MAX <= INGAME_SRAM_SESSION, "image room");
_Static_assert(sizeof(struct ingame_search) % 4 == 0 && sizeof(struct ingame_found) % 4 == 0 &&
		       sizeof(struct ingame_found_cheat) == 12 && sizeof(struct ingame_glyph) == 16,
	       "sram_copy takes words");
_Static_assert(INGAME_SRAM_SEARCH_SNAP == INGAME_SRAM_STATE &&
		       INGAME_SRAM_SEARCH_SNAP + 0x47E00 <= INGAME_SRAM_SEARCH_BITS &&
		       INGAME_SRAM_SEARCH_BITS + 36800 <= INGAME_SRAM_SEARCH &&
		       INGAME_SRAM_SEARCH + sizeof(struct ingame_search) <= INGAME_SRAM_STATE_INFO,
	       "the search in the save-state buffer, clear of its info, mark and tag");
_Static_assert(INGAME_SRAM_FOUND >= INGAME_BACKUP_SHOT + INGAME_BACKUP_SHOT_BYTES &&
		       INGAME_SRAM_FOUND + sizeof(struct ingame_found) <= INGAME_SRAM_WIDE,
	       "the found cheats after the backups, before the whole names");
_Static_assert(4 + INGAME_LIVE_PAIRS * 8 <= INGAME_LIVE_BYTES, "the live block's words");
_Static_assert(INGAME_SRAM_TEXTS >= INGAME_SRAM_WIDE + INGAME_CHEATS * sizeof(struct ingame_wide) &&
		       INGAME_TEXT_SRAM(INGAME_TEXTS) <= INGAME_SRAM_STATE &&
		       INGAME_TEXT_SRAM(INGAME_TEXTS) <= (INGAME_SRAM_TEXTS & ~0xFFFFu) + 0x10000,
	       "the search's lines after the whole names, before the state buffer, in their 64 KB page window");
_Static_assert(INGAME_TEXT_SRAM(INGAME_TEXTS) <= INGAME_SRAM_GLYPHS &&
		       INGAME_SRAM_GLYPHS + INGAME_GLYPHS * sizeof(struct ingame_glyph) <= INGAME_SRAM_STATE,
	       "the glyphs after the search's lines");
_Static_assert(sizeof(struct ingame_shot) <= INGAME_DUMP_PAL && sizeof(struct ingame_shot) % 4 == 0 &&
		       offsetof(struct ingame_shot, io) % 4 == 0,
	       "the screenshot's header");
_Static_assert(INGAME_SRAM_SHOT + sizeof(struct ingame_shot) + INGAME_SHOT_PACKED_MAX <= INGAME_SHOTS_END &&
		       INGAME_SHOTS_END <= 0xF0000,
	       "a screenshot fits, clear of the cart log at 0xF0000");

#endif
