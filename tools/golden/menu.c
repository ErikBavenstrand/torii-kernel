// Pokémon Emerald's L+R+B menu: one routine shows every list in it, as the
// game's own scrolling list (the Battle Frontier's): up to 7 rows with
// arrows when more follow, LEFT and RIGHT a page at a time, A to choose,
// B to go back. Each list remembers where its cursor was.
//
// A script shows a list with
//     callnative 0x08F00001
//     .word LIST
//     waitstate
// The routine reads LIST from the script and steps past it. A choice sets
// the script going again at the row's script, B at the list's back script.
// A row whose script is unset (0xFFFFFFFF, the ROM's free space) is left
// out: the main list's rows are slots that each section's cheat fills, so
// it lists only the sections picked. With no row left, the script goes on
// at the list's empty script.
//
// A list of values (LIST_VALUES) has a value in each row in place of its
// script: a choice puts its halves in VAR_0x8004 and VAR_0x8005 and sets
// the script going at the list's empty script, one for every row (Warp's:
// 106 places cost a row each, not a script each).
//
// Built by tools/golden/build.sh at 0x08F00000. Its variables are in EWRAM
// past the game's last (0x0203CF64), below what Torii and plugins use
// (0x0203FB00 on). Addresses are pokeemerald.sym's for this ROM (BPEE,
// CRC32 1F1C08FB).
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;
typedef int s32;

struct row {
	const u8 *name;
	const u8 *script;
};

// LIST in ROM, built by emerald.py's Menu
struct list {
	const u8 *back;   // on B
	const u8 *empty;  // with no row set
	u8 count;         // rows
	u8 memory;        // which cursor it keeps, below MEMORIES
	u8 flags;         // LIST_*
	u8 pad;
	struct row rows[];
};

struct list_item {
	const u8 *name;
	s32 id;
};

struct list_template {
	const struct list_item *items;
	void (*move_cursor)(s32 index, u8 on_init, void *list);
	void (*print_item)(u8 window, u32 id, u8 y);
	u16 total, shown;
	u8 window, header_x, item_x, cursor_x;
	u8 up_text_y_cursor_pal, fill_shadow_pal;
	u8 spacing_padding_scroll, font_cursor;
};

struct window_template {
	u8 bg, left, top, width, height, palette;
	u16 base_block;
};

struct arrows_template {
	u8 first_type, first_x, first_y, second_type, second_x, second_y;
	u16 fully_up, fully_down, tile_tag, pal_tag;
	u8 pal_num;
};

struct context {
	u8 depth, mode, comparison, pad;
	void *native;
	const u8 *script;
};

#define LIST_VALUES 1  // rows hold values; empty is the script every row runs

#define ROWS_MAX 48
#define MEMORIES 32
#define SHOWN 7
#define LEFT 1
#define TOP 1

struct menu {
	struct list_template template;
	struct list_item items[ROWS_MAX];
	u16 scroll;
	u8 window, list_task, arrows;
	const struct list *list;
	struct {
		u16 scroll, row;
	} memory[MEMORIES];
};

#define M (*(struct menu *)0x0203F000)
#define CONTEXT (*(struct context *)0x03000E40)  // sGlobalScriptContext
#define VAR_8004 (*(volatile u16 *)0x020375E0)  // gSpecialVar_0x8004
#define VAR_8005 (*(volatile u16 *)0x020375E2)
#define PALETTE_FADE_ACTIVE (*(volatile u8 *)(0x02037FD4 + 7) & 0x80)  // gPaletteFade.active

#define LIST_NOTHING_CHOSEN -1
#define LIST_CANCEL -2
#define SE_SELECT 5
#define SCROLL_DPAD 1
#define ARROW_UP 2  // SCROLL_ARROW_UP
#define ARROW_DOWN 3

#define ScriptContext_Enable ((void (*)(void))0x08098F3D)
#define ListMenuInit ((u8 (*)(const struct list_template *, u16, u16))0x081AE585)
#define ListMenu_ProcessInput ((s32 (*)(u8))0x081AE605)
#define DestroyListMenuTask ((void (*)(u8, u16 *, u16 *))0x081AE6C9)
#define ListMenuGetScrollAndRow ((void (*)(u8, u16 *, u16 *))0x081AE861)
#define AddWindow ((u8 (*)(const struct window_template *))0x08003381)
#define RemoveWindow ((void (*)(u8))0x08003575)
#define SetStandardWindowBorderStyle ((void (*)(u8, u8))0x08197855)
#define ClearStdWindowAndFrameToTransparent ((void (*)(u8, u8))0x08198071)
#define FillWindowPixelBuffer ((void (*)(u8, u8))0x08003C49)
#define CopyWindowToVram ((void (*)(u8, u8))0x08003659)
#define ScheduleBgCopyTilemapToVram ((void (*)(u8))0x081999BD)
#define AddScrollIndicatorArrowPair ((u8 (*)(const struct arrows_template *, u16 *))0x081AF381)
#define RemoveScrollIndicatorArrowPair ((void (*)(u8))0x081AF621)
#define CreateTask ((u8 (*)(void (*)(u8), u8))0x080A8FB1)
#define DestroyTask ((void (*)(u8))0x080A909D)
#define DisplayTextAndGetWidth ((s32 (*)(const u8 *, s32))0x080E2D2D)
#define ConvertPixelWidthToTileWidth ((u8 (*)(s32))0x080E2D41)
#define PlaySE ((void (*)(u16))0x080A37A5)

