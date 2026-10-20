// The checks behind safe file writes (safefile_scan.h).
#include "safefile_scan.h"

#include <stdio.h>
#include <string.h>

#include "crc32.h"

void safefile_scan_begin(struct safefile_scan *s, u32 prefix_len)
{
	memset(s, 0, sizeof(*s));
	s->prefix_len = prefix_len;
}

static int blank(char c)
{
	return c == ' ' || c == '\t' || c == '\r';
}

// The line just read ends (at a newline or the end of the file)
static void end_line(struct safefile_scan *s)
{
	u32 crc, n = s->line_len;
	if (n > sizeof(s->line)) {
		s->crc_line = 0;  // longer than a CRC line can be: text
		return;
	}
	u32 i = 0;
	while (i < n && blank(s->line[i]))
		i++;
	if (i == n)
		return;  // blank lines after the CRC line don't count
	s->crc_line = (u8)safefile_parse_crc_line(s->line, n, &crc);
	if (s->crc_line) {
		s->body = s->line_start;
		s->body_crc = s->line_crc;
		s->stated = crc;
	}
}

void safefile_scan_feed(struct safefile_scan *s, const void *data, u32 bytes)
{
	const u8 *p = data;
	u32 seg = 0;  // bytes of p before seg are in s->crc
	for (u32 i = 0; i < bytes; i++) {
		u32 at = s->size + i;
		if (at == s->prefix_len || at == s->line_start) {
			s->crc = crc32_update(s->crc, p + seg, i - seg);
			seg = i;
			if (at == s->prefix_len)
				s->prefix_crc = s->crc;
			if (at == s->line_start)
				s->line_crc = s->crc;
		}
		u8 b = p[i];
		s->has_nul |= b == 0;
		if (b == '\n') {
			end_line(s);
			s->line_start = at + 1;
			s->line_len = 0;
		} else {
			if (s->line_len < sizeof(s->line))
				s->line[s->line_len] = (char)b;
			s->line_len++;
		}
	}
	s->crc = crc32_update(s->crc, p + seg, bytes - seg);
	s->size += bytes;
}

void safefile_scan_end(struct safefile_scan *s)
{
	if (s->line_len)
		end_line(s);  // a last line with no newline (an edited file)
	if (s->size == s->prefix_len)
		s->prefix_crc = s->crc;
	s->valid = s->crc_line && s->body_crc == s->stated;
}

int safefile_pick(const struct safefile_scan *main, const struct safefile_scan *bak)
{
	if (main && main->valid)
		return SAFEFILE_MAIN;
	int bak_ok = bak && bak->valid;
	if (main && main->size && !main->has_nul) {
		// FatFs writes a file's size only when it closes it, so a write cut
		// short leaves NAME empty; a NAME that is the start of NAME.bak is
		// treated the same, in case a cut leaves more
		int cut = bak_ok && bak->prefix_len == main->size && main->size <= bak->body &&
			  bak->prefix_crc == main->crc;
		if (!cut)
			return SAFEFILE_HAND;
	}
	return bak_ok ? SAFEFILE_BAK : SAFEFILE_NONE;
}

u32 safefile_crc_line(char *out, u32 crc)
{
	return (u32)snprintf(out, SAFEFILE_CRC_LINE_LEN + 2, SAFEFILE_CRC_TAG "%08lx\n", (unsigned long)crc);
}

static int hex_digit(char c)
{
	if (c >= '0' && c <= '9')
		return c - '0';
	if (c >= 'a' && c <= 'f')
		return c - 'a' + 10;
	if (c >= 'A' && c <= 'F')
		return c - 'A' + 10;
	return -1;
}

int safefile_parse_crc_line(const char *line, u32 len, u32 *crc)
{
	const u32 tag = sizeof(SAFEFILE_CRC_TAG) - 1;
	while (len && blank(line[len - 1]))
		len--;
	if (len != SAFEFILE_CRC_LINE_LEN || memcmp(line, SAFEFILE_CRC_TAG, tag) != 0)
		return 0;
	u32 v = 0;
	for (u32 i = tag; i < len; i++) {
		int d = hex_digit(line[i]);
		if (d < 0)
			return 0;
		v = v << 4 | (u32)d;
	}
	*crc = v;
	return 1;
}

u32 safefile_lines_fill(void *lines, u32 pos, char *out, u32 max)
{
	struct safefile_lines *l = lines;
	if (pos == 0) {
		l->n = 0;
		l->off = 0;
		l->pos = 0;
		l->len = l->line(l->ctx, 0, l->buf, sizeof(l->buf));
	}
	if (pos != l->pos)
		return 0;  // not where the last call ended: nothing to give
	u32 done = 0;
	while (done < max && l->len) {
		u32 n = l->len - l->off;
		if (n > max - done)
			n = max - done;
		memcpy(out + done, l->buf + l->off, n);
		done += n;
		l->off += n;
		if (l->off == l->len) {
			l->off = 0;
			l->len = l->line(l->ctx, ++l->n, l->buf, sizeof(l->buf));
		}
	}
	l->pos += done;
	return done;
}
