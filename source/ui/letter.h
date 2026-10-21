// The letters an A-Z list jumps by (LEFT/RIGHT and the letter card, ui.h).
// Hardware-free, for tests/host.
//
// A row files under the first letter of the name it shows, as the lists
// sort (library_sort_key: accents folded, case ignored): LETTER_A to
// LETTER_Z, or "#" for the rest, which sorts in two runs, digits and
// symbols before A (LETTER_HASH_LOW) and other scripts after Z
// (LETTER_HASH_HIGH). In list order the letters never go down, so the jumps
// are binary searches: about 12 lookups of a 4,096-game list.
#ifndef TORII_UI_LETTER_H
#define TORII_UI_LETTER_H

#include <gba_base.h>

#define LETTER_HASH_LOW 0
#define LETTER_A 1
#define LETTER_Z 26
#define LETTER_HASH_HIGH 27
#define LETTER_NONE 0xFF

// The letter display files under.
u32 letter_of(const char *display);
// The card's text for a letter: "A" to "Z", "#"; "" for LETTER_NONE.
const char *letter_text(u32 letter);

// The letter of the list's item index (letter_of its shown name)
typedef u32 (*letter_at_fn)(void *ctx, u32 index);

// Where LEFT (dir -1) or RIGHT (1) jumps from cursor in a list of count
// items: the first item of the previous or next letter that has items,
// wrapping from the last letter to the first and back. cursor if count is 0.
u32 letter_jump(u32 count, u32 cursor, s32 dir, letter_at_fn at, void *ctx);
// The letters LEFT and RIGHT would reach from item index, for the card
// (LETTER_NONE when the list has only index's letter).
void letter_around(u32 count, u32 index, letter_at_fn at, void *ctx, u32 *prev, u32 *next);

#endif
