// A game's cheat file on the card, and its cheats at a launch (cheatstore.h).
#include "cheatstore.h"

#include <stdio.h>
#include <string.h>

#include "cheat.h"
#include "crc32.h"
#include "cutpoint.h"
#include "ez_define.h"
#include "Ezcard_OP.h"
#include "GBApatch.h"
#include "ff.h"
#include "namesdb.h"
#include "tlog.h"
#include "engine/cycles.h"
#include "engine/psram.h"

extern u8 pReadCache[];

_Static_assert(sizeof(struct cheatstore) <= READCACHE_CHEATS_SIZE, "the cheat file's share of pReadCache");
_Static_assert(READCACHE_CHEATS + READCACHE_CHEATS_SIZE <= MAX_pReadCache_size, "READCACHE_CHEATS");
_Static_assert(CHEATFILE_BYTES_MAX + 4 == CHEAT_TABLE_MAX, "a cheat file's budget is the table's");
_Static_assert(CHEATFILE_TEXT_MAX <= PSRAM_CHEATS_BYTES, "the text fits its PSRAM");
// cheatstore_put may write the byte past the room's last (as it was)
_Static_assert(PSRAM_CHEATS_BASE + PSRAM_CHEATS_BYTES + 2 <= PSRAM_NAMES_DB_BASE, "the cheats' PSRAM ends below "
										  "the names database");
_Static_assert((PSRAM_CHEATS_BASE & 3) == 0, "the cheats' PSRAM is word-aligned");

u32 gl_cheat_table[CHEAT_TABLE_MAX / 4] EWRAM_BSS;
u32 gl_cheat_bytes;
u32 gl_cheat_count;
u32 gl_cheat_patches;

struct cheatstore *cheatstore(void)
{
	return (struct cheatstore *)(void *)(pReadCache + READCACHE_CHEATS);
}

// --- The text in PSRAM ---------------------------------------------------------------
//
// PSRAM takes halfword DMA from RAM (psram_write), no byte stores: a write at
// an odd place or of an odd length takes the byte beside it along, as it is.

#define CHEATS_TEXT ((char *)PSRAM_CHEATS_BASE)

// n bytes from src (RAM or PSRAM, read before anything is written) to dst
// in PSRAM, a part at a time through the stage
void cheatstore_put(char *dst, const char *src, u32 n)
{
	struct cheatstore *cs = cheatstore();
	u8 *t = (u8 *)cs->stage;
	while (n) {
		u32 lead = (u32)dst & 1, c = n < CHEATSTORE_PART ? n : CHEATSTORE_PART;
		char *at = dst - lead;
		if (lead)
			t[0] = (u8)at[0];
		memcpy(t + lead, src, c);
		u32 total = lead + c;
		if (total & 1)
			t[total++] = (u8)dst[c];
		if (!psram_write(at, t, total)) {
			// The text is no longer what was read or edited: never written back
			if (!cs->room_failed)
				tlog_error("cheats: a PSRAM write at %08lX failed", (unsigned long)(u32)at);
			cs->room_failed = 1;
		}
		dst += c;
		src += c;
		n -= c;
	}
}

// memmove within PSRAM: parts from the end when the bytes move up
static void room_move(char *dst, const char *src, u32 n)
{
	if (dst < src) {
		for (u32 done = 0; done < n; done += CHEATSTORE_PART)
			cheatstore_put(dst + done, src + done, n - done < CHEATSTORE_PART ? n - done : CHEATSTORE_PART);
		return;
	}
	while (n) {
		u32 c = n < CHEATSTORE_PART ? n : CHEATSTORE_PART;
		n -= c;
		cheatstore_put(dst + n, src + n, c);
	}
}

static const struct cheatfile_room psram_room = {CHEATS_TEXT, PSRAM_CHEATS_BYTES, cheatstore_put, room_move};

// The open copy's text into PSRAM, a part at a time (never f_read straight
// into PSRAM: the card's DMA isn't known to reach it); its length in *len,
// though only CHEATFILE_TEXT_MAX bytes of it go in. 0 if a read failed.
static int load_text(u32 *len)
{
	struct cheatstore *cs = cheatstore();
	int n;
	*len = 0;
	psram_begin();
	while ((n = safefile_read_part(cs->part, sizeof(cs->part))) > 0) {
		if (*len + (u32)n <= CHEATFILE_TEXT_MAX)
			cheatstore_put(CHEATS_TEXT + *len, (const char *)cs->part, (u32)n);
		*len += (u32)n;
	}
	return n == 0;
}

