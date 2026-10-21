// The library index's hardware-free part (source/library.c): the extension
// table, names and sort keys, building and sorting a slot, its file image,
// and the queries and updates on it.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/crc32.h"
#include "../../source/gamename.h"
#include "../../source/library.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static void put(void *dst, const void *src, u32 bytes)
{
	CHECK(!(bytes & 1) && !((size_t)dst & 1) && !((size_t)src & 1));
	memcpy(dst, src, bytes);
}

static u8 *new_slot(void)
{
	u8 *slot = malloc(LIB_SLOT_BYTES);
	memset(slot, 0xA5, LIB_SLOT_BYTES);  // nothing may rely on a zeroed slot
	return slot;
}

static u8 tmp[LIBRARY_GAMES_MAX * 2];
static u32 seen[LIBRARY_GAMES_MAX / 32];

static void sort_all(u8 *slot, u32 budget)
{
	struct lib_sort s;
	lib_sort_begin(&s, slot, tmp, lib_hdr(slot)->records, put);
	u32 steps = 0;
	while (!lib_sort_step(&s, budget))
		steps++;
	CHECK(steps < 1000000);
}

void test_library_extensions(void)
{
	CHECK_EQ(library_console_of("Game.gba"), LIB_GBA);
	CHECK_EQ(library_console_of("GAME.GBA"), LIB_GBA);
	CHECK_EQ(library_console_of("a.gbc"), LIB_GBC);
	CHECK_EQ(library_console_of("tetris.gb"), LIB_GB);
	CHECK_EQ(library_console_of("Zelda.Nes"), LIB_NES);
	CHECK_EQ(library_console_of("notes.txt"), LIB_CONSOLE_NONE);
	CHECK_EQ(library_console_of("x.sgb"), LIB_CONSOLE_NONE);
	CHECK_EQ(library_console_of("gba"), LIB_CONSOLE_NONE);
	CHECK_EQ(library_console_of(".gba"), LIB_CONSOLE_NONE);
	CHECK_EQ(library_console_of("game.gba.txt"), LIB_CONSOLE_NONE);
	CHECK_EQ(library_console_of(""), LIB_CONSOLE_NONE);
}

static u32 key(char *out, const char *s)
{
	return library_sort_key(out, LIBRARY_KEY_MAX + 1, s);
}

static int key_cmp(const char *a, const char *b)
{
	char ka[LIBRARY_KEY_MAX + 1], kb[LIBRARY_KEY_MAX + 1];
	key(ka, a);
	key(kb, b);
	return strcmp(ka, kb);  // unsigned byte order (C's strcmp)
}

void test_library_sort_key(void)
{
	char k[LIBRARY_KEY_MAX + 1];
	CHECK_EQ(key(k, "Abc"), 3);
	CHECK(!strcmp(k, "abc"));
	CHECK_EQ(key(k, "Mega Man 10"), 13);
	CHECK(!memcmp(k,
		      "mega man \x01\x02"
		      "10",
		      13));
	key(k, "007");
	CHECK(!strcmp(k, "\x01\x01"
			 "7"));
	key(k, "0");
	CHECK(!strcmp(k, "\x01\x01"
			 "0"));
	key(k, "Pok\xC3\xA9mon \xC3\x86on Stra\xC3\x9F"
	       "e");
	CHECK(!strcmp(k, "pokemon aeon strasse"));
	// Numbers by value
	CHECK(key_cmp("Mega Man 2", "Mega Man 10") < 0);
	CHECK(key_cmp("Mega Man 9", "Mega Man 10") < 0);
	CHECK(key_cmp("Game 02", "Game 2") == 0);  // tied: the file name decides
	CHECK(key_cmp("Game", "Game 2") < 0);
	CHECK(key_cmp("2 Games", "Advance Wars") < 0);
	// Case and accents folded
	CHECK(key_cmp("pokemon", "POKEMON") == 0);
	CHECK(key_cmp("Pok\xC3\xA9mon", "Pokemon") == 0);
	CHECK(key_cmp("\xC3\x89lite", "Echo") > 0);
	CHECK(key_cmp("\xC3\x89lite", "Fire") < 0);
	// The name shown sorts: a leading "The" counts (R3)
	CHECK(key_cmp("The Legend of Zelda", "Tetris") > 0);
	CHECK(key_cmp("The Legend of Zelda", "Zelda") < 0);
	// Other scripts after z
	CHECK(key_cmp("\xE3\x83\x9D\xE3\x82\xB1\xE3\x83\xA2\xE3\x83\xB3", "Zelda") > 0);
	CHECK(key_cmp("\xC5\x81\xC3\xB3\x64\xC5\xBA", "Lodz") == 0);  // Extended-A folds too
	// The cap
	char longname[600];
	memset(longname, 'a', sizeof(longname) - 1);
	longname[sizeof(longname) - 1] = 0;
	CHECK_EQ(key(k, longname), LIBRARY_KEY_MAX);
	char tiny[4];
	CHECK_EQ(library_sort_key(tiny, sizeof(tiny), "abcdef"), 3);
	CHECK(!strcmp(tiny, "abc"));
	CHECK_EQ(library_sort_key(tiny, sizeof(tiny), "1"), 3);
	CHECK(!memcmp(tiny,
		      "\x01\x01"
		      "1",
		      4));
	// The prefix orders as the key does
	CHECK_EQ(library_key_prefix("ab"), 0x61620000);
	CHECK_EQ(library_key_prefix("abcdef"), 0x61626364);
	CHECK(library_key_prefix("\xC3\xA9") > library_key_prefix("zzzz"));
}

