// A game's save states on the card (statestore.h).
#include "statestore.h"

#include <gba_compression.h>
#include <stdio.h>
#include <string.h>

#include "crc32.h"
#include "cutpoint.h"
#include "ez_define.h"
#include "Ezcard_OP.h"
#include "ff.h"
#include "lang.h"
#include "lz77.h"
#include "progress.h"
#include "slotstore.h"
#include "tlog.h"
#include "waitcnt.h"

extern u8 pReadCache[];

_Static_assert(sizeof(struct statestore_head) == 64, "a state file's head");
_Static_assert(INGAME_STATE_BYTES % STATESTORE_CHUNK == 0, "whole chunks");
_Static_assert(STATESTORE_CHUNK <= LZ77_FAST_MAX, "a chunk packs at once");

// pReadCache from READCACHE_STATES: a chunk, it packed (or on its way in),
// the packer's hash, a path and the file
struct states_work {
	u8 raw[STATESTORE_CHUNK];
	u8 packed[(LZ77_MAX_OUT(STATESTORE_CHUNK) + 3) & ~3u];
	u16 head[LZ77_HASH];
	char path[STATESTORE_PATH_BYTES];
	FIL fil;
};
_Static_assert(sizeof(struct states_work) <= READCACHE_STATES_SIZE, "the states' share of pReadCache");
_Static_assert(READCACHE_STATES + READCACHE_STATES_SIZE <= MAX_pReadCache_size, "READCACHE_STATES");

static struct states_work *work(void)
{
	return (struct states_work *)(void *)(pReadCache + READCACHE_STATES);
}

#define CART_SRAM ((vu8 *)0x0E000000)

void IWRAM_CODE statestore_sram_bytes(vu8 *sram, u8 *ram, u32 n, u32 to_sram)
{
	if (to_sram)
		while (n--)
			*sram++ = *ram++;
	else
		while (n--)
			*ram++ = *sram++;
}

// n bytes of the state buffer from offset at (within it) to or from RAM,
// a page window at a time
static void buffer_io(u32 at, u8 *ram, u32 n, int to_sram)
{
	while (n) {
		u32 sram = INGAME_SRAM_STATE + at;
		u32 room = 0x10000 - (sram & 0xFFF), part = n < room ? n : room;
		SetRampage((u16)(sram >> 12));
		statestore_sram_bytes(CART_SRAM + (sram & 0xFFF), ram, part, (u32)to_sram);
		at += part;
		ram += part;
		n -= part;
	}
	SetRampage(0);
}

static const char state_flag[16] = {'E', 'Z', '-', 'O', 'm', 'e', 'g', 'a', 'R', 'T', 'C', 'F', 'I', 'L', 'E', '.'};
#define TAG_AT (INGAME_SRAM_STATE_TAG - INGAME_SRAM_STATE)
#define FLAG_AT (INGAME_SRAM_STATE_FLAG - INGAME_SRAM_STATE)

int statestore_buffer(struct ingame_state_tag *tag)
{
	u8 flag[16];
	buffer_io(TAG_AT, (u8 *)tag, sizeof(*tag), 0);
	buffer_io(FLAG_AT, flag, sizeof(flag), 0);
	return memcmp(flag, state_flag, sizeof(flag)) == 0 && tag->magic == INGAME_STATE_TAG_MAGIC;
}

static void tag_clear(void);
static void flag_set(int on);

// The flag first: with it cleared, no write to the buffer's last page can
// start the FPGA's write-back (fattable.h), whatever table it holds (after a
// cold boot, none: zeros, the card's first sectors)
void statestore_buffer_clear(void)
{
	flag_set(0);
	tag_clear();
}

static void flag_set(int on)
{
	u8 flag[16];
	if (on)
		memcpy(flag, state_flag, sizeof(flag));
	else
		memset(flag, 0, sizeof(flag));
	buffer_io(FLAG_AT, flag, sizeof(flag), 1);
}

static void tag_clear(void)
{
	struct ingame_state_tag tag;
	memset(&tag, 0, sizeof(tag));
	buffer_io(TAG_AT, (u8 *)&tag, sizeof(tag), 1);
}