int cheatstore_path(char *out, u32 size, const char *rom_path)
{
	const char *slash = strrchr(rom_path, '/');
	const char *name = slash ? slash + 1 : rom_path;
	const char *dot = strrchr(name, '.');
	u32 stem = dot && dot != name ? (u32)(dot - rom_path) : (u32)strlen(rom_path);
	const char *sep = rom_path[0] == '/' ? "" : "/";
	u32 n = (u32)snprintf(out, size, "%s%s%.*s.cht", CHEATS_DIR, sep, (int)stem, rom_path);
	if (n >= size || n + 4 >= SAFEFILE_PATH_MAX)
		return -1;
	return (int)n;
}

// 0 if f_stat says path is surely not on the card; a card error counts as
// there, so a file it hid is never written over
static int maybe_there(const char *path)
{
	FILINFO info;
	FRESULT r = f_stat(path, &info);
	return r != FR_NO_FILE && r != FR_NO_PATH;
}

// 1 if either copy of the cheat file at path (cheatstore()->path) may be on
// the card, though safefile_open took neither: too big, say
static int any_copy(char *path)
{
	u32 len = (u32)strlen(path);
	int there = maybe_there(path);
	memcpy(path + len, ".bak", 5);  // cheatstore_path leaves room for it
	there |= maybe_there(path);
	path[len] = 0;
	return there;
}

// 0 if the file names the dump it was made for (cheatfile_rom_crc) and the
// CRC cache (or crc.txt, before the cache is loaded) knows the ROM at
// rom_path as another: its codes would write another build's addresses. A
// ROM whose CRC isn't known yet passes. The cheat table is crc.txt's line
// buffer: cheatfile_parse fills it afresh after.
static int for_this_dump(const char *rom_path, const char *text, u32 len)
{
	u32 want, crc;
	struct cheatstore *cs = cheatstore();
	_Static_assert(sizeof(cs->cheats) >= SAFEFILE_LINE_MAX, "crc.txt's line fits the cheat table");
	if (!cheatfile_rom_crc(text, len, &want) ||
	    !names_crc_now(rom_path, &crc, (char *)cs->cheats, sizeof(cs->cheats)) || crc == want)
		return 1;
	tlog("cheats: %s is for crc32 %08lX, and %s is %08lX: left out", cheatstore()->path, (unsigned long)want,
	     rom_path, (unsigned long)crc);
	return 0;
}

int cheatstore_open(const char *rom_path)
{
	struct cheatstore *cs = cheatstore();
	cs->file.count = 0;
	cs->work.next = 0;
	cs->room_failed = 0;
	if (cheatstore_path(cs->path, sizeof(cs->path), rom_path) < 0) {
		tlog("cheats: %s: its cheat file's path is too long", rom_path);
		return CHEATSTORE_NONE;
	}
	int from = safefile_open(cs->path, CHEATSTORE_FILE_MAX);
	if (safefile_too_big()) {
		// A copy over the largest read: neither read nor ever written over
		// (no new file made in its place), though the other copy may hold up
		if (from != SAFEFILE_NONE)
			safefile_close();
		tlog("cheats: %s has a copy over %lu bytes: too big, left alone", cs->path,
		     (unsigned long)CHEATSTORE_FILE_MAX);
		return CHEATSTORE_BIG;
	}
	if (from == SAFEFILE_NONE) {
		if (safefile_failed()) {
			// A card error, not an absent file: never made anew over it
			tlog_error("cheats: %s can't be read", cs->path);
			return CHEATSTORE_UNREADABLE;
		}
		if (any_copy(cs->path)) {
			tlog("cheats: %s is there, but neither copy holds up: left alone", cs->path);
			return CHEATSTORE_UNREADABLE;
		}
		// None yet: an empty file, made on the first write
		cheatfile_parse(&cs->file, CHEATS_TEXT, 0, cs->cheats, CHEATFILE_MAX);
		cheatfile_begin(&cs->work);
		return CHEATSTORE_NEW;
	}
	u32 len;
	int read = load_text(&len);
	int failed = safefile_failed();
	safefile_close();
	if (!read || failed) {
		tlog_error("cheats: %s can't be read", cs->path);
		return CHEATSTORE_UNREADABLE;
	}
	if (len > CHEATFILE_TEXT_MAX) {
		// Its cheats would be read from a cut text, and the picks written
		// back over the rest: none of it, then
		tlog("cheats: %s is %lu bytes, over %lu: too big, left alone", cs->path, (unsigned long)len,
		     (unsigned long)CHEATFILE_TEXT_MAX);
		return CHEATSTORE_BIG;
	}
	if (cs->room_failed) {
		tlog_error("cheats: %s: its text didn't go into PSRAM whole", cs->path);
		return CHEATSTORE_UNREADABLE;
	}
	if (!for_this_dump(rom_path, CHEATS_TEXT, len))
		return CHEATSTORE_OTHER_DUMP;
	cheatfile_parse(&cs->file, CHEATS_TEXT, len, cs->cheats, CHEATFILE_MAX);
	cheatfile_begin(&cs->work);
	if (cs->file.dropped)
		tlog("cheats: %s: %lu cheats numbered past %u are left out", cs->path, (unsigned long)cs->file.dropped,
		     CHEATFILE_MAX);
	tlog_debug("cheats: %s: %lu bytes, %lu cheats", cs->path, (unsigned long)len, (unsigned long)cs->file.count);
	return CHEATSTORE_READ;
}