static const char *const sample_names[] = {
	"Mega Man 10.gba",         "Mega Man 2.gba",      "mega man 2 (Europe).gba",
	"Advance Wars.gba",        "Zelda.gbc",           "Tetris.gb",
	"Pok\xC3\xA9mon Ruby.gba", "Pokemon Emerald.gba", "Legend of Zelda, The (USA).gb",
	"Super Mario Bros.nes",    "007 Nightfire.gba",   "Metroid Fusion.gba",
};
#define SAMPLES (sizeof(sample_names) / sizeof(sample_names[0]))

// Builds the samples, the GBA ones in two folders
static void build_samples(u8 *slot, struct lib_build *b)
{
	lib_build_begin(b, slot, put);
	u16 root = lib_build_folder(b, "/");
	u16 sub = lib_build_folder(b, "/GBA/Old");
	CHECK_EQ(root, 0);
	CHECK_EQ(sub, 1);
	for (u32 i = 0; i < SAMPLES; i++)
		CHECK(lib_build_game(b, i & 1 ? sub : root, sample_names[i], NULL, 1000 + i,
				     library_console_of(sample_names[i]), i == 3 ? LIB_F_SAVE : 0));
	struct lib_header h;
	lib_build_header(b, &h, 0x1234, 0xBEEF, 99);
}

