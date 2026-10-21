// Display names, names.txt and the hidden rule (source/gamename.c). Inputs
// are heap copies of exactly their bytes and outputs exactly GAMENAME_MAX + 1
// bytes, so AddressSanitizer catches a read or write past either.
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "../../source/gamename.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static char *copy(const char *s)
{
	char *p = malloc(strlen(s) + 1);
	strcpy(p, s);
	return p;
}

static void check_clean(const char *path, const char *expected)
{
	char *in = copy(path);
	char *out = malloc(GAMENAME_MAX + 1);
	u32 len = gamename_clean(out, in);
	if (strcmp(out, expected) != 0 || len != strlen(expected)) {
		tlog_error("FAIL %s: %s cleans to \"%s\" (%u), expected \"%s\"", __FILE__, path, out, (unsigned)len,
			   expected);
		++torii_test_failures;
	}
	free(out);
	free(in);
}

void test_gamename_clean_real_names(void)
{
	// No-Intro names
	check_clean("Advance Wars (USA).gba", "Advance Wars");
	check_clean("/GBA/Legend of Zelda, The - A Link to the Past (USA).gba",
		    "The Legend of Zelda - A Link to the Past");
	check_clean("Monde Perdu, Le - Jurassic Park (France) (En) (SGB Enhanced).gb",
		    "Le Monde Perdu - Jurassic Park");
	check_clean("Aventures de Buzz l'Eclair, Les (France).gbc", "Les Aventures de Buzz l'Eclair");
	check_clean("Pokemon - Emerald Version (USA, Europe).gba", "Pokemon - Emerald Version");
	check_clean("Mario & Luigi - Superstar Saga (USA, Australia).gba", "Mario & Luigi - Superstar Saga");
	check_clean("Adventures of Jimmy Neutron Boy Genius, The - Attack of the Twonkies (USA).gba",
		    "The Adventures of Jimmy Neutron Boy Genius - Attack of the Twonkies");
	check_clean("Maus, Die (Europe).gb", "Die Maus");
	check_clean("Linea, La (World).nes", "La Linea");
	check_clean("Dr. Mario (World).nes", "Dr. Mario");
	check_clean("1943 - The Battle of Midway (USA).nes", "1943 - The Battle of Midway");
	// GoodTools tags and scene sets
	check_clean("0001 - Game (U).gba", "Game");
	check_clean("12345 - Game (E).gba", "Game");
	check_clean("Game (U) [!].gba", "Game");
	check_clean("Game [T+Eng_v1.0].gba", "Game");
	check_clean("Game (Rev 1) [b1]", "Game");
	check_clean("Game (USA) (Disc 1) - (Hack).gba", "Game");
	check_clean("Game, The - (Disc 1).gba", "The Game");
	// UTF-8 names pass through whole
	check_clean("Pokémon Émeraude (France).gba", "Pokémon Émeraude");
	check_clean("日本語 (Japan).gba", "日本語");
	check_clean("Ōkami, L' (Japan).gba", "L'Ōkami");
	// Names that clean to nothing keep their name without the extension
	check_clean("(Unlicensed).gba", "(Unlicensed)");
	check_clean("0001 - (U).gba", "0001");
	check_clean("[b1].gb", "[b1]");
}

