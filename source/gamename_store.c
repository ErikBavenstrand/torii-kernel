// The kernel's writes of /torii/names.txt (gamename.h, "The kernel's names
// file"): the file read through B5's safe file into PSRAM, changed by the
// hardware-free gamename_custom_set or _move into pReadCache, and written
// back as safefile.h's two copies.
#include <string.h>

#include "gamename.h"
#include "ez_define.h"
#include "safefile.h"
#include "tlog.h"
#include "engine/psram.h"

extern u8 pReadCache[];

// pReadCache's share (READCACHE_NAMES): a line on its way to PSRAM, the
// kept lines' offsets, then the new text (the line's staging comes first in
// it, while the file is read)
struct names_area {
	char line[1024];  // a line of names.txt: a path (511 bytes at most), a tab, a name
	u32 lines[GAMENAME_CUSTOM_MAX];
	char out[READCACHE_NAMES_SIZE - 1024 - GAMENAME_CUSTOM_MAX * 4];
};
_Static_assert(sizeof(struct names_area) <= READCACHE_NAMES_SIZE, "the names file's share of pReadCache");
_Static_assert(PSRAM_NAMES_TEXT_BYTES <= sizeof(((struct names_area *)0)->out),
	       "a names file read whole fits its writing");

#define STAGE_BYTES 1024  // the text staged before each PSRAM write, at the start of out
// What a new text may hold: what load reads back, less room for the CRC line
// safefile adds (so an edit never makes a file Rename can't read again)
#define TEXT_CAP (PSRAM_NAMES_TEXT_BYTES - SAFEFILE_CRC_LINE_LEN - 2)

static struct names_area *area(void)
{
	return (struct names_area *)(void *)(pReadCache + READCACHE_NAMES);
}

// Appends the staged bytes but an odd last one (psram_write takes
// halfwords) to the text in PSRAM; what stays staged
static u32 flush(u32 *len, u32 staged, int *ok)
{
	u32 even = staged & ~1u;
	if (*len + even > PSRAM_NAMES_TEXT_BYTES || !psram_write((u8 *)PSRAM_NAMES_TEXT_BASE + *len, area()->out, even))
		*ok = 0;
	*len += even;
	area()->out[0] = area()->out[even];
	return staged - even;
}

// Reads names.txt (the copy that holds) into PSRAM and indexes it into c;
// 0 if it can't be read whole (a card error, a line too long, too large)
static int load(struct gamename_custom *c)
{
	struct names_area *a = area();
	u32 len = 0, staged = 0;
	int ok = 1;
	psram_begin();
	if (safefile_open(GAMENAME_FILE, PSRAM_NAMES_TEXT_BYTES) == SAFEFILE_NONE) {
		if (safefile_failed()) {
			tlog_error("names: can't read %s", GAMENAME_FILE);
			return 0;
		}
		gamename_custom_parse(c, NULL, 0, a->lines, GAMENAME_CUSTOM_MAX);  // no file yet
		return 1;
	}
	for (;;) {
		int n = safefile_line(a->line, sizeof(a->line) - 1);
		if (n == SAFEFILE_END)
			break;
		if (n == SAFEFILE_LONG) {
			ok = 0;  // left out, so writing back would lose it
			continue;
		}
		a->line[n++] = '\n';
		if (staged + (u32)n > STAGE_BYTES)
			staged = flush(&len, staged, &ok);
		memcpy(a->out + staged, a->line, (u32)n);
		staged += (u32)n;
	}
	if (safefile_failed())
		ok = 0;
	safefile_close();
	u32 text = len + staged;
	a->out[staged++] = 0;  // an even end; the parser stops at text
	if (staged & 1)
		a->out[staged++] = 0;
	flush(&len, staged, &ok);
	if (!ok) {
		tlog_error("names: %s can't be read whole: left as it is", GAMENAME_FILE);
		return 0;
	}
	gamename_custom_parse(c, (const char *)PSRAM_NAMES_TEXT_BASE, text, a->lines, GAMENAME_CUSTOM_MAX);
	if (!c->complete)
		tlog_error("names: %s holds more than %u names: left as it is", GAMENAME_FILE, GAMENAME_CUSTOM_MAX);
	return c->complete;
}

struct fill_ctx {
	u32 len;
};

static u32 fill(void *ctx, u32 pos, char *buf, u32 max)
{
	const struct fill_ctx *f = ctx;
	u32 n = pos < f->len ? f->len - pos : 0;
	if (n > max)
		n = max;
	memcpy(buf, area()->out + pos, n);
	return n;
}

// Writes the len bytes of new text in out as names.txt
static int store(u32 len)
{
	if (GAMENAME_FAILED(len)) {
		tlog_error("names: %s isn't changed (%s)", GAMENAME_FILE,
			   len == GAMENAME_TOO_BIG     ? "too large"
			   : len == GAMENAME_BAD_PATH  ? "not a path it keeps"
			   : len == GAMENAME_UNCHANGED ? "nothing to move"
						       : "not read whole");
		return len == GAMENAME_UNCHANGED;
	}
	struct fill_ctx f = {len};
	if (!safefile_write(GAMENAME_FILE, PSRAM_NAMES_TEXT_BYTES, fill, &f)) {
		tlog_error("names: can't write %s", GAMENAME_FILE);
		return 0;
	}
	return 1;
}

int gamename_store_set(const char *path, const char *name)
{
	struct gamename_custom c;
	if (!load(&c))
		return 0;
	struct names_area *a = area();
	tlog("names: %s is %s%s%s", path, name ? "\"" : "", name ? name : "its own name again", name ? "\"" : "");
	return store(gamename_custom_set(&c, path, name, a->out, TEXT_CAP));
}

int gamename_store_move(const char *old_path, const char *new_path)
{
	struct gamename_custom c;
	if (!load(&c))
		return 0;
	u32 len;
	if (!gamename_custom_get(&c, old_path, &len))
		return 1;  // no name of its own: nothing to write
	struct names_area *a = area();
	tlog("names: %s's name moves to %s", old_path, new_path);
	return store(gamename_custom_move(&c, old_path, new_path, a->out, TEXT_CAP));
}
