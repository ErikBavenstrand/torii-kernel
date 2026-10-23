// The rename keyboard (ticket B18; docs/design/v0.4/keyboard-light.png and
// mockups-b16-b18.md): a page in the menu's session that takes the place of
// the page that opened it (Rename in a game popup or an SD file popup), in
// that page's accent.
//
// The name sits in a field card with its caret and a counter ("14/60"); an
// SD file's locked extension follows it in faint ink. Under it a 10-column
// QWERTY grid with digits on top, a Latin-1 page, and a function row:
// Shift, àé&, Space, delete, Cancel and OK.
//
// Keys: the d-pad moves the selection (a press wraps at the edges, a held
// key repeats and stops there), A types or runs the key, B deletes before
// the caret, L and R move the caret, SELECT is Shift, START is OK. Cancel
// asks first when the name changed.
#ifndef TORII_SCREENS_RENAME_H
#define TORII_SCREENS_RENAME_H

#include <gba_base.h>

#define RENAME_GAME_MAX 60   // characters of a game's display name (names.txt)
#define RENAME_STEM_MAX 100  // and of an SD file's name before its extension

struct rename_ask {
	u16 icon16;       // the opening page's 16x16 icon (ART_ICON16_*), for the title bar
	const char *name;  // the name to start from (UTF-8)
	const char *ext;   // a locked extension shown after it (".gba"), or NULL
	u8 max;            // characters it may have (RENAME_*_MAX)
	u8 file;           // a file name: OK drops \ / : * ? " < > | too
	// OK's name (trimmed, never empty, never the one it started from):
	// NULL once it is kept (the keyboard closes), or why not, a line the
	// hint area shows (the keyboard stays). Runs after a frame, where it may
	// read and write the card.
	const char *(*submit)(void *ctx, const char *name);
	void *ctx;
	const char *title;  // the title bar; NULL: "Rename"
};

// Shows the keyboard until OK keeps a name (1) or it is cancelled, or the
// name didn't change (0). Call it from a page's loop (a deferred function),
// with its popup closed: the page goes and the keyboard comes in its place
// in the menu's session, and when it returns nothing shows (the page pushes
// itself again).
int rename_run(const struct rename_ask *ask);

#endif