void test_gamename_clean_rules(void)
{
	// Rule 1: only the last short extension of letters and digits goes
	check_clean("Mega Man 4.5.gba", "Mega Man 4.5");
	check_clean(".hidden.gba", ".hidden");
	check_clean(".gba", ".gba");
	check_clean("...", "...");
	check_clean("Game.backup", "Game.backup");
	check_clean("Game", "Game");
	// Rule 2: underscores are spaces only in names without spaces
	check_clean("Super_Mario_Advance_(U).gba", "Super Mario Advance");
	check_clean("Game_Name (USA).gba", "Game_Name");
	// Rule 3: nested groups go whole; an unclosed one stays
	check_clean("Game (a (b)) [x (y)] Two.gba", "Game Two");
	check_clean("Game (USA.gba", "Game (USA");
	check_clean("Game) (USA).gba", "Game)");
	// Rule 4: 4 or 5 digits and a dash, with a letter after; years stay
	check_clean("007 - NightFire (USA).gba", "007 - NightFire");
	check_clean("0042-Game.gba", "Game");
	check_clean("123456 - Game.gba", "123456 - Game");
	check_clean("2005 - Game.gba", "2005 - Game");
	check_clean("1234 - 5678.gba", "1234 - 5678");
	check_clean("0001 - ゲーム.gba", "ゲーム");
	check_clean("0001 Game.gba", "0001 Game");
	// Rule 5: spaces collapse
	check_clean("  Game \t  Name  .gba", "Game Name");
	// Rule 6: articles move from the title part only, with case
	check_clean("Zelda, The - Link, A.gba", "The Zelda - Link, A");
	check_clean("Monde, de.gba", "Monde, de");
	check_clean(", The.gba", ", The");
	check_clean("Thing, An (USA).gba", "An Thing");
	check_clean("Ratatouille, Theme.gba", "Ratatouille, Theme");
	// Rule 7: stray separators, inner ones stay
	check_clean("- Game.gba", "Game");
	check_clean("Game - - Two.gba", "Game - Two");
	check_clean("A - B - C (USA).gba", "A - B - C");
	check_clean("(USA) - (Europe).gba", "(USA) - (Europe)");
}

void test_gamename_clean_limits(void)
{
	// A 255-byte name and a longer one cut at a character boundary
	char name[400], expected[GAMENAME_MAX + 1];
	memset(name, 'x', 251);
	memcpy(name + 251, ".gba", 5);
	memset(expected, 'x', 251);
	expected[251] = 0;
	check_clean(name, expected);
	// 300 bytes of two-byte characters: the cut keeps 127 whole ones
	for (int i = 0; i < 150; i++)
		memcpy(name + 2 * i, "é", 2);
	name[300] = 0;
	for (int i = 0; i < 127; i++)
		memcpy(expected + 2 * i, "é", 2);
	expected[254] = 0;
	check_clean(name, expected);
	check_clean("", "");

	char *out = malloc(GAMENAME_MAX + 1);
	CHECK_EQ(gamename_clean_title(out, "Vol.2 (USA)"), 5);
	CHECK(strcmp(out, "Vol.2") == 0);
	CHECK(gamename_same("Pokemon Emerald", "POKEMON emerald"));
	CHECK(!gamename_same("Pokemon Emerald", "Pokemon Emeral"));
	CHECK(!gamename_same("Pokémon", "Pokemon"));
	CHECK(gamename_cmp("abc", "ABD") < 0);
	CHECK(gamename_cmp("ABD", "abc") > 0);
	CHECK(gamename_cmp("ab", "abc") < 0);
	CHECK(gamename_cmp("Zelda", "é") < 0);
	CHECK_EQ(gamename_cmp("GAME", "game"), 0);
	free(out);
}

// Cleans and dedupes names[self] among the count names
static void check_dedupe(const char *const *names, u32 count, u32 self, const char *expected)
{
	char **in = malloc(count * sizeof(*in));
	for (u32 i = 0; i < count; i++)
		in[i] = copy(names[i]);
	char *out = malloc(GAMENAME_MAX + 1);
	gamename_clean(out, in[self]);
	u32 len = gamename_dedupe(out, (const char *const *)in, count, self);
	if (strcmp(out, expected) != 0 || len != strlen(expected)) {
		tlog_error("FAIL %s: %s dedupes to \"%s\", expected \"%s\"", __FILE__, names[self], out, expected);
		++torii_test_failures;
	}
	free(out);
	for (u32 i = 0; i < count; i++)
		free(in[i]);
	free(in);
}

