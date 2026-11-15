// See script.h. Hardware-free, so tests/host runs it.
#include "script.h"

#include <string.h>

static struct torii_script body_script EWRAM_BSS, heading_script EWRAM_BSS;
static struct torii_script body_extra EWRAM_BSS, heading_extra EWRAM_BSS;
u8 script_active EWRAM_BSS;
static u8 extra_ready EWRAM_BSS;
const struct torii_script *script_body EWRAM_BSS, *script_heading EWRAM_BSS;

static u32 rd16(const u8 *p)
{
	return p[0] | (u32)p[1] << 8;
}

static u32 rd32(const u8 *p)
{
	return rd16(p) | rd16(p + 2) << 16;
}

int script_header_ok(const u8 *header)
{
	if (memcmp(header, "TSCF", 4) != 0 || rd16(header + 4) != 1)
		return 0;
	u32 fonts = rd16(header + 6), bytes = rd32(header + 12);
	return (fonts == 1 || fonts == 2) && bytes >= SCRIPT_HEADER && bytes <= SCRIPT_MAX && !(bytes & 3);
}

u32 script_blob_crc(const u8 *header)
{
	return rd32(header + 8);
}

u32 script_blob_size(const u8 *header)
{
	return rd32(header + 12);
}

// Reads one font's record at blob + at into out; 0 if it is malformed. Its glyphs
// must sit inside a line of line_height rows once moved down by dy; trusted
// skips that walk over every glyph (the kernel's own blob, which
// tests/host's test_script_extra checks whole)
static int read_record(const u8 *blob, u32 bytes, u32 at, u32 line_height, s32 dy, int trusted,
		       struct torii_script *out)
{
	if (at & 3 || at < SCRIPT_HEADER || bytes - at < 12)
		return 0;
	const u8 *rec = blob + at;
	u32 ranges = rd16(rec), glyphs = rd16(rec + 2), table = rd32(rec + 4), bits = rd32(rec + 8);
	u32 room = bytes - at;
	if (!ranges || !glyphs || table & 3 || bits & 3 || table < 12 + 6 * ranges || table > room ||
	    glyphs > (room - table) / 6 || bits < table + 6 * glyphs || bits > room)
		return 0;
	u32 bits_bytes = room - bits;
	u32 last = 0xFF;  // the last code point of the range before
	u32 next_index = 0;
	for (u32 i = 0; i < ranges; i++) {
		const u8 *r = rec + 12 + 6 * i;
		u32 first = rd16(r), end = rd16(r + 2), base = rd16(r + 4);
		if (first <= last || end < first || base != next_index || end - first >= glyphs - base)
			return 0;
		last = end;
		next_index = base + (end - first + 1);
	}
	if (next_index > glyphs)
		return 0;
	// Every glyph's rows lie inside the bitmaps
	const u8 *g = rec + table;
	for (u32 i = 0; i < glyphs && !trusted; i++, g += 6) {
		u32 rows = g[4], width = g[5];
		if (width > 16 || rows > 16 || (u32)g[3] + rows + (u32)dy > line_height)
			return 0;
		if (rows && rd16(g) * 4u + rows * (width > 8 ? 2u : 1u) > bits_bytes)
			return 0;
	}
	out->glyphs = (const struct torii_glyph *)(const void *)(rec + table);
	out->bits = rec + bits;
	out->ranges = rec + 12;
	out->range_count = ranges;
	out->glyph_count = glyphs;
	out->dy = dy;
	return 1;
}

// Reads a blob's records for font_body and font_heading; 0 if it is malformed
static int read_blob(const u8 *blob, u32 bytes, int trusted, struct torii_script *body_out,
		     struct torii_script *heading_out)
{
	if (!script_header_ok(blob) || script_blob_size(blob) != bytes)
		return 0;
	struct torii_script body, heading;
	u32 fonts = rd16(blob + 6);
	if (!read_record(blob, bytes, rd32(blob + 16), font_body.line_height, 0, trusted, &body))
		return 0;
	u32 heading_at = rd32(blob + 20);
	if (fonts == 2 && heading_at) {
		if (!read_record(blob, bytes, heading_at, font_heading.line_height, 0, trusted, &heading))
			return 0;
	} else if (!read_record(blob, bytes, rd32(blob + 16), font_heading.line_height,
				(s32)font_heading.baseline - (s32)font_body.baseline, trusted, &heading)) {
		return 0;  // the body's glyphs moved down to the heading's baseline leave its line
	}
	*body_out = body;
	*heading_out = heading;
	return 1;
}

int script_use(const u8 *blob, u32 bytes)
{
	if (!read_blob(blob, bytes, 0, &body_script, &heading_script))
		return 0;
	script_active = 1;
	script_body = &body_script;
	script_heading = &heading_script;
	return 1;
}

void script_drop(void)
{
	memset(&body_script, 0, sizeof(body_script));
	memset(&heading_script, 0, sizeof(heading_script));
	script_active = 0;
	script_body = script_heading = NULL;  // script_find points them at the kernel's own
}

// The kernel's own glyphs, read once; a blob that doesn't read (a build
// broken past tests/host's test_script_extra) leaves them empty: boxes
static void extra_start(void)
{
	extra_ready = 1;
	if (!read_blob((const u8 *)font_body_extra, font_body_extra_bytes, 1, &body_extra, &heading_extra)) {
		memset(&body_extra, 0, sizeof(body_extra));
		memset(&heading_extra, 0, sizeof(heading_extra));
	}
}

const struct torii_script *script_find(const struct torii_font *font)
{
	if (font != &font_body && font != &font_heading)
		return NULL;
	if (!script_active) {
		if (!extra_ready)
			extra_start();
		script_body = &body_extra;
		script_heading = &heading_extra;
	}
	return font == &font_body ? script_body : script_heading;
}

int script_glyph(const struct torii_script *sc, u32 cp)
{
	if (cp > 0xFFFF)
		return -1;
	u32 lo = 0, hi = sc->range_count;
	while (lo < hi) {
		u32 mid = (lo + hi) / 2;
		const u8 *r = sc->ranges + 6 * mid;
		if (cp < rd16(r))
			hi = mid;
		else if (cp > rd16(r + 2))
			lo = mid + 1;
		else
			return (int)(TORII_FONT_SCRIPT + rd16(r + 4) + (cp - rd16(r)));
	}
	return -1;
}

const struct torii_glyph *torii_font_script_glyph(const struct torii_font *font, u32 g)
{
	const struct torii_script *sc = script_of(font);
	if (!sc || g - TORII_FONT_SCRIPT >= sc->glyph_count)
		return &font->glyphs[TORII_FONT_REPLACEMENT];
	return &sc->glyphs[g - TORII_FONT_SCRIPT];
}