static void tag_set(u32 slot, u32 kept, u32 autoload, u32 rom_crc)
{
	struct ingame_state_tag tag;
	memset(&tag, 0, sizeof(tag));
	tag.magic = INGAME_STATE_TAG_MAGIC;
	tag.slot = (u8)slot;
	tag.kept = (u8)kept;
	tag.autoload = (u8)autoload;
	tag.rom_crc = rom_crc;
	buffer_io(TAG_AT, (u8 *)&tag, sizeof(tag), 1);
}

int statestore_path(char *out, const char *rom_path, const char *save_slot, u32 slot, u32 bak)
{
	int n;
	if (!slot)
		n = snprintf(out, STATESTORE_PATH_BYTES, "%s%s/%s", STATESTORE_DIR, rom_path, save_slot);
	else
		n = snprintf(out, STATESTORE_PATH_BYTES, "%s%s/%s/%lu.state%s", STATESTORE_DIR, rom_path, save_slot,
			     (unsigned long)slot, bak ? ".bak" : "");
	if (n < 0 || n >= STATESTORE_PATH_BYTES || rom_path[0] != '/' || !save_slot[0]) {
		out[0] = 0;
		return -1;
	}
	return n;
}

static int head_ok(const struct statestore_head *h, u32 slot)
{
	return memcmp(h->magic, STATESTORE_MAGIC, 8) == 0 && h->head_bytes == sizeof(*h) &&
	       h->raw_bytes == INGAME_STATE_BYTES && h->slot == slot &&
	       h->head_crc == crc32(h, offsetof(struct statestore_head, head_crc));
}

// Reads the head of the copy at path into h: 1 if it holds up
static int head_read(const char *path, u32 slot, struct statestore_head *h)
{
	FIL *f = &work()->fil;
	UINT got = 0;
	if (f_open(f, path, FA_READ) != FR_OK)
		return 0;
	FRESULT r = f_read(f, h, sizeof(*h), &got);
	int ok = r == FR_OK && got == sizeof(*h) && head_ok(h, slot) && f_size(f) == sizeof(*h) + h->data_bytes;
	f_close(f);
	return ok;
}

u32 statestore_scan(const char *rom_path, const char *save_slot, struct statestore_list *l)
{
	memset(l, 0, sizeof(*l));
	u32 n = 0;
	char *path = work()->path;
	for (u32 s = 1; s <= STATESTORE_SLOTS; s++) {
		struct statestore_slot *e = &l->slot[s];
		for (u32 bak = 0; bak < 2; bak++) {
			u32 h_words[sizeof(struct statestore_head) / 4];  // word-aligned for f_read
			struct statestore_head *h = (struct statestore_head *)(void *)h_words;
			if (statestore_path(path, rom_path, save_slot, s, bak) < 0 || !head_read(path, s, h))
				continue;
			if (!e->have || h->seq > e->seq) {
				e->have = 1;
				e->bak = (u8)bak;
				e->seq = h->seq;
				e->order = h->order;
				e->when = h->when;
			}
		}
		n += e->have;
	}
	return n;
}

u32 statestore_newest(const struct statestore_list *l)
{
	u32 best = 0;
	for (u32 s = 1; s <= STATESTORE_SLOTS; s++)
		if (l->slot[s].have && (!best || l->slot[s].order > l->slot[best].order))
			best = s;
	return best;
}