void test_gamename_dedupe(void)
{
	const char *regions[] = {"/GBA/Pokemon Emerald (USA).gba", "/GBA/Pokemon Emerald (Europe).gba"};
	check_dedupe(regions, 2, 0, "Pokemon Emerald (USA)");
	check_dedupe(regions, 2, 1, "Pokemon Emerald (Europe)");
	check_dedupe(regions, 1, 0, "Pokemon Emerald");

	// No tag where the other has one: keeps none
	const char *rev[] = {"/Game (USA).gba", "/Game (USA) (Rev 1).gba"};
	check_dedupe(rev, 2, 0, "Game");
	check_dedupe(rev, 2, 1, "Game (Rev 1)");

	// A revision wins over an earlier language list (the kept tags)
	const char *langs[] = {"/Game (Europe) (En,Fr) (Rev 1).gba", "/Game (Europe) (En,De) (Rev 2).gba"};
	check_dedupe(langs, 2, 0, "Game (Rev 1)");
	check_dedupe(langs, 2, 1, "Game (Rev 2)");

	// Four that need two tags each
	const char *four[] = {"/G (USA) (Rev 1).gba", "/G (Europe) (Rev 1).gba", "/G (USA) (Rev 2).gba",
			      "/G (Europe) (Rev 2).gba"};
	check_dedupe(four, 4, 0, "G (USA) (Rev 1)");
	check_dedupe(four, 4, 1, "G (Europe) (Rev 1)");
	check_dedupe(four, 4, 3, "G (Europe) (Rev 2)");

	// GoodTools dump tags
	const char *dumps[] = {"/Game (U) [!].gba", "/Game (U) [b1].gba", "/Game [T+Eng].gba"};
	check_dedupe(dumps, 3, 0, "Game (U) (!)");
	check_dedupe(dumps, 3, 1, "Game (U) (b1)");
	check_dedupe(dumps, 3, 2, "Game (T+Eng)");
	// Underscores in a tag are spaces where cleaning made them spaces
	const char *scene[] = {"/Game_[T+Eng_v1.0].gba", "/Game (U).gba", "/A Game [T+Eng_v2].gba", "/A Game.gba"};
	check_dedupe(scene, 2, 0, "Game (T+Eng v1.0)");
	check_dedupe(scene, 2, 1, "Game (U)");
	check_dedupe(scene + 2, 2, 0, "A Game (T+Eng_v2)");

	// Same tags: the folder tells them apart; the root adds none
	const char *folders[] = {"/GBA/Hacks/Game (USA).gba", "/GBA/Game (USA).gba", "/Game (USA).gba"};
	check_dedupe(folders, 3, 0, "Game (Hacks)");
	check_dedupe(folders, 3, 1, "Game (GBA)");
	check_dedupe(folders, 3, 2, "Game");
	// In the same folder: the extension
	const char *exts[] = {"/A/G.gb", "/a/G.gbc", "/B/G.gb", "/G.gb", "/G.gbc"};
	check_dedupe(exts, 2, 0, "G (gb)");
	check_dedupe(exts, 2, 1, "G (gbc)");
	check_dedupe(exts, 3, 0, "G (A) (gb)");
	check_dedupe(exts, 3, 2, "G (B)");
	check_dedupe(exts + 3, 2, 0, "G (gb)");

	// Underscores and case clean the same, so they collide too
	const char *spelled[] = {"/Super_Game_(J).gba", "/super game (U).gba"};
	check_dedupe(spelled, 2, 0, "Super Game (J)");
	check_dedupe(spelled, 2, 1, "super game (U)");

	// The folder fills up to GAMENAME_MAX, cut at a character boundary
	char deep[300], other[300], expected[GAMENAME_MAX + 1];
	strcpy(deep, "/éééééééééé/");
	strcpy(other, "/b/");
	memset(expected, 'x', 240);
	expected[240] = 0;
	strcat(deep, expected);
	strcat(other, expected);
	strcat(deep, " (U).gba");
	strcat(other, " (U).gba");
	strcat(expected, " (éééééé)");
	const char *full[] = {deep, other};
	check_dedupe(full, 2, 0, expected);
	// Room for one byte: no separator and no empty brackets
	strcpy(deep, "/éé/");
	strcpy(other, "/b/");
	memset(expected, 'x', 252);
	expected[252] = 0;
	strcat(deep, expected);
	strcat(other, expected);
	strcat(deep, ".gb");
	strcat(other, ".gb");
	check_dedupe(full, 2, 0, expected);
	// Room for one two-byte character
	expected[250] = 0;
	deep[6 + 250] = 0;
	other[3 + 250] = 0;
	strcat(deep, ".gba");
	strcat(other, ".gba");
	strcat(expected, " (é)");
	check_dedupe(full, 2, 0, expected);

	// A kept tag wins even when the other name has none there: the beta
	// doesn't pass for the release
	const char *beta[] = {"/Game (USA, Europe).gba", "/Game (USA) (Beta).gba", "/Game (USA, Europe) (Beta).gba"};
	check_dedupe(beta, 3, 0, "Game");
	check_dedupe(beta, 3, 1, "Game (USA) (Beta)");
	check_dedupe(beta, 3, 2, "Game (USA, Europe) (Beta)");
}

