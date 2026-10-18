// A temporary text demo in mode 0, in the test kernel only: with L held at
// boot, after the unit tests it draws sample text on BG0 (in place of the
// engine's scene) and logs "screen ready: text-demo". See it with
// mgba-shot ezkerneltest.gba -k 1:L -f 1600 -s 1600:PNG.
// T6 replaces it with the Diagnostics demo.
#ifdef TORII_TEST_KERNEL
#include <gba_video.h>

#include "../../source/engine/text.h"
#include "text_demo.h"
#include "torii_test.h"

// Palette indices
enum {
	BACK,
	TEXT,
	DIM,
	ACCENT,
	PANEL,
};

static void line(const struct text_window *win, const struct torii_font *font, u32 x, u32 y, u32 clip, u32 ink,
		 const char *s)
{
	u32 len = 0;
	while (s[len])
		len++;
	text_draw(win, font, x, y, clip, ink, s, len);
}

void text_demo(void)
{
	// The whole screen as one 30 x 20 tile window: tile tx * 20 + ty at
	// (tx, ty), char block 0, map in screen block 31
	struct text_window win = {(u32 *)CHAR_BASE_ADR(0), 30, 20};
	u16 *map = (u16 *)SCREEN_BASE_BLOCK(31);

	REG_DISPCNT = MODE_0 | LCDC_OFF;
	BG_PALETTE[BACK] = RGB5(3, 4, 6);
	BG_PALETTE[TEXT] = RGB5(29, 29, 30);
	BG_PALETTE[DIM] = RGB5(15, 16, 19);
	BG_PALETTE[ACCENT] = RGB5(31, 19, 8);
	BG_PALETTE[PANEL] = RGB5(8, 11, 19);
	for (u32 ty = 0; ty < 32; ty++)
		for (u32 tx = 0; tx < 32; tx++)
			map[ty * 32 + tx] = tx < 30 && ty < 20 ? tx * 20 + ty : 0;
	text_clear(&win, 0, 0, 240, 160, BACK);

	line(&win, &font_heading, 8, 4, 224, ACCENT, "Torii text \xE2\x80\x94 mode 0");
	line(&win, &font_body, 8, 24, 224, TEXT, "Pok\xC3\xA9mon \xE2\x80\x93 \xC3\x89meraude (France).gba");
	line(&win, &font_body, 8, 36, 224, TEXT,
	     "\xC3\x9Cn\xC3\xAF"
	     "c\xC3\xB6"
	     "d\xC3\xA9 \xE2\x80\x9Cquotes\xE2\x80\x9D \xC2\xA9 \xC2\xAE \xC2\xBD \xC2\xBC \xC2\xBE "
	     "\xC3\x9F\xC3\xB8\xC3\xA5");
	line(&win, &font_body, 8, 48, 224, DIM,
	     "\xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88 \xE3\x82\xB2\xE3\x83\xBC\xE3\x83\xA0.gba (no glyphs)");
	// A selected row: a panel, then text over it, which leaves it showing
	text_clear(&win, 4, 64, 232, 16, PANEL);
	line(&win, &font_body, 8, 66, 150, TEXT, "A long file name that does not fit its row.gba");
	line(&win, &font_body, 170, 66, 62, DIM, "16 MB");
	line(&win, &font_body, 8, 82, 150, TEXT, "Advance Wars 2 - Black Hole Rising.gba");
	line(&win, &font_body, 170, 82, 62, DIM, "8 MB");
	for (u32 i = 0; i < 8; i++)
		line(&win, &font_body, 9 + i * 29, 100 + (i & 1) * 2, 28, 1 + i % 4, "Odd x");
	line(&win, &font_heading, 3, 118, 236, TEXT, "ABCDEFGHIJKLMNOPQRSTU");
	line(&win, &font_body, 5, 136, 234, TEXT, "abcdefghijklmnopqrstuvwxyz 0123456789 !?&%");
	line(&win, &font_body, 5, 148, 234, DIM,
	     "\xC3\x80\xC3\x81\xC3\x82\xC3\x83\xC3\x84\xC3\x85\xC3\x86\xC3\x87\xC3\x88\xC3\x89 "
	     "\xC3\xA0\xC3\xA1\xC3\xA2\xC3\xA3\xC3\xA4\xC3\xA5\xC3\xA6\xC3\xA7\xC3\xA8\xC3\xA9 "
	     "\xC2\xA1\xC2\xBF\xC2\xB5");

	REG_BG0CNT = BG_16_COLOR | CHAR_BASE(0) | SCREEN_BASE(31);
	REG_DISPCNT = MODE_0 | BG0_ON;
	tlog_debug("screen ready: text-demo");
	for (;;)
		;
}
#endif
