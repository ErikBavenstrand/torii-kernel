// The user's lists, the hardware-free half: parsing, edits and the "played
// ago" text (userdata.h).
#include "userdata.h"
#include "lang.h"

#include <stdio.h>
#include <string.h>

static int blank(char c)
{
	return c == ' ' || c == '\t' || c == '\r' || c == '\n';
}

// s without its leading and trailing blanks (cut in place)
static char *trim(char *s)
{
	while (*s == ' ' || *s == '\t')
		s++;
	char *e = s + strlen(s);
	while (e > s && blank(e[-1]))
		*--e = 0;
	return s;
}

// Reads a decimal number of at most 10 digits that fits a u32 at *s,
// moving *s past it; 0 if there is none or it is too big.
static int number(char **s, u32 *out)
{
	char *p = *s;
	unsigned long long v = 0;
	u32 digits = 0;
	while (*p >= '0' && *p <= '9') {
		v = v * 10 + (u32)(*p++ - '0');
		if (++digits > 10)
			return 0;
	}
	if (!digits || v > 0xFFFFFFFFull)
		return 0;
	*out = (u32)v;
	*s = p;
	return 1;
}

const char *userdata_nor_name(const char *path)
{
	return strncmp(path, USERDATA_NOR, sizeof(USERDATA_NOR) - 1) == 0 ? path + sizeof(USERDATA_NOR) - 1 : NULL;
}

int userdata_path_ok(const char *path)
{
	for (const char *p = path; *p; p++)
		if ((u8)*p < 0x20 || *p == 0x7F)
			return 0;
	const char *nor = userdata_nor_name(path);
	if (nor) {
		u32 n = (u32)strlen(nor);
		return n >= 1 && n <= USERDATA_NOR_NAME_MAX;
	}
	if (path[0] != '/')
		return 0;
	const char *slash = strrchr(path, '/');
	u32 name = (u32)strlen(slash + 1);
	return (u32)(slash - path) < USERDATA_DIR_MAX && name >= 1 && name <= USERDATA_NAME_MAX;
}

void userdata_canonical(char *path)
{
	if (path[0] != '/')
		return;
	const char *in = path;
	u32 len = 0;  // what's written back: "/part" for each part kept
	for (;;) {
		while (*in == '/')
			in++;
		const char *part = in;
		while (*in && *in != '/')
			in++;
		u32 n = (u32)(in - part);
		if (!n)
			break;
		if (n == 1 && part[0] == '.')
			continue;
		if (n == 2 && part[0] == '.' && part[1] == '.') {
			while (len > 0 && path[len - 1] != '/')
				len--;
			if (len > 0)
				len--;
			continue;
		}
		path[len++] = '/';
		memmove(&path[len], part, n);  // behind the reading point
		len += n;
	}
	if (!len)
		path[len++] = '/';
	path[len] = 0;
}

int userdata_join(char *out, const char *dir, const char *name)
{
	const char *sep = strcmp(dir, "/") == 0 ? "" : "/";
	u32 n = (u32)snprintf(out, USERDATA_PATH_MAX, "%s%s%s", dir, sep, name);
	if (n < USERDATA_PATH_MAX)
		return 1;
	out[0] = 0;
	return 0;
}

int userdata_item_ok(const char *item)
{
	u32 n = 0;
	for (; item[n]; n++) {
		char c = item[n];
		if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' ||
		      c == '_'))
			return 0;
	}
	return n >= 1 && n < USERDATA_ITEM_MAX;
}

// "@boot <item>", from after the '@'; an older kernel's row after it is
// read and dropped
static int parse_boot(char *s, struct userdata_boot *boot)
{
	if (strncmp(s, "boot", 4) != 0 || (s[4] != ' ' && s[4] != '\t'))
		return USERDATA_BAD;
	s = trim(s + 4);
	char *item = s;
	while (*s && *s != ' ' && *s != '\t')
		s++;
	if (*s) {
		*s++ = 0;
		s = trim(s);
		u32 row;
		if (*s && (!number(&s, &row) || *s))
			return USERDATA_BAD;
	}
	if (!userdata_item_ok(item))
		return USERDATA_BAD;
	snprintf(boot->item, sizeof(boot->item), "%s", item);
	return USERDATA_BOOT;
}