int cheatstore_read(const char *rom_path)
{
	return cheatstore_open(rom_path) == CHEATSTORE_READ && cheatstore()->file.count != 0;
}

void cheatstore_decoder_before(u32 i, struct cheat_decoder *out)
{
	struct cheatstore *cs = cheatstore();
	if (cs->work.next != i || i == 0) {
		cheatfile_begin(&cs->work);
		while (cs->work.next < i && cheatfile_next(&cs->file, &cs->work) >= 0)
			;
	}
	*out = cs->work.dec;
}

int cheatstore_decode(u32 budget)
{
	struct cheatstore *cs = cheatstore();
	u32 start = cycles_now();
	while (cheatfile_next(&cs->file, &cs->work) >= 0)
		if (budget && cycles_since(start) > budget)
			return cs->work.next >= cs->file.count;
	return 1;
}

// The last file counted whole, for the game popup: its path's CRC, and the
// size and date of the copy on the card (cheatstore_peek's), and the count.
// Picks don't change what is listed; anything else that changes the file
// changes its size or date (a write here records them again).
static struct {
	u32 path_crc, size;
	u16 date, time;
	u16 listed;
	u8 valid;
} listed_cache EWRAM_BSS;
_Static_assert(CHEATFILE_MAX <= 0xFFFF, "a count fits listed_cache");

// The copy of the file at path (cheatstore()->path) whose size and date
// key the count: NAME, else NAME.bak. 0 if neither is there.
static int stat_copy(char *path, FILINFO *info)
{
	if (f_stat(path, info) == FR_OK)
		return 1;
	u32 len = (u32)strlen(path);
	memcpy(path + len, ".bak", 5);  // cheatstore_path leaves room for it
	int there = f_stat(path, info) == FR_OK;
	path[len] = 0;
	return there;
}

s32 cheatstore_listed(void)
{
	struct cheatstore *cs = cheatstore();
	if (cs->work.next < cs->file.count)
		return -1;
	u32 listed = 0;
	for (u32 i = 0; i < cs->file.count; i++)
		listed += (u32)cheatfile_shown(&cs->cheats[i]);
	FILINFO info;
	listed_cache.valid = 0;
	if (stat_copy(cs->path, &info)) {
		listed_cache.path_crc = crc32(cs->path, (u32)strlen(cs->path));
		listed_cache.size = (u32)info.fsize;
		listed_cache.date = info.fdate;
		listed_cache.time = info.ftime;
		listed_cache.listed = (u16)listed;
		listed_cache.valid = 1;
	}
	return (s32)listed;
}

// --- The counts kept on the card -------------------------------------------------
//
// COUNTS_PATH, a safe file of a line a cheat file the cheat screen has
// counted, the latest first: "PATHCRC SIZE DATETIME LISTED" in hex, as
// listed_cache holds them, so the popup has the count after a power-off too.
// A file changed since (by tools/cheats.py, say) has another size or date:
// its line is passed over until the screen counts it again.