// Unpacks the copy at path into the buffer: 1 if it read whole and its CRC holds
static int copy_load(const char *path, u32 slot)
{
	struct states_work *w = work();
	FIL *f = &w->fil;
	UINT got = 0;
	u32 h_words[sizeof(struct statestore_head) / 4];
	struct statestore_head *h = (struct statestore_head *)(void *)h_words;
	if (f_open(f, path, FA_READ) != FR_OK)
		return 0;
	int ok = f_read(f, h, sizeof(*h), &got) == FR_OK && got == sizeof(*h) && head_ok(h, slot);
	u32 crc = 0, at = 0, left = ok ? h->data_bytes : 0;
	while (ok && at < INGAME_STATE_BYTES) {
		u32 sizes[2];
		ok = left >= 8 && f_read(f, sizes, 8, &got) == FR_OK && got == 8 && sizes[0] == STATESTORE_CHUNK &&
		     sizes[1] <= sizeof(w->packed) && sizes[1] % 4 == 0 && sizes[1] + 8 <= left;
		if (!ok)
			break;
		crc = crc32_update(crc, sizes, 8);
		ok = f_read(f, w->packed, sizes[1], &got) == FR_OK && got == sizes[1];
		if (!ok)
			break;
		crc = crc32_update(crc, w->packed, sizes[1]);
		left -= 8 + sizes[1];
		u32 size = w->packed[1] | w->packed[2] << 8 | (u32)w->packed[3] << 16;
		ok = w->packed[0] == 0x10 && size == STATESTORE_CHUNK;
		if (!ok)
			break;
		LZ77UnCompWram(w->packed, w->raw);
		buffer_io(at, w->raw, STATESTORE_CHUNK, 1);
		at += STATESTORE_CHUNK;
	}
	f_close(f);
	ok = ok && left == 0 && crc == h->data_crc;
	if (!ok)
		tlog_error("states: %s doesn't read whole", path);
	return ok;
}

static int load_slot(const char *rom_path, const char *save_slot, u32 slot, u32 rom_crc, u32 autoload)
{
	struct statestore_list l;
	char *path = work()->path;
	flag_set(0);
	statestore_scan(rom_path, save_slot, &l);
	if (slot < 1 || slot > STATESTORE_SLOTS || !l.slot[slot].have)
		return 0;
	// The newer copy first, then the other
	for (u32 k = 0; k < 2; k++) {
		u32 bak = k ? !l.slot[slot].bak : l.slot[slot].bak;
		if (statestore_path(path, rom_path, save_slot, slot, bak) < 0 || !copy_load(path, slot))
			continue;
		tag_set(slot, 1, autoload, rom_crc);
		flag_set(1);
		tlog("states: slot %lu in cart SRAM from %s%s", (unsigned long)slot, path,
		     autoload ? ", loaded at the game's start" : "");
		return 1;
	}
	flag_set(0);  // a copy that failed its CRC left its chunks, and its flag
	return 0;
}

// Makes the folders of path's parents, as mkdir -p (path is changed and
// put back): 1 if the last one is a folder now
static int make_parents(char *path)
{
	u32 len = (u32)strlen(path);
	for (u32 i = 1; i < len; i++) {
		if (path[i] != '/')
			continue;
		path[i] = 0;
		FRESULT r = f_mkdir(path);
		if (r != FR_OK && r != FR_EXIST) {
			tlog_error("states: can't make %s (FatFs %d)", path, r);
			path[i] = '/';
			return 0;
		}
		if (r == FR_OK)
			cutpoint("states: a folder made");
		path[i] = '/';
	}
	return 1;
}

