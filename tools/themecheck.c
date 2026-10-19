// The theme checker behind `just theme-check` and `just theme-new`: the
// kernel's own theme.ini parser (source/ui/theme_ini.c, included here so the
// scaffold can walk its key table) built for the host. tools/theme_check.py
// builds it in the torii-emu container and runs it.
//
// usage: themecheck check DIR [FOLDER]
//            Loads DIR as ui_theme_load loads /torii/themes/FOLDER (FOLDER:
//            DIR's last part by default) and prints the lines the kernel
//            logs, "kernel: theme: ..." and "kernel error: theme: ...".
//            Exits 0 if the kernel would load it, 1 if it would refuse it.
//        themecheck new NAME [dark|light]
//            Prints a theme.ini with every key at the base theme's value.
//        themecheck name NAME
//            Exits 0 if NAME can be a theme's folder (theme_name_ok).
//        themecheck selftest
//            Checks that `new` writes what parses back to each built-in.
//
// File names are looked up ignoring ASCII case, as FAT does on the card.
#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "../source/tlog.h"
#include "../source/ui/theme_ini.c"

// The engine code linked in (the budget checker's) may log: to stderr
void tlog(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputc('\n', stderr);
}

void tlog_error(const char *format, ...)
{
	va_list args;
	va_start(args, format);
	vfprintf(stderr, format, args);
	va_end(args);
	fputc('\n', stderr);
}

static unsigned warnings_seen;

static void print_warning(const char *message)
{
	printf("kernel: theme: %s\n", message);
	warnings_seen++;
}

static void print_error(const char *format, ...) __attribute__((format(printf, 1, 2)));
static void print_error(const char *format, ...)
{
	char msg[THEME_MESSAGE_MAX];  // as ui_sdtheme.c's sd.error
	va_list args;
	va_start(args, format);
	vsnprintf(msg, sizeof(msg), format, args);
	va_end(args);
	printf("kernel error: theme: %s\n", msg);
}

// The path of the file in dir whose name matches name ignoring ASCII case
// (a regular file, as f_stat with no AM_DIR); 0 if none
static int find_file(const char *dir, const char *name, char *path, size_t cap)
{
	DIR *d = opendir(dir);
	if (!d)
		return 0;
	int found = 0;
	struct dirent *e;
	// Sorted lookups aren't needed: two names differing only in case can't
	// both exist on FAT, so the first match is the one the card would have
	while (!found && (e = readdir(d)) != 0) {
		if (strcasecmp(e->d_name, name))
			continue;
		snprintf(path, cap, "%s/%s", dir, e->d_name);
		FILE *f = fopen(path, "rb");
		if (f) {
			// fopen opens folders on Linux; reading one fails
			char c;
			found = fread(&c, 1, 1, f) == 1 || !ferror(f);
			fclose(f);
		}
	}
	closedir(d);
	return found;
}

// Reads up to max bytes of path into a new buffer; the file's size in *size
// (which may be more than max). NULL if it can't be read.
static u8 *read_file(const char *path, u32 max, u32 *size)
{
	FILE *f = fopen(path, "rb");
	if (!f)
		return 0;
	u8 *buf = malloc(max + 1);
	size_t got = buf ? fread(buf, 1, max, f) : 0;
	int err = !buf || ferror(f);
	long total = 0;
	if (!err && fseek(f, 0, SEEK_END) == 0)
		total = ftell(f);
	fclose(f);
	if (err || total < 0) {
		free(buf);
		return 0;
	}
	*size = (u32)total < got ? (u32)got : (u32)total;
	return buf;
}

static int check(const char *dir, const char *folder)
{
	if (!theme_name_ok(folder)) {
		print_error(THEME_MSG_BAD_FOLDER, folder);
		return 1;
	}
	char path[4096];
	if (!find_file(dir, "theme.ini", path, sizeof(path))) {
		print_error(THEME_MSG_NO_INI, folder);
		return 1;
	}
	u32 size = 0;
	u8 *text = read_file(path, THEME_INI_MAX, &size);
	if (!text) {
		print_error(THEME_MSG_INI_READ, folder);
		return 1;
	}
	if (size > THEME_INI_MAX) {
		free(text);
		print_error(THEME_MSG_INI_BIG, folder);
		return 1;
	}
	static struct theme_ini ini;
	ini.on_warning = print_warning;
	int r = theme_ini_parse((const char *)text, size, &ini);
	free(text);
	if (r != THEME_OK) {
		print_error("%s: %s", folder, ini.message);
		return 1;
	}
	int wallpaper = 0;
	if (ini.wallpaper[0]) {
		if (!find_file(dir, ini.wallpaper, path, sizeof(path))) {
			printf("kernel: theme: " THEME_MSG_NO_WALLPAPER "\n", folder, ini.wallpaper);
			ini.warnings++;
		} else {
			u8 *data = read_file(path, ASSET_FILE_MAX, &size);
			char msg[THEME_MESSAGE_MAX];
			if (!data) {
				print_error("%s: wallpaper %s can't be read", folder, ini.wallpaper);
				return 1;
			}
			int ok = theme_wallpaper_check(folder, ini.wallpaper, data, size, msg, sizeof(msg));
			free(data);
			if (!ok) {
				print_error("%s", msg);
				return 1;
			}
			wallpaper = 1;
		}
	}
	printf("kernel: theme: " THEME_MSG_LOADED "\n", folder, ini.name[0] ? ini.name : folder, (unsigned)ini.warnings,
	       wallpaper ? ", a wallpaper" : "");
	// Not a kernel line: for tools/theme_check.py, which decodes it
	if (wallpaper)
		printf("theme-check: wallpaper %s\n", path);
	return 0;
}

