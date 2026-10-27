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
#define INGAME_SRAM_STATE_FLAG 0xAFFF0  // "EZ-OmegaRTCFILE." once a state is saved
#define INGAME_SRAM_STATE_TAG 0xAFFE0  // struct ingame_state_tag (P3)
#define INGAME_STATE_BYTES 0x70000  // the buffer, tag and flag included
#define INGAME_SRAM_IMAGE 0xB0000  // the overlay's image
#define INGAME_IMAGE_MAX 0x8000
#define INGAME_SRAM_SESSION 0xB8000  // struct ingame_session
#define INGAME_SESSION_MAX 0x8000
#define INGAME_SRAM_SHOT 0xC0000  // the screenshot dump (later), to 0xD8FFF

// The backups, from INGAME_SRAM_BACKUP: all in one 64 KB page window
#define INGAME_BACKUP_EWRAM 0x20000  // the EWRAM window, window_bytes
#define INGAME_BACKUP_OBJ 0x28000  // OBJ tiles from INGAME_OBJ_VRAM, obj_bytes
#define INGAME_BACKUP_OAM 0x2C000  // 1 KB
#define INGAME_BACKUP_OBJ_PAL 0x2C400  // 512 bytes

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
	uint32_t reserved;
};

// --- The call: on the overlay's stack, from the stub
// Flags: what the game's patch offers (the stub's switches, which a NOR copy
// keeps from when it was written)
#define INGAME_CAN_STATES (1u << 0)  // SAVE and LOAD (RTS_switch: SAVESTATE on)
#define INGAME_CAN_CHEATS (1u << 1)  // the cheat interpreter is in (Cheat_switch)
#define INGAME_CAN_DEBUG (1u << 2)  // the DEBUG snapshot is in (Debug_switch)
#define INGAME_NOTICE_DEBUG (1u << 8)  // reopened after a DEBUG snapshot: say so
struct ingame_call {
	// Copies n bytes (a multiple of 4) between cart SRAM at sram (a byte
	// offset) and word-aligned RAM: to RAM when to_sram is 0. It leaves the
	// SRAM page set; the stub puts page 0 back.
	void (*sram_copy)(uint32_t sram, void *ram, uint32_t n, uint32_t to_sram);
	uint32_t flags;  // INGAME_CAN_*, INGAME_NOTICE_*
};

// What the entry returns
enum {
	INGAME_ACT_RESUME = 0,
	INGAME_ACT_SAVE = 1,  // today's SAVE: the state into SRAM 0x40000-0xAFFFF
	INGAME_ACT_LOAD = 2,  // today's LOAD
	INGAME_ACT_RESET = 3,  // restart the game (SWI 0 with the ROM page left)
	INGAME_ACT_SLEEP = 4,
	INGAME_ACT_TORII = 5,  // back to the kernel (today's RESET)
	INGAME_ACT_DEBUG = 6,  // the DEBUG snapshot, then the menu again
	INGAME_ACTS
};

// The word the cheat interpreter reads (source/cheatrun.s): 1 stops it
#define INGAME_CHEAT_ONOFF 0x03007FE0
// The per-cheat toggles (P2): 64 bits, bit n flips the table's cheat n
// against the launch (source/cheatrun.s). In IWRAM's last 0x200 bytes, past
// the BIOS's SVC stack (from 0x3007FE0 down) and before its words at
// 0x3007FF0: a game's own code leaves them alone, and the BIOS's SoftReset
// zeroes them, back to the launch's cheats.
#define INGAME_CHEAT_TOGGLES 0x03007FE8

// --- The session block, at INGAME_SRAM_SESSION
#define INGAME_SESSION_MAGIC 0x31535354u  // "TSS1"
#define INGAME_SESSION_VERSION 2
#define INGAME_NAME_BYTES 48
#define INGAME_CHEATS 64  // the cheats the in-game list shows; the toggles' bits
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

// The menu's text, drawn by the kernel (the engine's body font) so the
// overlay needs no font: each line 1bpp, INGAME_TEXT_ROWS rows of
// INGAME_TEXT_W pixels, bit 0 of a row's first byte its leftmost pixel
#define INGAME_TEXT_W 160
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
	// P3: the state slots' pages' footers
	INGAME_TEXT_STATE_KEEP,  // a save is kept on the card on Back to Torii
	INGAME_TEXT_STATE_REPLACES,  // saving here drops the state not kept yet
	INGAME_TEXT_STATE_LOAD,  // A loads it
	INGAME_TEXT_STATE_ELSEWHERE,  // another slot: load it from Torii
	INGAME_TEXT_STATE_EMPTY,  // nothing in this slot
	INGAME_TEXTS
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
	uint32_t reserved2;
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