static int set(const u8 *script)
{
	u32 a = (u32)script;
	return a >= 0x08000000 && a < 0x0A000000;
}

static void move_cursor(s32 index, u8 on_init, void *list)
{
	(void)index;
	M.scroll = *(const u16 *)((const u8 *)list + 0x18);  // struct ListMenu's scrollOffset
	if (!on_init)
		PlaySE(SE_SELECT);
}

static void go(const u8 *script)
{
	CONTEXT.script = script;
	ScriptContext_Enable();
}

static void task(u8 id)
{
	if (PALETTE_FADE_ACTIVE)
		return;
	s32 input = ListMenu_ProcessInput(M.list_task);
	if (input == LIST_NOTHING_CHOSEN)
		return;
	const struct list *l = M.list;
	u16 scroll, row;
	ListMenuGetScrollAndRow(M.list_task, &scroll, &row);
	M.memory[l->memory].scroll = scroll;
	M.memory[l->memory].row = row;
	PlaySE(SE_SELECT);
	if (M.arrows != 0xFF)
		RemoveScrollIndicatorArrowPair(M.arrows);
	DestroyListMenuTask(M.list_task, 0, 0);
	ClearStdWindowAndFrameToTransparent(M.window, 1);
	FillWindowPixelBuffer(M.window, 0);
	CopyWindowToVram(M.window, 2);  // COPYWIN_GFX
	RemoveWindow(M.window);
	DestroyTask(id);
	if (input != LIST_CANCEL && (l->flags & LIST_VALUES)) {
		VAR_8004 = (u16)input;
		VAR_8005 = (u16)((u32)input >> 16);
		go(l->empty);
		return;
	}
	go(input == LIST_CANCEL ? l->back : (const u8 *)input);
}

__attribute__((section(".text.entry"))) int show_list(void)
{
	const u8 *p = CONTEXT.script;
	const struct list *l = (const struct list *)(p[0] | p[1] << 8 | p[2] << 16 | (u32)p[3] << 24);
	CONTEXT.script = p + 4;

	u32 n = 0;
	s32 width = 0;
	for (u32 i = 0; i < l->count && n < ROWS_MAX; i++) {
		if (!(l->flags & LIST_VALUES) && !set(l->rows[i].script))
			continue;
		M.items[n].name = l->rows[i].name;
		M.items[n].id = (s32)l->rows[i].script;
		width = DisplayTextAndGetWidth(l->rows[i].name, width);
		n++;
	}
	if (!n) {
		// No waitstate: the script goes straight on at the empty script
		CONTEXT.script = l->empty;
		return 0;
	}
	u32 shown = n < SHOWN ? n : SHOWN;
	u16 scroll = M.memory[l->memory].scroll, row = M.memory[l->memory].row;
	if (row >= shown || scroll > n - shown)
		scroll = row = 0;

	M.list = l;
	M.scroll = scroll;
	u8 w = ConvertPixelWidthToTileWidth(width);
	const struct window_template wt = {0, LEFT, TOP, w, (u8)(shown * 2), 15, 0x64};
	M.window = AddWindow(&wt);
	SetStandardWindowBorderStyle(M.window, 0);

	struct list_template *t = &M.template;
	t->items = M.items;
	t->move_cursor = move_cursor;
	t->print_item = 0;
	t->total = (u16)n;
	t->shown = (u16)shown;
	t->window = M.window;
	t->header_x = 0;
	t->item_x = 8;
	t->cursor_x = 0;
	t->up_text_y_cursor_pal = 1 | 2 << 4;
	t->fill_shadow_pal = 1 | 3 << 4;
	t->spacing_padding_scroll = SCROLL_DPAD << 6;
	t->font_cursor = 1;  // FONT_NORMAL, CURSOR_BLACK_ARROW

	M.arrows = 0xFF;
	if (n > shown) {
		u8 x = (u8)((w / 2) * 8 + 12 + (LEFT - 1) * 8);
		const struct arrows_template at = {
			ARROW_UP, x, 8, ARROW_DOWN, x, (u8)(shown * 2 * 8 + 10), 0, (u16)(n - shown), 2000, 100, 0};
		M.arrows = AddScrollIndicatorArrowPair(&at, &M.scroll);
	}
	M.list_task = ListMenuInit(t, scroll, row);
	ScheduleBgCopyTilemapToVram(0);
	CreateTask(task, 80);
	return 0;
}