// --- The scaffold -------------------------------------------------------------

static void write_theme(FILE *f, const char *name, const struct ui_theme *base)
{
	const char *base_name = base == &ui_theme_light ? "light" : "dark";
	fprintf(f, "; %s: a Torii theme. docs/themes.md explains every key. Each one but\n", name);
	fprintf(f, "; version is optional: a key left out keeps the base theme's value.\n");
	fprintf(f, "; The values below are the %s theme's.\n", base_name);
	fprintf(f, "version = %d\nname = %s\nbase = %s\n", THEME_VERSION, name, base_name);
	const u8 *theme = (const u8 *)base;
	u32 section = S_TOP;
	for (u32 i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
		const struct key *k = &keys[i];
		if (k->section != section) {
			section = k->section;
			fprintf(f, "\n[%s]\n", section_names[section]);
			if (section == S_COLOURS)
				fprintf(f, "; #RRGGBB, rounded down to the GBA's 15 bits\n");
		}
		switch (k->kind) {
		case K_U8:
		case K_ROW_H:
			fprintf(f, "%s = %u\n", k->name, theme[k->offset]);
			break;
		case K_COLOUR: {
			u16 c;
			memcpy(&c, theme + k->offset, sizeof(c));
			fprintf(f, "%s = #%02X%02X%02X\n", k->name, (c & 31) << 3, (c >> 5 & 31) << 3,
				(c >> 10 & 31) << 3);
			break;
		}
		case K_FONT: {
			const struct torii_font *font;
			memcpy(&font, theme + k->offset, sizeof(font));
			fprintf(f, "%s = %s\n", k->name, font == &font_heading ? "heading" : "body");
			break;
		}
		case K_FILE:
			fprintf(f, "; A 4bpp TAS1 asset in this folder, repeated behind the menu; make one\n");
			fprintf(f, "; with `just theme-wallpaper PICTURE.png THEME/wallpaper.tas`\n");
			fprintf(f, "; %s = wallpaper.tas\n", k->name);
			break;
		default:
			break;
		}
	}
	fprintf(f, "\n[%s]\n", section_names[S_PATCHES]);
	fprintf(f, "; VIEW.NODE.FIELD = VALUE moves, hides or recolours a node, e.g.\n");
	fprintf(f, "; ui-demo.4.x = 16\n");
	for (u32 i = 0; i < base->patch_count; i++) {
		const struct ui_patch *p = &base->patches[i];
		fprintf(f, "%u.%u.%s = %d\n", p->view, p->node, field_names[p->field], p->value);
	}
}

static int selftest(void)
{
	static const struct ui_theme *const bases[] = {&ui_theme_dark, &ui_theme_light};
	int failed = 0;
	for (u32 b = 0; b < 2; b++) {
		const struct ui_theme *base = bases[b];
		char *text = 0;
		size_t len = 0;
		FILE *f = open_memstream(&text, &len);
		if (!f)
			return 1;
		write_theme(f, "Selftest", base);
		fclose(f);
		static struct theme_ini ini;
		warnings_seen = 0;
		ini.on_warning = print_warning;
		int r = theme_ini_parse(text, (u32)len, &ini);
		free(text);
		const u8 *want = (const u8 *)base, *got = (const u8 *)&ini.theme;
		int same_values = r == THEME_OK && ini.base == base && !warnings_seen &&
				  ini.theme.patch_count == base->patch_count && !strcmp(ini.name, "Selftest") &&
				  !ini.wallpaper[0];
		for (u32 i = 0; i < sizeof(keys) / sizeof(keys[0]); i++) {
			const struct key *k = &keys[i];
			u32 n = k->kind == K_COLOUR ? 2
				: k->kind == K_FONT ? sizeof(void *)
				: k->kind == K_FILE ? 0
						    : 1;
			if (memcmp(want + k->offset, got + k->offset, n)) {
				printf("selftest: %s: %s differs after a round trip\n", base->name, k->name);
				same_values = 0;
			}
		}
		if (!same_values) {
			printf("FAIL: themecheck new %s doesn't parse back to it\n", base->name);
			failed = 1;
		}
	}
	if (!failed)
		printf("PASS: themecheck new writes each built-in theme as it is\n");
	return failed;
}

int main(int argc, char **argv)
{
	if (argc >= 3 && argc <= 4 && !strcmp(argv[1], "check")) {
		const char *folder = argv[3];
		if (!folder) {
			// The last part of DIR, without trailing slashes
			static char last[4096];
			snprintf(last, sizeof(last), "%s", argv[2]);
			size_t n = strlen(last);
			while (n > 1 && last[n - 1] == '/')
				last[--n] = 0;
			const char *slash = strrchr(last, '/');
			folder = slash ? slash + 1 : last;
		}
		return check(argv[2], folder);
	}
	if (argc >= 3 && argc <= 4 && !strcmp(argv[1], "new")) {
		const char *base = argc == 4 ? argv[3] : "dark";
		if (strcmp(base, "dark") && strcmp(base, "light")) {
			fprintf(stderr, "themecheck: the base is dark or light, not %s\n", base);
			return 2;
		}
		write_theme(stdout, argv[2], !strcmp(base, "light") ? &ui_theme_light : &ui_theme_dark);
		return 0;
	}
	if (argc == 2 && !strcmp(argv[1], "selftest"))
		return selftest();
	if (argc == 3 && !strcmp(argv[1], "name"))
		return !theme_name_ok(argv[2]);
	fprintf(stderr, "usage: themecheck check DIR [FOLDER] | new NAME [dark|light] | name NAME | selftest\n");
	return 2;
}