#define COUNTS_PATH CHEATS_DIR "/counts.txt"
#define COUNTS_MAX 32       // lines kept
#define COUNTS_BYTES 1536   // the file's text at most (COUNTS_MAX lines of 29 bytes and more)
_Static_assert(COUNTS_BYTES <= CHEATSTORE_DETAIL, "the counts' text is built in the detail view's buffer");

struct count_line {
	u32 path_crc, size, stamp, listed;
};

static u32 hex_field(const char **s)
{
	u32 v = 0, digits = 0;
	while (**s == ' ')
		(*s)++;
	for (;; (*s)++, digits++) {
		char c = **s;
		u32 d = c >= '0' && c <= '9' ? (u32)(c - '0') : c >= 'A' && c <= 'F' ? (u32)(c - 'A' + 10) : 16;
		if (d == 16 || digits == 8)
			break;
		v = v << 4 | d;
	}
	return digits ? v : 0xFFFFFFFFu;
}

// A line of COUNTS_PATH into *out: 0 if it isn't one
static int count_parse(const char *line, struct count_line *out)
{
	const char *s = line;
	out->path_crc = hex_field(&s);
	out->size = hex_field(&s);
	out->stamp = hex_field(&s);
	out->listed = hex_field(&s);
	return *s == 0 && out->listed <= CHEATFILE_MAX;
}

static u32 counts_len;

static u32 counts_fill(void *ctx, u32 pos, char *buf, u32 max)
{
	u32 n = pos < counts_len ? counts_len - pos : 0;
	n = n < max ? n : max;
	memcpy(buf, (const char *)ctx + pos, n);
	return n;
}

void cheatstore_keep_listed(void)
{
	struct cheatstore *cs = cheatstore();
	if (!listed_cache.valid)
		return;
	struct count_line now = {listed_cache.path_crc, listed_cache.size,
				 (u32)listed_cache.date << 16 | listed_cache.time, listed_cache.listed};
	char *out = cs->detail, *line = (char *)cs->part;
	u32 n = (u32)snprintf(out, COUNTS_BYTES, "%08lX %lX %08lX %lX\n", (unsigned long)now.path_crc,
			      (unsigned long)now.size, (unsigned long)now.stamp, (unsigned long)now.listed);
	u32 lines = 1;
	if (safefile_open(COUNTS_PATH, COUNTS_BYTES) != SAFEFILE_NONE) {
		int len;
		struct count_line was;
		while ((len = safefile_line(line, CHEATSTORE_PART)) != SAFEFILE_END) {
			if (len < 0 || !count_parse(line, &was))
				continue;
			if (was.path_crc == now.path_crc) {
				if (lines == 1 && !memcmp(&was, &now, sizeof(was))) {
					safefile_close();
					return;  // kept already, the latest
				}
				continue;
			}
			if (lines < COUNTS_MAX && n + (u32)len + 1 < COUNTS_BYTES) {
				memcpy(out + n, line, (u32)len);
				out[n + (u32)len] = '\n';
				n += (u32)len + 1;
				lines++;
			}
		}
		int failed = safefile_failed();
		safefile_close();
		if (failed)
			return;  // never written over what couldn't be read
	} else if (safefile_failed()) {
		return;
	}
	counts_len = n;
	if (!safefile_write(COUNTS_PATH, COUNTS_BYTES, counts_fill, out))
		tlog("cheats: %s not written", COUNTS_PATH);
}

// The count COUNTS_PATH keeps for the file at path (its copy's info): -1 if none
static s32 kept_count(const char *path, const FILINFO *info)
{
	struct count_line want = {crc32(path, (u32)strlen(path)), (u32)info->fsize,
				  (u32)info->fdate << 16 | info->ftime, 0},
			  was;
	char *line = (char *)cheatstore()->part;
	s32 listed = -1;
	int len;
	if (safefile_open(COUNTS_PATH, COUNTS_BYTES) == SAFEFILE_NONE)
		return -1;
	while ((len = safefile_line(line, CHEATSTORE_PART)) != SAFEFILE_END)
		if (len >= 0 && count_parse(line, &was) && was.path_crc == want.path_crc) {
			if (was.size == want.size && was.stamp == want.stamp)
				listed = (s32)was.listed;
			break;  // the latest line for the path counts
		}
	safefile_close();
	return listed;
}

