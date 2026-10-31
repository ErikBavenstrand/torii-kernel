// Which game an in-game screenshot is (docs/design/v0.9/shot-viewer.md,
// "Which shots are a game's"): shotstore.c files one as
//
//	<stem> YYYY-MM-DD HH-MM-SS.bmp   then -2, -3 ... within one second
//	<stem>-N.bmp                     N from 2, with no clock
//
// where the stem is the game's (shotname_stem). Hardware-free: the host
// tests check it.
#ifndef TORII_SHOTNAME_H
#define TORII_SHOTNAME_H

#include <gba_base.h>

#define SHOTNAME_STEM_BYTES 48  // INGAME_NAME_BYTES: a stem's bytes with its NUL

// When a shot was taken, from its name
struct shotname_when {
	u16 year;  // 0: no clock
	u8 month, day, hour, min, sec;
	u16 n;  // its number within the second, or with no clock: 1, 2 ...
};

// The stem of the game at path: its file name without the folder and the
// extension, cut to SHOTNAME_STEM_BYTES - 1 bytes on a whole UTF-8
// character (the in-game session's name, which names its shots)
void shotname_stem(char *out, u32 size, const char *path);

// 1 if the len bytes at rest are exactly a shot's suffix after its stem
// (no ".bmp"); when filled in
int shotname_suffix(const char *rest, u32 len, struct shotname_when *when);

// A file name of a shot: the length of its stem (its suffix and .bmp,
// any case, after it), or 0 if the name isn't a shot's
u32 shotname_split(const char *name, struct shotname_when *when);

// 1 if name ends in .bmp, any case: a picture the viewer opens
int shotname_is_bmp(const char *name);

// 1 if name is a shot of the game with that stem
int shotname_match(const char *name, const char *stem, struct shotname_when *when);

// Orders shots by when they were taken: below 0 if a is older than b, 0
// if alike, above 0 if newer. A shot with no clock is older than any with one.
int shotname_cmp(const struct shotname_when *a, const struct shotname_when *b);

#endif