// --- names.txt ------------------------------------------------------------------

struct custom {
	char *text;
	u32 line[GAMENAME_CUSTOM_MAX];
	struct gamename_custom c;
};

static void custom_parse(struct custom *m, const char *text, u32 cap)
{
	free(m->text);
	m->text = text ? malloc(strlen(text) + 1) : NULL;
	if (text)
		memcpy(m->text, text, strlen(text));  // no NUL: the parser must not need one
	gamename_custom_parse(&m->c, m->text, text ? (u32)strlen(text) : 0, m->line, cap);
}

static void check_get(struct custom *m, const char *path, const char *expected)
{
	char *key = copy(path);
	u32 len;
	const char *name = gamename_custom_get(&m->c, key, &len);
	int ok = expected ? name && len == strlen(expected) && memcmp(name, expected, len) == 0 : name == NULL;
	if (!ok) {
		tlog_error("FAIL %s: %s has \"%.*s\", expected \"%s\"", __FILE__, path, name ? (int)len : 6,
			   name ? name : "(none)", expected ? expected : "(none)");
		++torii_test_failures;
	}
	free(key);
}

// Sets path's name and checks the whole new file
static void check_set(struct custom *m, const char *path, const char *name, const char *expected)
{
	char out[1024];
	u32 len = gamename_custom_set(&m->c, path, name, out, sizeof(out));
	int ok = expected ? len == strlen(expected) && memcmp(out, expected, len) == 0 : GAMENAME_FAILED(len);
	if (!ok) {
		tlog_error("FAIL %s: setting %s gives \"%.*s\", expected \"%s\"", __FILE__, path,
			   GAMENAME_FAILED(len) ? 6 : (int)len, GAMENAME_FAILED(len) ? "failed" : out,
			   expected ? expected : "too big");
		++torii_test_failures;
	}
}

