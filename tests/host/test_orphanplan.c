// The global save manager's hardware-free parts (source/orphanplan.c): which
// file belongs to which slot, the names a relinked slot takes, the journal
// and the order of the relink picker's rows.
#include <stdlib.h>
#include <string.h>

#include "../../source/orphanplan.h"
#include "../kernel/torii_test.h"
#include "tests.h"

static void slot_of(const char *name, const char *slot, const char *rest)
{
	const char *r = NULL;
	u32 n = orphanplan_slot_of(name, &r);
	CHECK_EQ(n, strlen(slot));
	if (n == strlen(slot)) {
		CHECK(strncmp(name, slot, n) == 0);
		CHECK(strcmp(r, rest) == 0);
	}
}

void test_orphanplan_slot_of(void)
{
	slot_of("Main.sav", "Main", ".sav");
	slot_of("Boss fight.SAV", "Boss fight", ".SAV");
	slot_of("Main.sav.tmp", "Main", ".sav.tmp");
	slot_of("Main.sav.BAK", "Main", ".sav.BAK");
	slot_of("Main.sav.new", "Main", ".sav.new");
	slot_of("Main.sav.ren", "Main", ".sav.ren");
	slot_of("a.sav.sav", "a.sav", ".sav");
	slot_of("Caf\xC3\xA9.sav", "Caf\xC3\xA9", ".sav");
	CHECK_EQ(orphanplan_slot_of("game.ini", NULL), 0);
	CHECK_EQ(orphanplan_slot_of(".sav", NULL), 0);
	CHECK_EQ(orphanplan_slot_of("Main.sav.old", NULL), 0);
	CHECK_EQ(orphanplan_slot_of("Main.save", NULL), 0);
	CHECK_EQ(orphanplan_slot_of("", NULL), 0);
}

struct taken {
	const char *names[8];
	u32 n;
};

static int is_taken(void *ctx, const char *name)
{
	struct taken *t = ctx;
	for (u32 i = 0; i < t->n; i++)
		if (saveslot_names_equal(t->names[i], name))
			return 1;
	return 0;
}

void test_orphanplan_pick_name(void)
{
	char out[SAVESLOT_NAME_BYTES];
	struct taken t = {{"Main"}, 1};
	// Free: unchanged
	CHECK(orphanplan_pick_name(out, "Speedrun", is_taken, &t));
	CHECK(strcmp(out, "Speedrun") == 0);
	// Taken (ignoring case): the first free number from 2
	CHECK(orphanplan_pick_name(out, "main", is_taken, &t));
	CHECK(strcmp(out, "main 2") == 0);
	t.names[t.n++] = "Main 2";
	t.names[t.n++] = "MAIN 3";
	CHECK(orphanplan_pick_name(out, "Main", is_taken, &t));
	CHECK(strcmp(out, "Main 4") == 0);
	// A long name is cut to make room for the number and stays 24 characters
	t.n = 0;
	t.names[t.n++] = "abcdefghijklmnopqrstuvwx";
	CHECK(orphanplan_pick_name(out, "abcdefghijklmnopqrstuvwx", is_taken, &t));
	CHECK(strcmp(out, "abcdefghijklmnopqrstuv 2") == 0);
	CHECK_EQ(strlen(out), 24);
	char again[SAVESLOT_NAME_BYTES];
	saveslot_clean_name(again, out);
	CHECK(strcmp(again, out) == 0);
	// Whole characters: Japanese ones are three bytes each
	char jp[SAVESLOT_NAME_BYTES] = "";
	for (u32 i = 0; i < 24; i++)
		strcat(jp, "\xE3\x81\x82");
	t.names[0] = jp;
	CHECK(orphanplan_pick_name(out, jp, is_taken, &t));
	CHECK_EQ(strlen(out), 22 * 3 + 2);
	CHECK(strcmp(out + 22 * 3, " 2") == 0);
	// A cut that leaves a space at the end drops it
	t.names[0] = "abcdefghijklmnopqrstuv  x";
	CHECK(orphanplan_pick_name(out, "abcdefghijklmnopqrstuv  x", is_taken, &t));
	CHECK(strcmp(out, "abcdefghijklmnopqrstuv 2") == 0);
}

static int always(void *ctx, const char *name)
{
	(void)ctx;
	(void)name;
	return 1;
}

void test_orphanplan_pick_name_full(void)
{
	char out[SAVESLOT_NAME_BYTES];
	CHECK(!orphanplan_pick_name(out, "Main", always, NULL));
	CHECK(out[0] == 0);
}