int userdata_parse_recent(char *line, u32 *time, char **path, struct userdata_boot *boot)
{
	char *s = trim(line);
	if (!*s || *s == '#')
		return USERDATA_SKIP;
	if (*s == '@')
		return parse_boot(s + 1, boot);
	u32 t = 0;
	if (*s >= '0' && *s <= '9') {  // a time, then blanks, then the path
		if (!number(&s, &t) || (*s != ' ' && *s != '\t'))
			return USERDATA_BAD;
		s = trim(s);
	}
	if (!userdata_path_ok(s))
		return USERDATA_BAD;
	*time = t;
	*path = s;
	return USERDATA_GAME;
}

int userdata_parse_fav(char *line, char **path)
{
	char *s = trim(line);
	if (!*s || *s == '#')
		return USERDATA_SKIP;
	if (!userdata_path_ok(s))
		return USERDATA_BAD;
	*path = s;
	return USERDATA_GAME;
}

u32 userdata_recent_line(const struct userdata_recent *list, u32 count, const struct userdata_boot *boot, u32 n,
			 char *buf, u32 size)
{
	u32 first = boot->item[0] ? 1 : 0;
	int len;
	if (n < first)
		len = snprintf(buf, size, "@boot %s\n", boot->item);
	else if (n - first < count)
		len = snprintf(buf, size, "%lu\t%s\n", (unsigned long)list[n - first].time, list[n - first].path);
	else
		return 0;
	return len < 0 ? 0 : (u32)len < size ? (u32)len : size - 1;
}

u32 userdata_recent_put(struct userdata_recent *list, u32 count, const char *path, u32 time)
{
	u32 at = count;
	for (u32 i = 0; i < count; i++) {
		if (strcmp(list[i].path, path) == 0) {
			at = i;
			break;
		}
	}
	if (at == count) {  // new: a free row, or the oldest one
		if (count < USERDATA_RECENT_MAX)
			count++;
		at = count - 1;
	}
	memmove(&list[1], &list[0], at * sizeof(list[0]));
	list[0].time = time;
	snprintf(list[0].path, sizeof(list[0].path), "%s", path);
	return count;
}

// Drops the games drop(path, arg) says yes to
static u32 drop_if(struct userdata_recent *list, u32 count, int (*drop)(const char *path, const char *arg),
		   const char *arg)
{
	u32 kept = 0;
	for (u32 i = 0; i < count; i++) {
		if (drop(list[i].path, arg))
			continue;
		if (kept != i)
			list[kept] = list[i];
		kept++;
	}
	return kept;
}

static int same(const char *path, const char *arg)
{
	return strcmp(path, arg) == 0;
}

static int nor_named(const char *path, const char *name)
{
	const char *nor = userdata_nor_name(path);
	return nor && (!name || strcmp(nor, name) == 0);
}

u32 userdata_recent_drop(struct userdata_recent *list, u32 count, const char *path)
{
	return drop_if(list, count, same, path);
}

u32 userdata_recent_drop_nor(struct userdata_recent *list, u32 count, const char *name)
{
	return drop_if(list, count, nor_named, name);
}

u32 userdata_recent_rename(struct userdata_recent *list, u32 count, const char *from, const char *to)
{
	if (strcmp(from, to) == 0)
		return count;
	u32 i = 0;
	while (i < count && strcmp(list[i].path, from) != 0)
		i++;
	if (i == count)
		return count;
	list[i].path[0] = 0;  // out of the way of the drop
	count = drop_if(list, count, same, to);
	for (i = 0; i < count; i++) {
		if (!list[i].path[0]) {
			snprintf(list[i].path, sizeof(list[i].path), "%s", to);
			break;
		}
	}
	return count;
}

u32 userdata_find(const u32 *list, u32 count, const char *path, userdata_str str, int *found)
{
	u32 lo = 0, hi = count;
	while (lo < hi) {
		u32 mid = lo + (hi - lo) / 2;
		if (strcmp(str(list[mid]), path) < 0)
			lo = mid + 1;
		else
			hi = mid;
	}
	*found = lo < count && strcmp(str(list[lo]), path) == 0;
	return lo;
}

