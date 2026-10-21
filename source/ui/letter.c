// The letters an A-Z list jumps by (letter.h). Hardware-free.
#include "letter.h"
#include "../library.h"

u32 letter_of(const char *display)
{
	char key[4];
	u32 len = library_sort_key(key, sizeof(key), display);
	if (!len)
		return LETTER_HASH_LOW;
	u8 c = (u8)key[0];
	if (c >= 'a' && c <= 'z')
		return LETTER_A + (u32)(c - 'a');
	return c < 'a' ? LETTER_HASH_LOW : LETTER_HASH_HIGH;
}

const char *letter_text(u32 letter)
{
	static const char letters[] = "#\0A\0B\0C\0D\0E\0F\0G\0H\0I\0J\0K\0L\0M\0N\0O\0P\0Q\0R\0S\0T\0U\0V\0W\0X\0Y\0Z";
	if (letter >= LETTER_A && letter <= LETTER_Z)
		return &letters[2 * letter];
	return letter == LETTER_HASH_LOW || letter == LETTER_HASH_HIGH ? letters : "";
}

// The first item whose letter is letter or later, in [0, count]
static u32 first_from(u32 count, u32 letter, letter_at_fn at, void *ctx)
{
	u32 lo = 0, hi = count;
	while (lo < hi) {
		u32 mid = lo + (hi - lo) / 2;
		if (at(ctx, mid) < letter)
			lo = mid + 1;
		else
			hi = mid;
	}
	return lo;
}

static int is_hash(u32 letter)
{
	return letter == LETTER_HASH_LOW || letter == LETTER_HASH_HIGH;
}

// 1 when the list has both "#" runs, the one before A and the one after Z:
// across the lap's wrap they are one stop, at the first run's start (0)
static int hash_merged(u32 count, letter_at_fn at, void *ctx)
{
	return at(ctx, 0) == LETTER_HASH_LOW && at(ctx, count - 1) == LETTER_HASH_HIGH;
}

u32 letter_jump(u32 count, u32 cursor, s32 dir, letter_at_fn at, void *ctx)
{
	if (!count)
		return cursor;
	if (cursor >= count)
		cursor = count - 1;
	u32 letter = at(ctx, cursor);
	int merged = hash_merged(count, at, ctx);
	if (merged && is_hash(letter)) {
		// From the "#" stop: RIGHT to A's run, LEFT to the run before the
		// second "#" run; the stop itself when the list has nothing else
		if (dir > 0) {
			u32 next = first_from(count, LETTER_A, at, ctx);
			return at(ctx, next) != LETTER_HASH_HIGH ? next : 0;
		}
		u32 high = first_from(count, LETTER_HASH_HIGH, at, ctx);
		u32 before = at(ctx, high - 1);
		return before != LETTER_HASH_LOW ? first_from(count, before, at, ctx) : 0;
	}
	if (dir > 0) {
		u32 next = first_from(count, letter + 1, at, ctx);
		if (next >= count || (merged && at(ctx, next) == LETTER_HASH_HIGH))
			return 0;
		return next;
	}
	u32 start = first_from(count, letter, at, ctx);
	u32 before = start ? start - 1 : count - 1;
	return first_from(count, at(ctx, before), at, ctx);
}

void letter_around(u32 count, u32 index, letter_at_fn at, void *ctx, u32 *prev, u32 *next)
{
	*prev = *next = LETTER_NONE;
	if (!count)
		return;
	if (index >= count)
		index = count - 1;
	u32 letter = at(ctx, index);
	u32 to_next = at(ctx, letter_jump(count, index, 1, at, ctx));
	u32 to_prev = at(ctx, letter_jump(count, index, -1, at, ctx));
	// One stop only: RIGHT comes back to it
	if (to_next == letter || (is_hash(to_next) && is_hash(letter) && hash_merged(count, at, ctx)))
		return;
	*next = to_next;
	*prev = to_prev;
}