int cheatstore_peek(const char *rom_path, s32 *listed)
{
	char *path = cheatstore()->path;
	*listed = -1;
	if (cheatstore_path(path, SAFEFILE_PATH_MAX, rom_path) < 0)
		return 0;
	FILINFO info;
	if (!stat_copy(path, &info))
		return 1;
	if (listed_cache.valid && listed_cache.size == (u32)info.fsize && listed_cache.date == info.fdate &&
	    listed_cache.time == info.ftime && listed_cache.path_crc == crc32(path, (u32)strlen(path)))
		*listed = listed_cache.listed;
	else
		*listed = kept_count(path, &info);
	return 1;
}

// The read file, for an edit: its text is the room's
static struct cheatfile *edited_file(void)
{
	struct cheatfile *f = &cheatstore()->file;
	TLOG_ASSERT(f->text == CHEATS_TEXT);
	psram_begin();
	return f;
}

// An edit's result: a failed PSRAM write (now or before) fails it
static u32 edit_done(u32 r)
{
	if (!cheatstore()->room_failed)
		return r;
	tlog_error("cheats: %s: a PSRAM write failed, the edit can't be trusted", cheatstore()->path);
	return CHEATFILE_EDIT_FAILED;
}

u32 cheatstore_add(const char *desc, const char *code, u8 format, u32 *index)
{
	return edit_done(cheatfile_add(edited_file(), &psram_room, desc, code, format, index));
}

u32 cheatstore_set_code(u32 i, const char *desc, const char *code, u8 format)
{
	return edit_done(cheatfile_set_code(edited_file(), &psram_room, i, desc, code, format));
}

u32 cheatstore_remove(u32 i)
{
	return edit_done(cheatfile_remove(edited_file(), &psram_room, i));
}

u32 cheatstore_can_add(const char *desc)
{
	struct cheatstore *cs = cheatstore();
	return cs->room_failed ? CHEATFILE_EDIT_FAILED : cheatfile_can_add(&cs->file, psram_room.cap, desc);
}

int cheatstore_write(void)
{
	struct cheatstore *cs = cheatstore();
	if (!cheatfile_changed(&cs->file))
		return 1;
	if (cs->file.cut)
		return 0;  // never a text cut short (cheatstore_read refuses such a file)
	if (cs->room_failed) {
		tlog_error("cheats: %s: a PSRAM write of its text failed: not written", cs->path);
		return 0;
	}
	u32 len = cheatfile_text_len(&cs->file);
	if (len > CHEATFILE_TEXT_MAX) {
		tlog_error("cheats: %s would be %lu bytes, over %u: not written", cs->path, (unsigned long)len,
			   CHEATFILE_TEXT_MAX);
		return 0;
	}
	int ok = safefile_write_cut(cs->path, CHEATSTORE_FILE_MAX, cheatfile_text, &cs->file);
	tlog("cheats: %s: %lu picked, %s", cs->path, (unsigned long)cheatfile_picks(&cs->file),
	     ok ? "written" : "not written");
	if (ok) {
		cheatfile_written(&cs->file);
		cheatstore_listed();  // the count kept for the file as it is on the card now
	}
	return ok;
}

int cheatstore_delete(const char *rom_path)
{
	char *path = cheatstore()->path;
	int len = cheatstore_path(path, SAFEFILE_PATH_MAX, rom_path);
	if (len < 0)
		return 1;
	FRESULT a = f_unlink(path);
	if (a == FR_OK)
		cutpoint("cheats: the cheat file deleted");
	memcpy(path + len, ".bak", 5);  // cheatstore_path leaves room for it
	FRESULT b = f_unlink(path);
	if (b == FR_OK)
		cutpoint("cheats: its .bak deleted");
	int gone = (a == FR_OK || a == FR_NO_FILE || a == FR_NO_PATH) &&
		   (b == FR_OK || b == FR_NO_FILE || b == FR_NO_PATH);
	if (a == FR_OK || b == FR_OK || !gone)
		tlog("cheats: %s: deleted (FatFs %u, %u)", path, (unsigned)a, (unsigned)b);
	return gone;
}