void test_library_build_and_sort(void)
{
	u8 *slot = new_slot();
	static struct lib_build b;
	build_samples(slot, &b);
	const struct lib_header *h = lib_hdr(slot);
	CHECK_EQ(h->records, SAMPLES);
	CHECK_EQ(h->counts[LIB_GBA], 8);
	CHECK_EQ(h->counts[LIB_GBC], 1);
	CHECK_EQ(h->counts[LIB_GB], 2);
	CHECK_EQ(h->counts[LIB_NES], 1);
	CHECK_EQ(h->total_files, 99);
	CHECK_EQ(h->walk_hash, 0x1234);
	CHECK(!(h->flags & LIB_H_TRUNCATED));
	sort_all(slot, 1);
	CHECK(lib_body_ok(slot, seen));
	static const char *const gba[] = {
		"007 Nightfire.gba", "Advance Wars.gba",   "Mega Man 2.gba",      "mega man 2 (Europe).gba",
		"Mega Man 10.gba",   "Metroid Fusion.gba", "Pokemon Emerald.gba", "Pok\xC3\xA9mon Ruby.gba",
	};
	for (u32 i = 0; i < 8; i++) {
		u32 rec = lib_nth(slot, LIB_GBA, i);
		CHECK(rec != LIBRARY_NONE && !strcmp(lib_name(slot, rec), gba[i]));
		if (rec != LIBRARY_NONE && strcmp(lib_name(slot, rec), gba[i]))
			tlog_error("FAIL GBA %u is %s, expected %s", (unsigned)i, lib_name(slot, rec), gba[i]);
	}
	CHECK_EQ(lib_nth(slot, LIB_GBA, 8), LIBRARY_NONE);
	// "The Legend of Zelda" after "Tetris": the name shown sorts (R3)
	CHECK(!strcmp(lib_name(slot, lib_nth(slot, LIB_GB, 0)), "Tetris.gb"));
	CHECK(!strcmp(lib_name(slot, lib_nth(slot, LIB_GB, 1)), "Legend of Zelda, The (USA).gb"));
	CHECK(!strcmp(lib_name(slot, lib_nth(slot, LIB_NES, 0)), "Super Mario Bros.nes"));
	CHECK(lib_rec_at(slot, 3)->flags & LIB_F_SAVE);
	CHECK(!(lib_rec_at(slot, 2)->flags & LIB_F_SAVE));
	// Paths and lookups
	char path[300];
	u32 rec = lib_nth(slot, LIB_GBA, 1);  // Advance Wars, record 3, in the sub folder
	CHECK_EQ(rec, 3);
	CHECK_EQ(lib_path(slot, rec, path, sizeof(path)), strlen("/GBA/Old/Advance Wars.gba"));
	CHECK(!strcmp(path, "/GBA/Old/Advance Wars.gba"));
	CHECK_EQ(lib_find(slot, "/GBA/Old/Advance Wars.gba"), 3);
	CHECK_EQ(lib_find(slot, "/Advance Wars.gba"), LIBRARY_NONE);
	CHECK_EQ(lib_find(slot, "/Mega Man 10.gba"), 0);
	CHECK_EQ(lib_path(slot, 0, path, sizeof(path)), strlen("/Mega Man 10.gba"));
	CHECK_EQ(lib_path(slot, 0, path, 10), 0);
	CHECK_EQ(lib_find(slot, "no slash"), LIBRARY_NONE);
	free(slot);
}

// A thousand games with random names in random consoles sort the same with
// any step budget, and the order agrees with lib_compare
void test_library_sort_random(void)
{
	u8 *slot = new_slot();
	static struct lib_build b;
	lib_build_begin(&b, slot, put);
	u16 f[3] = {lib_build_folder(&b, "/"), lib_build_folder(&b, "/b"), lib_build_folder(&b, "/a")};
	srand(7);
	for (u32 i = 0; i < 1000; i++) {
		char name[32];
		static const char *const ext[] = {"gba", "gbc", "gb", "nes"};
		snprintf(name, sizeof(name), "%c%c %u.%s", 'A' + rand() % 4, 'a' + rand() % 3, (unsigned)(rand() % 30),
			 ext[rand() % 4]);
		CHECK(lib_build_game(&b, f[rand() % 3], name, NULL, i, library_console_of(name), 0));
	}
	struct lib_header h;
	lib_build_header(&b, &h, 0, 0, 1000);
	sort_all(slot, 7);
	CHECK(lib_body_ok(slot, seen));
	static u16 first[1000];
	memcpy(first, lib_order(slot), sizeof(first));
	for (u32 i = 1; i < 1000; i++)
		CHECK(lib_compare(slot, first[i - 1], first[i]) < 0);
	sort_all(slot, 100000);
	CHECK(!memcmp(first, lib_order(slot), sizeof(first)));
	free(slot);
}