// Writes the buffer to the copy at path, its head last (seq and order),
// moving the progress dialog's bar
static int copy_write(char *path, u32 slot, u32 seq, u32 order)
{
	struct states_work *w = work();
	FIL *f = &w->fil;
	UINT put = 0;
	u32 h_words[sizeof(struct statestore_head) / 4];
	struct statestore_head *h = (struct statestore_head *)(void *)h_words;
	memset(h, 0, sizeof(*h));
	FRESULT r = make_parents(path) ? f_open(f, path, FA_CREATE_ALWAYS | FA_WRITE) : FR_DENIED;
	if (r != FR_OK) {
		tlog_error("states: can't write %s (FatFs %d)", path, r);
		return 0;
	}
	// A head of zeros first: a copy cut short never holds up
	int ok = f_write(f, h, sizeof(*h), &put) == FR_OK && put == sizeof(*h);
	cutpoint("states: a copy's blank head written");
	u32 crc = 0, data = 0;
	for (u32 at = 0; ok && at < INGAME_STATE_BYTES; at += STATESTORE_CHUNK) {
		buffer_io(at, w->raw, STATESTORE_CHUNK, 0);
		if (at + STATESTORE_CHUNK == INGAME_STATE_BYTES) {
			// The tag as the file keeps it: kept, no autoload
			struct ingame_state_tag *tag =
				(struct ingame_state_tag *)(void *)(w->raw + STATESTORE_CHUNK - 32);
			tag->kept = 1;
			tag->autoload = 0;
		}
		u32 sizes[2] = {STATESTORE_CHUNK, lz77_compress_fast(w->raw, STATESTORE_CHUNK, w->packed, w->head)};
		ok = f_write(f, sizes, 8, &put) == FR_OK && put == 8 &&
		     f_write(f, w->packed, sizes[1], &put) == FR_OK && put == sizes[1];
		crc = crc32_update(crc32_update(crc, sizes, 8), w->packed, sizes[1]);
		data += 8 + sizes[1];
		progress_part(at / STATESTORE_CHUNK + 1, INGAME_STATE_BYTES / STATESTORE_CHUNK + 1);
	}
	ok = ok && f_sync(f) == FR_OK;
	cutpoint("states: a copy's chunks written");
	memcpy(h->magic, STATESTORE_MAGIC, 8);
	h->head_bytes = sizeof(*h);
	h->raw_bytes = INGAME_STATE_BYTES;
	h->data_bytes = data;
	h->data_crc = crc;
	h->seq = seq;
	h->order = order;
	slotstore_now(&h->when);
	h->slot = (u8)slot;
	h->head_crc = crc32(h, offsetof(struct statestore_head, head_crc));
	ok = ok && f_lseek(f, 0) == FR_OK && f_write(f, h, sizeof(*h), &put) == FR_OK && put == sizeof(*h);
	ok = f_close(f) == FR_OK && ok;
	cutpoint("states: a copy's head written and closed");
	tlog("states: %s: %lu bytes of %u packed, %s", path, (unsigned long)data, INGAME_STATE_BYTES,
	     ok ? "written" : "not written");
	return ok;
}

static int keep_slot(const char *rom_path, const char *save_slot, u32 slot)
{
	struct statestore_list l;
	char *path = work()->path;
	if (slot < 1 || slot > STATESTORE_SLOTS)
		return 0;
	statestore_scan(rom_path, save_slot, &l);
	u32 order = 0;
	for (u32 s = 1; s <= STATESTORE_SLOTS; s++)
		if (l.slot[s].have && l.slot[s].order > order)
			order = l.slot[s].order;
	// The copy a reader wouldn't take now
	const struct statestore_slot *e = &l.slot[slot];
	u32 bak = e->have ? !e->bak : 0;
	if (statestore_path(path, rom_path, save_slot, slot, bak) < 0) {
		tlog_error("states: the path for slot %lu of %s is too long", (unsigned long)slot, rom_path);
		return 0;
	}
	if (!copy_write(path, slot, e->have ? e->seq + 1 : 1, order + 1))
		return 0;
	// Kept: the next boot doesn't file it again. The flag goes first and
	// stays cleared: at the boot after a cold one the FPGA's table is zeros,
	// and a write to the buffer's page with the flag set may start its
	// write-back (fattable.h). A launch puts a state back from its file.
	struct ingame_state_tag tag;
	if (statestore_buffer(&tag)) {
		flag_set(0);
		tag.kept = 1;
		buffer_io(TAG_AT, (u8 *)&tag, sizeof(tag), 1);
	}
	return 1;
}

// The official kernel's name for the game's state: the ROM's file name with
// .rts for its extension (suffix: "" or ".imported", the name once taken)
static int rts_path(char *out, u32 size, const char *rom_name, const char *suffix)
{
	const char *dot = strrchr(rom_name, '.');
	int len = dot && dot != rom_name ? (int)(dot - rom_name) : (int)strlen(rom_name);
	int n = snprintf(out, size, "/RTS/%.*s.rts%s", len, rom_name, suffix);
	return n >= 0 && (u32)n < size;
}