void test_gamename_custom_parse(void)
{
	struct custom *m = calloc(1, sizeof(*m));
	custom_parse(m,
		     "\xEF\xBB\xBF/GBA/Zelda.gba\tMy Zelda\r\n"
		     "# a comment\n"
		     "\n"
		     "/GBA/Advance Wars (USA).gba\t  Wars  \n"
		     "no slash\tName\n"
		     "/no tab\n"
		     "/GBA/blank.gba\t   \n"
		     "/GBA/zelda.GBA\tLater Zelda\n"
		     "/日本/ゲーム.gba\tゲーム!",
		     GAMENAME_CUSTOM_MAX);
	CHECK_EQ(m->c.count, 3);
	CHECK(m->c.complete);
	check_get(m, "/GBA/Zelda.gba", "Later Zelda");  // the later line, without case
	check_get(m, "/gba/ZELDA.gba", "Later Zelda");
	check_get(m, "/GBA/Advance Wars (USA).gba", "Wars");
	check_get(m, "/日本/ゲーム.gba", "ゲーム!");
	check_get(m, "/GBA/blank.gba", NULL);
	check_get(m, "/no tab", NULL);
	check_get(m, "/GBA/Advance Wars", NULL);
	check_get(m, "/", NULL);

	// No file, an empty one, one past the line cap
	custom_parse(m, NULL, GAMENAME_CUSTOM_MAX);
	CHECK_EQ(m->c.count, 0);
	CHECK(m->c.complete);
	check_get(m, "/a.gba", NULL);
	custom_parse(m, "", GAMENAME_CUSTOM_MAX);
	CHECK_EQ(m->c.count, 0);
	custom_parse(m, "/c\tC\n/a\tA\n/b\tB\n", 2);
	CHECK_EQ(m->c.count, 2);
	CHECK(!m->c.complete);
	check_get(m, "/a", "A");
	check_get(m, "/c", "C");
	check_set(m, "/d", "D", NULL);  // would lose /b
	char code[64];
	CHECK_EQ(gamename_custom_set(&m->c, "/d", "D", code, sizeof(code)), GAMENAME_INCOMPLETE);
	CHECK_EQ(gamename_custom_move(&m->c, "/a", "/d", code, sizeof(code)), GAMENAME_INCOMPLETE);

	// A name past GAMENAME_MAX reads cut
	char longest[400];
	memcpy(longest, "/a\t", 3);
	memset(longest + 3, 'n', 300);
	longest[303] = 0;
	custom_parse(m, longest, GAMENAME_CUSTOM_MAX);
	u32 len;
	CHECK(gamename_custom_get(&m->c, "/a", &len) != NULL);
	CHECK_EQ(len, GAMENAME_MAX);

	// The sort holds for many lines in any order
	char *big = malloc(GAMENAME_CUSTOM_MAX * 16);
	char *p = big;
	for (u32 i = 0; i < GAMENAME_CUSTOM_MAX; i++)
		p += sprintf(p, "/g%u\tN%u\n", (unsigned)((i * 7919u) % GAMENAME_CUSTOM_MAX),
			     (unsigned)((i * 7919u) % GAMENAME_CUSTOM_MAX));
	custom_parse(m, big, GAMENAME_CUSTOM_MAX);
	CHECK_EQ(m->c.count, GAMENAME_CUSTOM_MAX);
	CHECK(m->c.complete);
	for (u32 i = 0; i < GAMENAME_CUSTOM_MAX; i += 37) {
		char key[16], name[16];
		sprintf(key, "/g%u", (unsigned)i);
		sprintf(name, "N%u", (unsigned)i);
		check_get(m, key, name);
	}
	free(big);
	free(m->text);
	free(m);
}