void test_library_cap(void)
{
	u8 *slot = new_slot();
	static struct lib_build b;
	lib_build_begin(&b, slot, put);
	u16 root = lib_build_folder(&b, "/");
	for (u32 i = 0; i <= LIBRARY_GAMES_MAX; i++) {
		char name[32];
		snprintf(name, sizeof(name), "Game %04u.gba", (unsigned)i);
		CHECK_EQ(lib_build_game(&b, root, name, NULL, i, LIB_GBA, 0),
			 i < LIBRARY_GAMES_MAX ? LIB_ADDED : LIB_FULL);
	}
	struct lib_header h;
	lib_build_header(&b, &h, 0, 0, LIBRARY_GAMES_MAX + 1);
	CHECK_EQ(lib_hdr(slot)->records, LIBRARY_GAMES_MAX);
	CHECK_EQ(lib_hdr(slot)->games_seen, LIBRARY_GAMES_MAX + 1);
	CHECK(lib_hdr(slot)->flags & LIB_H_TRUNCATED);
	sort_all(slot, 64);
	CHECK(lib_body_ok(slot, seen));
	CHECK(!strcmp(lib_name(slot, lib_nth(slot, LIB_GBA, 0)), "Game 0000.gba"));
	CHECK(!strcmp(lib_name(slot, lib_nth(slot, LIB_GBA, 4095)), "Game 4095.gba"));
	// The folder table and names past the caps
	lib_build_begin(&b, slot, put);
	for (u32 i = 0; i < LIBRARY_FOLDERS_MAX; i++)
		CHECK(lib_build_folder(&b, "/f") != LIBRARY_NONE);
	CHECK_EQ(lib_build_folder(&b, "/g"), LIBRARY_NONE);
	CHECK(b.flags & LIB_H_TRUNCATED);
	lib_build_begin(&b, slot, put);
	char longname[LIBRARY_NAME_MAX + 2];
	memset(longname, 'x', sizeof(longname));
	memcpy(longname + LIBRARY_NAME_MAX - 3, ".gba", 5);  // 256 bytes
	// A name too long is no game and no reason for "showing the first N"
	CHECK_EQ(lib_build_game(&b, lib_build_folder(&b, "/"), longname, NULL, 1, LIB_GBA, 0), LIB_BAD_NAME);
	CHECK(!(b.flags & LIB_H_TRUNCATED));
	CHECK_EQ(b.games_seen, 0);
	CHECK_EQ(lib_build_folder(&b, longname), LIBRARY_NONE);
	CHECK(!(b.flags & LIB_H_TRUNCATED));
	memcpy(longname + LIBRARY_NAME_MAX - 4, ".gba", 5);  // 255 bytes: fits
	CHECK_EQ(lib_build_game(&b, 0, longname, NULL, 1, LIB_GBA, 0), LIB_ADDED);
	free(slot);
}

// Writes a slot as library_scan.c does, into file; returns its size
static u32 write_image(const u8 *slot, u8 *file)
{
	struct lib_header h = *lib_hdr(slot);
	struct lib_section s[LIB_SECTIONS];
	lib_sections(&h, s);
	u32 at = sizeof(h), crc = 0;
	for (u32 i = 0; i < LIB_SECTIONS; i++) {
		memcpy(file + at, slot + s[i].off, s[i].bytes);
		crc = crc32_update(crc, file + at, s[i].bytes);
		at += s[i].bytes;
	}
	h.body_crc = crc;
	h.header_crc = lib_header_crc(&h);
	memcpy(file, &h, sizeof(h));
	CHECK_EQ(at, lib_file_bytes(&h));
	return at;
}

// Reads it back as library_scan.c does: 1 if it is whole
static int read_image(u8 *slot, const u8 *file, u32 size)
{
	struct lib_header h;
	memcpy(&h, file, sizeof(h));
	if (!lib_header_ok(&h) || lib_file_bytes(&h) != size)
		return 0;
	struct lib_section s[LIB_SECTIONS];
	lib_sections(&h, s);
	u32 at = sizeof(h), crc = 0;
	for (u32 i = 0; i < LIB_SECTIONS; i++) {
		memcpy(slot + s[i].off, file + at, s[i].bytes);
		crc = crc32_update(crc, file + at, s[i].bytes);
		at += s[i].bytes;
	}
	memcpy(slot, &h, sizeof(h));
	return crc == h.body_crc && lib_body_ok(slot, seen);
}