// A cheat of the in-game list (P2), the kernel's at launch
struct ingame_cheat {
	struct ingame_line line;  // its name
	uint16_t cost;  // its cycles a frame while on (cheatcode.h's model), with INGAME_CHEAT_TABLE
	uint16_t number;  // its cheatN_ number in the file
	uint16_t index;  // its place in the file's cheats (cheatfile.h), for Back to Torii
	uint8_t flags;  // INGAME_CHEAT_*
	uint8_t bit;  // its toggle (the table's cheat), with INGAME_CHEAT_TABLE
};

// The overlay's answer, for the kernel's next boot (P2 and P3 fill it)
#define INGAME_OUT_MAGIC 0x3254554Fu  // "OUT2"
struct ingame_out {
	uint32_t magic;  // INGAME_OUT_MAGIC once written
	uint16_t action;  // the last INGAME_ACT_*
	uint16_t pending_state;  // a state slot to write to SD (P3); 0xFFFF none
	uint16_t screenshot;  // 1: a screenshot waits at INGAME_SRAM_SHOT (later)
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
	// P2: the cheat list; the cycles a frame are cheatcode.h's model
	uint16_t cheat_count;  // the list's cheats (0: the old Cheats on/off row)
	uint16_t cheat_table;  // the cheats in the interpreter's table
	uint16_t cheat_call;  // the interpreter's call, the table's end included
	uint16_t cheat_skip;  // a cheat of the table that is off
	uint16_t cheat_max;  // the cap on them all
	struct ingame_line text[INGAME_TEXTS];  // the overlay reads one at a time
	struct ingame_cheat cheats[INGAME_CHEATS];
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
#define INGAME_SESSION_CHEAT(n) (offsetof(struct ingame_session, cheats) + (n) * sizeof(struct ingame_cheat))
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
_Static_assert(sizeof(void *) != 4 || sizeof(struct ingame_call) == 8, "the stub builds it (on the GBA)");
_Static_assert(INGAME_SESSION_HEAD % 4 == 0 && INGAME_SESSION_OUT % 4 == 0, "sram_copy takes words");
_Static_assert(sizeof(struct ingame_out) % 4 == 0, "sram_copy takes words");
_Static_assert(sizeof(struct ingame_line) % 4 == 0 && INGAME_SESSION_TEXT(0) % 4 == 0, "sram_copy takes words");
_Static_assert(sizeof(struct ingame_cheat) % 4 == 0 && INGAME_SESSION_CHEAT(0) % 4 == 0, "sram_copy takes words");
_Static_assert(INGAME_SESSION_PATH % 4 == 0 && INGAME_PATH_BYTES % 4 == 0, "sram_copy takes words");
_Static_assert(INGAME_WHYS <= INGAME_CHEAT_WHY + 1, "a reason fits its bits");
_Static_assert(sizeof(struct ingame_state_tag) == 16 && offsetof(struct ingame_state_tag, autoload) == 6 &&
		       INGAME_SRAM_STATE_TAG + 16 == INGAME_SRAM_STATE_FLAG,
	       "the stub reads the tag's autoload byte at 0xAFFE6");
_Static_assert(INGAME_SESSION_SAVE_SLOT % 4 == 0, "sram_copy takes words");
_Static_assert(sizeof(struct ingame_session) % 4 == 0 && sizeof(struct ingame_session) <= INGAME_SESSION_MAX,
	       "the session block's room");
_Static_assert(INGAME_BACKUP_EWRAM + INGAME_WINDOW_MAX <= INGAME_BACKUP_OBJ &&
		       INGAME_BACKUP_OBJ + INGAME_OBJ_MAX <= INGAME_BACKUP_OAM &&
		       INGAME_BACKUP_OAM + 0x400 <= INGAME_BACKUP_OBJ_PAL &&
		       INGAME_BACKUP_OBJ_PAL + 0x200 <= INGAME_BACKUP_EWRAM + 0x10000,
	       "the backups share one 64 KB page window and don't overlap");
_Static_assert(INGAME_SRAM_IMAGE + INGAME_IMAGE_MAX <= INGAME_SRAM_SESSION, "image room");

#endif