void test_gamename_custom_set(void)
{
	struct custom *m = calloc(1, sizeof(*m));
	custom_parse(m, NULL, GAMENAME_CUSTOM_MAX);
	check_set(m, "/b.gba", "Bee", "/b.gba\tBee\n");
	check_set(m, "/b.gba", NULL, "");
	check_set(m, "b.gba", "Bee", NULL);  // not a path from the root
	check_set(m, "/b\t.gba", "Bee", NULL);
	char code[64];
	CHECK_EQ(gamename_custom_set(&m->c, "b.gba", "Bee", code, sizeof(code)), GAMENAME_BAD_PATH);
	CHECK_EQ(gamename_custom_set(&m->c, "/b\n", "Bee", code, sizeof(code)), GAMENAME_BAD_PATH);
	CHECK_EQ(gamename_custom_set(&m->c, "/b.gba", "Bee", code, 4), GAMENAME_TOO_BIG);

	// Written back sorted, one line per path, the junk gone
	custom_parse(m, "/c.gba\tSee\r\n# note\n/A.gba\tAy\n/a.gba\tAy two\n", GAMENAME_CUSTOM_MAX);
	check_set(m, "/b.gba", " Bee\tline\n ", "# note\n/a.gba\tAy two\n/b.gba\tBee line\n/c.gba\tSee\n");
	check_set(m, "/C.GBA", "Sea", "# note\n/a.gba\tAy two\n/C.GBA\tSea\n");
	check_set(m, "/a.gba", "", "# note\n/c.gba\tSee\n");
	check_set(m, "/a.gba", " \t ", "# note\n/c.gba\tSee\n");
	check_set(m, "/z.gba", "Zed", "# note\n/a.gba\tAy two\n/c.gba\tSee\n/z.gba\tZed\n");
	check_set(m, "/日本.gba", "名前", "# note\n/a.gba\tAy two\n/c.gba\tSee\n/日本.gba\t名前\n");

	// Out of room: refused, never cut
	char out[20];
	CHECK_EQ(gamename_custom_set(&m->c, "/b.gba", "Bee", out, sizeof(out)), GAMENAME_TOO_BIG);
	CHECK_EQ(gamename_custom_set(&m->c, "/a.gba", NULL, out, 17), GAMENAME_TOO_BIG);
	CHECK_EQ(gamename_custom_set(&m->c, "/a.gba", NULL, out, 18), 18);

	// A set file parses back to the same names
	char text[1024];
	u32 len = gamename_custom_set(&m->c, "/b.gba", "Bee", text, sizeof(text));
	text[len] = 0;
	custom_parse(m, text, GAMENAME_CUSTOM_MAX);
	CHECK_EQ(m->c.count, 3);
	check_get(m, "/b.gba", "Bee");
	check_get(m, "/a.gba", "Ay two");
	// Notes stay as they were, BOM, blank and empty-name lines go
	custom_parse(m, "\xEF\xBB\xBF# top\r\n\n  \nnotes here\n/x.gba\t \n/no tab\n/b\tB\n", GAMENAME_CUSTOM_MAX);
	check_set(m, "/a", "A", "# top\nnotes here\n/no tab\n/a\tA\n/b\tB\n");
	free(m->text);
	free(m);
}

// Moves old's name to new and checks the whole new file
static void check_move(struct custom *m, const char *old, const char *new_path, const char *expected)
{
	char out[1024];
	u32 len = gamename_custom_move(&m->c, old, new_path, out, sizeof(out));
	if (len != strlen(expected) || memcmp(out, expected, len) != 0) {
		tlog_error("FAIL %s: moving %s to %s gives \"%.*s\", expected \"%s\"", __FILE__, old, new_path,
			   GAMENAME_FAILED(len) ? 6 : (int)len, GAMENAME_FAILED(len) ? "failed" : out, expected);
		++torii_test_failures;
	}
}

void test_gamename_custom_move(void)
{
	struct custom *m = calloc(1, sizeof(*m));
	custom_parse(m, "# keep\n/a.gba\tAy\n/c.gba\tSee\n", GAMENAME_CUSTOM_MAX);
	check_move(m, "/a.gba", "/b.gba", "# keep\n/b.gba\tAy\n/c.gba\tSee\n");
	check_move(m, "/A.GBA", "/z/a.gba", "# keep\n/c.gba\tSee\n/z/a.gba\tAy\n");
	check_move(m, "/a.gba", "/c.gba", "# keep\n/c.gba\tAy\n");  // replaces the name there
	check_move(m, "/a.gba", "/A.gba", "# keep\n/A.gba\tAy\n/c.gba\tSee\n");  // only the case changes
	char out[64];
	CHECK_EQ(gamename_custom_move(&m->c, "/x.gba", "/y.gba", out, sizeof(out)), GAMENAME_UNCHANGED);
	CHECK_EQ(gamename_custom_move(&m->c, "/a.gba", "y.gba", out, sizeof(out)), GAMENAME_BAD_PATH);
	CHECK_EQ(gamename_custom_move(&m->c, "/a.gba", "/b.gba", out, 8), GAMENAME_TOO_BIG);
	free(m->text);
	free(m);
}