void test_library_file_round_trip(void)
{
	u8 *slot = new_slot(), *back = new_slot();
	static struct lib_build b;
	build_samples(slot, &b);
	sort_all(slot, 3);
	static u8 file[0x4000];
	u32 size = write_image(slot, file);
	CHECK_EQ(size % 4, 0);
	CHECK(read_image(back, file, size));
	for (u32 c = 0; c < LIB_CONSOLES; c++)
		for (u32 n = 0; n < lib_hdr(slot)->counts[c]; n++)
			CHECK(!strcmp(lib_name(slot, lib_nth(slot, c, n)), lib_name(back, lib_nth(back, c, n))));
	CHECK_EQ(lib_hdr(back)->volume_serial, 0xBEEF);
	// Any damage is caught: a header byte, a body byte, a short file
	file[13] ^= 1;
	CHECK(!read_image(back, file, size));
	file[13] ^= 1;
	file[size - 40] ^= 0x10;
	CHECK(!read_image(back, file, size));
	file[size - 40] ^= 0x10;
	CHECK(!read_image(back, file, size - 4));
	CHECK(read_image(back, file, size));
	// Another version is a rebuild
	struct lib_header h;
	memcpy(&h, file, sizeof(h));
	h.version++;
	h.header_crc = lib_header_crc(&h);
	CHECK(!lib_header_ok(&h));
	free(slot);
	free(back);
}

void test_library_forget_rename(void)
{
	u8 *slot = new_slot();
	static struct lib_build b, scratch;
	build_samples(slot, &b);
	sort_all(slot, 5);
	u32 mm2 = lib_find(slot, "/GBA/Old/Mega Man 2.gba");
	CHECK(mm2 != LIBRARY_NONE);
	CHECK(lib_forget(slot, put, mm2));
	CHECK(!lib_forget(slot, put, mm2));
	CHECK_EQ(lib_hdr(slot)->counts[LIB_GBA], 7);
	CHECK_EQ(lib_find(slot, "/GBA/Old/Mega Man 2.gba"), LIBRARY_NONE);
	CHECK(lib_body_ok(slot, seen));
	CHECK(!strcmp(lib_name(slot, lib_nth(slot, LIB_GBA, 2)), "mega man 2 (Europe).gba"));
	CHECK(!strcmp(lib_name(slot, lib_nth(slot, LIB_GB, 1)), "Legend of Zelda, The (USA).gb"));
	// A rename moves the game to its new place
	u32 aw = lib_find(slot, "/GBA/Old/Advance Wars.gba");
	CHECK(lib_rename(slot, put, aw, "Zoo Tycoon.gba", &scratch));
	CHECK(lib_body_ok(slot, seen));
	CHECK_EQ(lib_find(slot, "/GBA/Old/Zoo Tycoon.gba"), aw);
	CHECK_EQ(lib_find(slot, "/GBA/Old/Advance Wars.gba"), LIBRARY_NONE);
	CHECK_EQ(lib_nth(slot, LIB_GBA, 6), aw);
	CHECK(lib_rec_at(slot, aw)->flags & LIB_F_SAVE);
	CHECK(lib_rename(slot, put, aw, "AAA.gba", &scratch));
	CHECK_EQ(lib_nth(slot, LIB_GBA, 1), aw);  // after 007
	CHECK(lib_body_ok(slot, seen));
	// Not to another console, not a gone record
	CHECK(!lib_rename(slot, put, aw, "AAA.gb", &scratch));
	CHECK(!lib_rename(slot, put, mm2, "BBB.gba", &scratch));
	// A game listed twice (and so another missing) is caught
	u16 *order = (u16 *)(slot + LIB_OFF_ORDER), keep = order[1];
	order[1] = order[0];
	CHECK(!lib_body_ok(slot, seen));
	order[1] = keep;
	CHECK(lib_body_ok(slot, seen));
	// A rename keeps the order sorted
	for (u32 c = 0; c < LIB_CONSOLES; c++)
		for (u32 n = 1; n < lib_hdr(slot)->counts[c]; n++)
			CHECK(lib_compare(slot, lib_nth(slot, c, n - 1), lib_nth(slot, c, n)) < 0);
	free(slot);
}