int cheatstore_drop(void)
{
	struct cheatstore *cs = cheatstore();
	char *path = cs->path;
	u32 len = (u32)strlen(path);
	// The copy a reader takes goes last, so a cut between the two leaves
	// the file as last written, or none
	int bak_last = safefile_reads_bak(path, CHEATSTORE_FILE_MAX);
	FRESULT a = FR_OK, b = FR_OK;
	if (bak_last) {
		a = f_unlink(path);
		if (a == FR_OK)
			cutpoint("cheats: the emptied file deleted");
	}
	memcpy(path + len, ".bak", 5);  // cheatstore_path leaves room for it
	b = f_unlink(path);
	if (b == FR_OK)
		cutpoint("cheats: the emptied file's .bak deleted");
	path[len] = 0;
	if (!bak_last) {
		a = f_unlink(path);
		if (a == FR_OK)
			cutpoint("cheats: the emptied file deleted");
	}
	int gone = (a == FR_OK || a == FR_NO_FILE || a == FR_NO_PATH) &&
		   (b == FR_OK || b == FR_NO_FILE || b == FR_NO_PATH);
	tlog("cheats: %s: no cheats left, deleted (FatFs %u, %u)", path, (unsigned)a, (unsigned)b);
	if (gone) {
		cheatfile_written(&cs->file);  // nothing to write: no file is what it says
		listed_cache.valid = 0;
	}
	return gone;
}

// The halfword at offset in the game's PSRAM image becomes (old & ~mask) |
// (value & mask); PSRAM shows 8 MB a page
static void rom_patch(const struct cheat_patch *p)
{
	vu16 *at = (vu16 *)((u8 *)PSRAMBase_S98 + (p->offset & 0x7FFFFF));
	SetPSRampage((u16)((p->offset >> 23) * 0x1000));
	*at = (u16)((*at & ~p->mask) | (p->value & p->mask));
	SetPSRampage(0);
}

static const char *const status_names[] = {"new",        "ok",          "master",        "too big",
					   "unreadable", "placeholder", "can't run here"};

// The first pass's ROM patches, for the second: the game's load takes PSRAM,
// where the text is, and all of pReadCache. With the names of the first
// cheats they come from, for the second pass's log.
#define KEPT_NAMES 8
#define KEPT_NAME_LEN 24
#define KEPT_NO_NAME 0xFF
static struct {
	u32 rom;  // the ROM path's CRC32: the second pass is the same game's
	u32 n;
	struct cheat_patch patches[CHEATSTORE_ROM_PATCHES];
	u16 number[CHEATSTORE_ROM_PATCHES];  // the cheat's N
	u8 name[CHEATSTORE_ROM_PATCHES];     // its name in names, or KEPT_NO_NAME
	u32 names_used;
	char names[KEPT_NAMES][KEPT_NAME_LEN];
} kept EWRAM_BSS;

static u32 path_crc(const char *path)
{
	return crc32(path, (u32)strlen(path));
}

// Keeps cheat c's ROM patches (in w) for the second pass: all of them (the
// caller has checked they fit)
static void keep_patches(const struct cheatfile *f, const struct cheatfile_cheat *c, const struct cheatfile_work *w)
{
	u8 name = KEPT_NO_NAME;
	TLOG_ASSERT(kept.n + w->n_patches <= CHEATSTORE_ROM_PATCHES);
	if (w->n_patches && kept.names_used < KEPT_NAMES) {
		name = (u8)kept.names_used++;
		snprintf(kept.names[name], KEPT_NAME_LEN, "%.*s", c->desc != CHEATFILE_NONE ? (int)c->desc_len : 0,
			 c->desc != CHEATFILE_NONE ? f->text + c->desc : "");
	}
	for (u32 p = 0; p < w->n_patches && kept.n < CHEATSTORE_ROM_PATCHES; p++) {
		kept.patches[kept.n] = w->patches[p];
		kept.number[kept.n] = c->number;
		kept.name[kept.n] = name;
		kept.n++;
	}
}