int userdata_in_order(const u32 *list, u32 count, userdata_str str)
{
	for (u32 i = 1; i < count; i++)
		if (strcmp(str(list[i - 1]), str(list[i])) >= 0)
			return 0;
	return 1;
}

static void sift(u32 *a, u32 root, u32 end, userdata_str str)
{
	for (;;) {
		u32 child = 2 * root + 1;
		if (child >= end)
			return;
		if (child + 1 < end && strcmp(str(a[child]), str(a[child + 1])) < 0)
			child++;
		if (strcmp(str(a[root]), str(a[child])) >= 0)
			return;
		u32 t = a[root];
		a[root] = a[child];
		a[child] = t;
		root = child;
	}
}

u32 userdata_sort(u32 *list, u32 count, userdata_str str)
{
	for (u32 start = count / 2; start-- > 0;)
		sift(list, start, count, str);
	for (u32 end = count; end > 1; end--) {
		u32 t = list[0];
		list[0] = list[end - 1];
		list[end - 1] = t;
		sift(list, 0, end - 1, str);
	}
	u32 kept = 0;
	for (u32 i = 0; i < count; i++)
		if (!kept || strcmp(str(list[kept - 1]), str(list[i])) != 0)
			list[kept++] = list[i];
	return kept;
}

static int leap(u32 year)
{
	return (year % 4 == 0 && year % 100 != 0) || year % 400 == 0;
}

u32 userdata_unix_time(u32 year, u32 month, u32 day, u32 hour, u32 minute, u32 second)
{
	static const u16 before[12] = {0, 31, 59, 90, 120, 151, 181, 212, 243, 273, 304, 334};
	static const u8 length[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
	if (year < 2000 || year > 2099 || month < 1 || month > 12 || day < 1 || hour > 23 || minute > 59 || second > 59)
		return 0;
	if (day > (u32)length[month - 1] + (month == 2 && leap(year)))
		return 0;
	u32 days = (year - 1970) * 365 + (year - 1969) / 4;  // leap days 1972-2096, every 4 years
	days += (u32)before[month - 1] + (month > 2 && leap(year)) + day - 1;
	return days * 86400 + hour * 3600 + minute * 60 + second;
}

u32 userdata_clock_time(u32 t)
{
	return t < USERDATA_CLOCK_FROM ? 0 : t;
}

struct userdata_ago_words userdata_ago_words_now(void)
{
	return (struct userdata_ago_words){S(STR_AGO_NOW),       S(STR_AGO_MINUTES), S(STR_AGO_HOURS),
					   S(STR_AGO_YESTERDAY), S(STR_AGO_DAYS),    S(STR_AGO_WEEK),
					   S(STR_AGO_WEEKS),     S(STR_AGO_MONTH),   S(STR_AGO_MONTHS),
					   S(STR_AGO_YEAR),      S(STR_AGO_YEARS)};
}

void userdata_ago(char *out, u32 size, u32 then, u32 now, const struct userdata_ago_words *w)
{
	const u32 hour = 3600, day = 24 * hour;
	if (!size)
		return;
	out[0] = 0;
	if (!then || !now || then > now)
		return;
	u32 d = now - then;
	const char *whole = NULL, *after = NULL;
	u32 n = 0;
	if (d < 60)
		whole = w->now;
	else if (d < hour)
		n = d / 60, after = w->minutes;
	else if (d < day)
		n = d / hour, after = w->hours;
	else if (d < 2 * day)
		whole = w->yesterday;
	else if (d < 7 * day)
		n = d / day, after = w->days;
	else if (d < 14 * day)
		whole = w->week;
	else if (d < 30 * day)
		n = d / (7 * day), after = w->weeks;
	else if (d < 60 * day)
		whole = w->month;
	else if (d < 365 * day)
		n = d / (30 * day), after = w->months;
	else if (d < 2 * 365 * day)
		whole = w->year;
	else
		n = d / (365 * day), after = w->years;
	if (whole)
		snprintf(out, size, "%s", whole);
	else
		lang_fill(out, size, after, LN(n));
}