// A custom name (names.txt) sorts the game by it, as the lists show it (R3)
void test_library_custom_display(void)
{
	u8 *slot = new_slot();
	static struct lib_build b;
	lib_build_begin(&b, slot, put);
	u16 root = lib_build_folder(&b, "/");
	CHECK_EQ(lib_build_game(&b, root, "Alpha.gba", NULL, 1, LIB_GBA, 0), LIB_ADDED);
	CHECK_EQ(lib_build_game(&b, root, "Beta.gba", "Zeta, my hack", 1, LIB_GBA, LIB_F_CUSTOM), LIB_ADDED);
	CHECK_EQ(lib_build_game(&b, root, "Gamma (USA) [!].gba", NULL, 1, LIB_GBA, LIB_F_SAVE), LIB_ADDED);
	// A display name written into the build's own buffer, as the walk does
	strcpy(lib_build_display(&b), "Delta Custom");
	CHECK_EQ(lib_build_game(&b, root, "Delta.gba", lib_build_display(&b), 1, LIB_GBA, LIB_F_CUSTOM), LIB_ADDED);
	struct lib_header h;
	lib_build_header(&b, &h, 0, 0, 4);
	sort_all(slot, 4);
	CHECK(!strcmp(lib_name(slot, lib_nth(slot, LIB_GBA, 0)), "Alpha.gba"));
	CHECK(!strcmp(lib_name(slot, lib_nth(slot, LIB_GBA, 1)), "Delta.gba"));
	CHECK(!strcmp(lib_name(slot, lib_nth(slot, LIB_GBA, 2)), "Gamma (USA) [!].gba"));
	CHECK(!strcmp(lib_name(slot, lib_nth(slot, LIB_GBA, 3)), "Beta.gba"));
	// Each keeps the name the lists show, after its key, and its flags
	CHECK(!strcmp(lib_display(slot, lib_nth(slot, LIB_GBA, 0)), "Alpha"));
	CHECK(!strcmp(lib_display(slot, lib_nth(slot, LIB_GBA, 1)), "Delta Custom"));
	CHECK(!strcmp(lib_display(slot, lib_nth(slot, LIB_GBA, 2)), "Gamma"));
	CHECK(!strcmp(lib_display(slot, lib_nth(slot, LIB_GBA, 3)), "Zeta, my hack"));
	CHECK(!strcmp(lib_key(slot, lib_nth(slot, LIB_GBA, 3)), "zeta, my hack"));
	CHECK_EQ(lib_rec_at(slot, lib_nth(slot, LIB_GBA, 3))->flags, LIB_GBA | LIB_F_CUSTOM);
	CHECK_EQ(lib_rec_at(slot, lib_nth(slot, LIB_GBA, 2))->flags, LIB_GBA | LIB_F_SAVE);
	CHECK(lib_body_ok(slot, seen));
	// And back from a record to its place in the list
	for (u32 n = 0; n < 4; n++)
		CHECK_EQ(lib_position(slot, lib_nth(slot, LIB_GBA, n)), n);
	CHECK_EQ(lib_position(slot, 99), LIBRARY_NONE);
	free(slot);
}

// The stored order depends on gamename_clean and the sort key: these pinned
// keys fail when either changes, and then LIBRARY_VERSION goes up (so every
// card's index is built again) and the keys here are updated.
void test_library_pinned_keys(void)
{
	static const struct {
		const char *file;
		const char *key;
	} pinned[] = {
		{"Legend of Zelda, The - The Minish Cap (Europe) (En,Fr,De,Es,It).gba",
		 "the legend of zelda - the minish cap"},
		{"0001 - Mega Man Zero 2 (USA) [!].gba", "mega man zero \x01\x01"
							 "2"},
		{"Pok\xC3\xA9mon - Emerald Version (USA, Europe).gba", "pokemon - emerald version"},
		{"Final_Fantasy_IV_Advance.gba", "final fantasy iv advance"},
		{"Castlevania - Aria of Sorrow (USA) (Rev 1).gba", "castlevania - aria of sorrow"},
		{"(Unlicensed).gba", "(unlicensed)"},
	};
	CHECK_EQ(LIBRARY_VERSION, 3);
	for (u32 i = 0; i < sizeof(pinned) / sizeof(pinned[0]); i++) {
		char display[GAMENAME_MAX + 1], k[LIBRARY_KEY_MAX + 1];
		gamename_clean(display, pinned[i].file);
		library_sort_key(k, sizeof(k), display);
		if (strcmp(k, pinned[i].key))
			tlog_error("FAIL the key of \"%s\" is \"%s\": bump LIBRARY_VERSION, then update the pinned key",
				   pinned[i].file, k);
		CHECK(!strcmp(k, pinned[i].key));
	}
}