void test_orphanplan_journal(void)
{
	char buf[1024];
	const char *x[] = {"Main", "Boss fight"};
	const char *y[] = {"Main 2", "Boss fight"};
	u32 len = orphanplan_write(buf, sizeof(buf), ORPHANPLAN_RELINK, "/GBA/Old.gba", "/GBA/New.gba", x, y, 2);
	CHECK(len > 0);
	char *copy = malloc(len + 1);
	memcpy(copy, buf, len);
	copy[len] = 0;
	struct orphanplan_job job;
	CHECK(orphanplan_read(copy, len, &job));
	CHECK_EQ(job.kind, ORPHANPLAN_RELINK);
	CHECK(strcmp(job.from, "/GBA/Old.gba") == 0);
	CHECK(strcmp(job.to, "/GBA/New.gba") == 0);
	CHECK_EQ(job.n, 2);
	CHECK(strcmp(job.x[0], "Main") == 0 && strcmp(job.y[0], "Main 2") == 0);
	CHECK(strcmp(job.x[1], "Boss fight") == 0 && strcmp(job.y[1], "Boss fight") == 0);
	free(copy);

	// A trash job has no slots
	len = orphanplan_write(buf, sizeof(buf), ORPHANPLAN_TRASH, "/GBA/Old.gba", "/torii/trash/20260801-000000-1", NULL, NULL,
			       0);
	CHECK(len > 0);
	copy = malloc(len + 1);
	memcpy(copy, buf, len);
	copy[len] = 0;
	CHECK(orphanplan_read(copy, len, &job));
	CHECK_EQ(job.kind, ORPHANPLAN_TRASH);
	CHECK_EQ(job.n, 0);
	free(copy);

	// Cut anywhere, or one byte changed, is no journal
	len = orphanplan_write(buf, sizeof(buf), ORPHANPLAN_RELINK, "/a.gba", "/b.gba", x, y, 2);
	for (u32 cut = 0; cut < len; cut++) {
		copy = malloc(cut + 1);
		memcpy(copy, buf, cut);
		copy[cut] = 0;
		CHECK(!orphanplan_read(copy, cut, &job));
		free(copy);
	}
	for (u32 i = 0; i < len; i++) {
		copy = malloc(len + 1);
		memcpy(copy, buf, len);
		copy[len] = 0;
		copy[i] ^= 0x01;
		CHECK(!orphanplan_read(copy, len, &job));
		free(copy);
	}
	// Bad input
	CHECK_EQ(orphanplan_write(buf, sizeof(buf), 'X', "/a", "/b", NULL, NULL, 0), 0);
	CHECK_EQ(orphanplan_write(buf, sizeof(buf), ORPHANPLAN_TRASH, "", "/b", NULL, NULL, 0), 0);
	CHECK_EQ(orphanplan_write(buf, sizeof(buf), ORPHANPLAN_TRASH, "/a\n", "/b", NULL, NULL, 0), 0);
	CHECK_EQ(orphanplan_write(buf, 40, ORPHANPLAN_RELINK, "/a.gba", "/b.gba", x, y, 2), 0);
	const char *bad[] = {"a\tb"};
	CHECK_EQ(orphanplan_write(buf, sizeof(buf), ORPHANPLAN_RELINK, "/a", "/b", bad, bad, 1), 0);
}

// Every row is one position, once
static void check_pick(u32 total, u32 cs, u32 ce, const u32 *names, u32 nn)
{
	struct orphanpick p;
	orphanpick_begin(&p, total, cs, ce);
	for (u32 i = 0; i < nn; i++)
		orphanpick_add(&p, names[i]);
	u8 *seen = calloc(total ? total : 1, 1);
	u32 first_group[3] = {0, 0, 0};
	u32 last_group = 0;
	for (u32 r = 0; r < total; r++) {
		u32 pos = orphanpick_pos(&p, r);
		CHECK(pos < total);
		if (pos >= total)
			continue;
		CHECK(!seen[pos]);
		seen[pos] = 1;
		u32 g = orphanpick_group(&p, r);
		CHECK(g >= last_group);  // the groups come in order
		last_group = g;
		first_group[g]++;
		if (g == 1)
			CHECK(pos >= p.cs && pos < p.ce);
		if (g == 2)
			CHECK(pos < p.cs || pos >= p.ce);
	}
	for (u32 i = 0; i < total; i++)
		CHECK(seen[i]);
	CHECK_EQ(first_group[0], p.nn);
	free(seen);
}

void test_orphanplan_picker(void)
{
	u32 names[] = {7, 2, 30, 31};
	check_pick(40, 5, 12, names, 4);   // names inside the console and outside
	check_pick(40, 0, 40, names, 4);   // one console
	check_pick(40, 0, 0, names, 4);    // no console
	check_pick(40, 35, 40, names, 4);  // the last console
	check_pick(1, 0, 1, NULL, 0);
	check_pick(0, 0, 0, NULL, 0);
	u32 all[] = {0, 1, 2};
	check_pick(3, 0, 3, all, 3);
	u32 dup[] = {4, 4, 9, 100};  // a repeat and a position past the end are ignored
	check_pick(10, 3, 6, dup, 4);
	// The order of the first group is the order given
	struct orphanpick p;
	orphanpick_begin(&p, 40, 5, 12);
	orphanpick_add(&p, 30);
	orphanpick_add(&p, 2);
	orphanpick_add(&p, 7);
	CHECK_EQ(orphanpick_pos(&p, 0), 30);
	CHECK_EQ(orphanpick_pos(&p, 1), 2);
	CHECK_EQ(orphanpick_pos(&p, 2), 7);
	// then the console's run in order, without 7
	CHECK_EQ(orphanpick_pos(&p, 3), 5);
	CHECK_EQ(orphanpick_pos(&p, 4), 6);
	CHECK_EQ(orphanpick_pos(&p, 5), 8);
	// then the others from the start, without 2
	CHECK_EQ(orphanpick_pos(&p, 3 + 6), 0);
	CHECK_EQ(orphanpick_pos(&p, 3 + 7), 1);
	CHECK_EQ(orphanpick_pos(&p, 3 + 8), 3);
	// A full table keeps its first 16
	orphanpick_begin(&p, 100, 0, 100);
	for (u32 i = 0; i < 40; i++)
		orphanpick_add(&p, i);
	CHECK_EQ(p.nn, ORPHANPICK_NAMES);
	check_pick(100, 20, 60, (u32[]){3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15, 16, 17, 18, 19, 20, 21, 22}, 20);
}