// The second pass: the kept ROM patches into the game in PSRAM (psram_bytes
// of it), but for any past its end or over the kernel's own patches
static void write_patches(const char *rom_path, u32 psram_bytes)
{
	u32 applied = 0, skipped = 0, guarded = 0;
	if (kept.rom != path_crc(rom_path)) {
		tlog_error("cheats: %s: its ROM patches weren't kept before the load: none written", rom_path);
		return;
	}
	for (u32 p = 0; p < kept.n; p++) {
		const struct cheat_patch *pt = &kept.patches[p];
		if (pt->offset + 2 > psram_bytes) {
			skipped++;
			continue;
		}
		// Not over the kernel's own patches: the patch area (the hook, the
		// interpreter, this table), the entry branch, the hook's words
		if (rom_guard_hits(pt->offset, 2)) {
			if (!guarded++ || tlog_wants(TLOG_DEBUG))
				tlog("cheats: cheat %u (%s): its ROM patch at %08lX is over the kernel's own, left out",
				     kept.number[p], kept.name[p] != KEPT_NO_NAME ? kept.names[kept.name[p]] : "?",
				     (unsigned long)0x08000000 + pt->offset);
			continue;
		}
		rom_patch(pt);
		applied++;
	}
	tlog("cheats: %lu ROM patches written into the game%s%s", (unsigned long)applied,
	     skipped ? ", some past its end left out" : "",
	     guarded ? ", some over the kernel's own patches left out" : "");
}

void cheatstore_launch(const char *rom_path, u32 psram_bytes)
{
	if (psram_bytes) {
		write_patches(rom_path, psram_bytes);
		return;
	}
	u32 cost = CHEAT_COST_CALL;
	gl_cheat_count = 0;
	gl_cheat_bytes = 0;
	gl_cheat_patches = 0;
	kept.rom = path_crc(rom_path);
	kept.n = kept.names_used = 0;
	if (!cheatstore_read(rom_path))
		return;
	struct cheatstore *cs = cheatstore();
	struct cheatfile *f = &cs->file;
	u32 last = 0, picks = 0;
	for (u32 i = 0; i < f->count; i++)
		if (cs->cheats[i].picked) {
			last = i + 1;
			picks++;
		}
	if (!picks)
		return;
	// Every cheat up to the last pick, as keys carry from cheat to cheat
	s32 i;
	while ((i = cheatfile_next(f, &cs->work)) >= 0 && (u32)i < last) {
		const struct cheatfile_cheat *c = &cs->cheats[i];
		if (!c->picked)
			continue;
		if (c->status != CHEATFILE_OK) {
			tlog("cheats: cheat %u (%.*s): %s, left out", c->number, c->desc_len, f->text + c->desc,
			     status_names[c->status]);
			continue;
		}
		u32 bytes = cs->work.n_prog * 4;
		if (gl_cheat_bytes + bytes > CHEATFILE_BYTES_MAX || cost + cs->work.cost > CHEAT_CYCLES_MAX) {
			tlog("cheats: cheat %u (%.*s): its %lu bytes and %lu cycles don't fit beside %lu and %lu, left "
			     "out",
			     c->number, c->desc_len, f->text + c->desc, (unsigned long)bytes,
			     (unsigned long)cs->work.cost, (unsigned long)gl_cheat_bytes, (unsigned long)cost);
			continue;
		}
		// Its ROM patches all kept, or the cheat left out whole: never its
		// ops running without the patches they go with
		if (kept.n + cs->work.n_patches > CHEATSTORE_ROM_PATCHES) {
			tlog("cheats: cheat %u (%.*s): its %lu ROM patches don't fit beside %lu, left out", c->number,
			     c->desc_len, f->text + c->desc, (unsigned long)cs->work.n_patches, (unsigned long)kept.n);
			continue;
		}
		if (bytes) {
			memcpy((u8 *)gl_cheat_table + gl_cheat_bytes, cs->work.prog, bytes);
			gl_cheat_bytes += bytes;
			cost += cs->work.cost;
			gl_cheat_count++;
		}
		gl_cheat_patches += cs->work.n_patches;
		keep_patches(f, c, &cs->work);
	}
	if (cs->work.next >= f->count)
		cheatstore_listed();  // decoded whole: the game popup's count, should the launch come back
	if (gl_cheat_bytes) {
		gl_cheat_table[gl_cheat_bytes / 4] = 0;  // the table's end
		gl_cheat_bytes += 4;
	}
	tlog("cheats: %s: %lu picked, %lu run in game (%lu bytes, %lu cycles a frame at most), %lu ROM "
	     "patches",
	     cs->path, (unsigned long)picks, (unsigned long)gl_cheat_count, (unsigned long)gl_cheat_bytes,
	     (unsigned long)(gl_cheat_count ? cost : 0), (unsigned long)gl_cheat_patches);
}