static int import_rts(const char *rom_path, const char *save_slot, const char *rom_name, u32 rom_crc)
{
	struct statestore_list l;
	if (statestore_scan(rom_path, save_slot, &l))
		return 0;
	struct states_work *w = work();
	char *path = w->path;
	FIL *f = &w->fil;
	if (!rts_path(path, STATESTORE_PATH_BYTES, rom_name, "") || f_open(f, path, FA_READ) != FR_OK)
		return 0;
	int ok = f_size(f) == INGAME_STATE_BYTES;
	UINT got = 0;
	for (u32 at = 0; ok && at < INGAME_STATE_BYTES; at += STATESTORE_CHUNK) {
		ok = f_read(f, w->raw, STATESTORE_CHUNK, &got) == FR_OK && got == STATESTORE_CHUNK;
		if (ok && at + STATESTORE_CHUNK == INGAME_STATE_BYTES)
			ok = memcmp(w->raw + STATESTORE_CHUNK - 16, state_flag, 16) == 0;  // a state was saved
		if (ok)
			buffer_io(at, w->raw, STATESTORE_CHUNK, 1);
	}
	f_close(f);
	if (!ok) {
		flag_set(0);
		return 0;
	}
	tlog("states: %s becomes slot 1 of %s", path, rom_path);
	tag_set(1, 0, 0, rom_crc);
	flag_set(1);
	if (!keep_slot(rom_path, save_slot, 1))
		return 0;
	flag_set(1);  // keep_slot cleared it; a launch's table names /torii/rts.bin
	// Taken once: renamed, so the game's other save slots (each a folder of
	// states of its own, empty) don't take it again at every launch. The
	// buffers are keep_slot's, free again
	char *to = (char *)w->raw;
	if (rts_path(path, STATESTORE_PATH_BYTES, rom_name, "") &&
	    rts_path(to, STATESTORE_CHUNK, rom_name, ".imported")) {
		f_unlink(to);  // an older import's
		FRESULT fr = f_rename(path, to);
		cutpoint("states: the RTS file renamed");
		if (fr != FR_OK)
			tlog_error("states: %s not renamed (FatFs %d): it is taken again", path, (int)fr);
	}
	return 1;
}

// At the menu's wait states (a launch runs at the BIOS's): the packing, the
// unpacking and cart SRAM's bytes take a third of the time. SetRampage and
// the card's driver set their own.
int statestore_load(const char *rom_path, const char *save_slot, u32 slot, u32 rom_crc, u32 autoload)
{
	u16 waitcnt = REG_WAITCNT_CART;
	REG_WAITCNT_CART = WAITCNT_MENU;
	int r = load_slot(rom_path, save_slot, slot, rom_crc, autoload);
	REG_WAITCNT_CART = waitcnt;
	return r;
}

int statestore_keep(const char *rom_path, const char *save_slot, u32 slot)
{
	u16 waitcnt = REG_WAITCNT_CART;
	REG_WAITCNT_CART = WAITCNT_MENU;
	int r = keep_slot(rom_path, save_slot, slot);
	REG_WAITCNT_CART = waitcnt;
	return r;
}

int statestore_import_rts(const char *rom_path, const char *save_slot, const char *rom_name, u32 rom_crc)
{
	u16 waitcnt = REG_WAITCNT_CART;
	REG_WAITCNT_CART = WAITCNT_MENU;
	int r = import_rts(rom_path, save_slot, rom_name, rom_crc);
	REG_WAITCNT_CART = waitcnt;
	return r;
}

void statestore_row(char *out, u32 size, u32 slot, const struct statestore_slot *s, int unkept)
{
	if (unkept)
		snprintf(out, size, EN_STATE_ROW_UNKEPT, (unsigned long)slot);
	else if (!s || !s->have)
		snprintf(out, size, EN_STATE_ROW_EMPTY, (unsigned long)slot);
	else if (!s->when.year || s->when.month < 1 || s->when.month > 12)
		snprintf(out, size, EN_STATE_ROW_SAVED, (unsigned long)slot);
	else
		snprintf(out, size, EN_STATE_ROW_WHEN, (unsigned long)slot, s->when.day,
			 gl_states->months[s->when.month - 1], s->when.hour, s->when.min);
}