void test_gamename_display(void)
{
	struct custom *m = calloc(1, sizeof(*m));
	custom_parse(m, "/GBA/Zelda (USA).gba\tMy Zelda\n/GBA/Tab.gba\tA\tB\n", GAMENAME_CUSTOM_MAX);
	char *out = malloc(GAMENAME_MAX + 1);
	CHECK_EQ(gamename_display(out, "/GBA/Zelda (USA).gba", &m->c, "Legend of Zelda, The (USA)"), GAMENAME_CUSTOM);
	CHECK(strcmp(out, "My Zelda") == 0);
	CHECK_EQ(gamename_display(out, "/GBA/Tab.gba", &m->c, NULL), GAMENAME_CUSTOM);
	CHECK(strcmp(out, "A B") == 0);
	CHECK_EQ(gamename_display(out, "/GBA/Zelda (Europe).gba", &m->c, "Legend of Zelda, The (Europe)"),
		 GAMENAME_CRC);
	CHECK(strcmp(out, "The Legend of Zelda") == 0);
	CHECK_EQ(gamename_display(out, "/GBA/Zelda (Europe).gba", &m->c, ""), GAMENAME_CLEAN);
	CHECK(strcmp(out, "Zelda") == 0);
	CHECK_EQ(gamename_display(out, "/GBA/Zelda (USA).gba", NULL, NULL), GAMENAME_CLEAN);
	CHECK(strcmp(out, "Zelda") == 0);
	free(out);
	free(m->text);
	free(m);
}

void test_gamename_hidden(void)
{
	const u32 file = GAMENAME_OPENABLE, dir = GAMENAME_DIR;
	// The root's system folders, without case, and only in the root
	CHECK(gamename_hidden("/", "torii", dir, 0));
	CHECK(gamename_hidden("/", "SAVER", dir, 0));
	CHECK(gamename_hidden("/", "saver", dir, 0));
	CHECK(gamename_hidden("/", "RTS", dir, 0));
	CHECK(gamename_hidden("/", "SYSTEM", dir, 0));
	CHECK(gamename_hidden("/", "PATCH", dir, 0));
	CHECK(gamename_hidden("/", "CHEAT", dir, 0));
	CHECK(gamename_hidden("", "System Volume Information", dir, 0));
	CHECK(!gamename_hidden("/GBA", "SAVER", dir, 0));
	CHECK(!gamename_hidden("/", "GBA", dir, 0));
	CHECK(!gamename_hidden("/", "SAVERS", dir, 0));
	// Dot files and folders anywhere, and the hidden and system attributes
	CHECK(gamename_hidden("/GBA", "._Game.gba", file, 0));
	CHECK(gamename_hidden("/", ".Trashes", dir, 0));
	CHECK(gamename_hidden("/GBA", "Game.gba", file | GAMENAME_ATTR_HIDDEN, 0));
	CHECK(gamename_hidden("/", "Folder", dir | GAMENAME_ATTR_HIDDEN, 0));
	// Files the kernel can't start
	CHECK(gamename_hidden("/GBA", "Game.sav", 0, 0));
	CHECK(gamename_hidden("/", "readme.txt", 0, 0));
	CHECK(!gamename_hidden("/GBA", "Game.gba", file, 0));
	CHECK(!gamename_hidden("/", "Game.gba", file, 0));
	// Show all files shows everything
	CHECK(!gamename_hidden("/", "SAVER", dir, 1));
	CHECK(!gamename_hidden("/GBA", "._Game.gba", file, 1));
	CHECK(!gamename_hidden("/GBA", "Game.sav", GAMENAME_ATTR_HIDDEN, 1));
}
