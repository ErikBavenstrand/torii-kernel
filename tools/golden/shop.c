// Pokémon Emerald's all-in-one shop: in any shop's Buy list, L and R go
// through more shelves, the shop's own first. A Poké Mart's go through the
// item categories, a decoration shop's to every decoration. Nothing else
// changes: prices, the bag, the money and the purchases are the game's.
//
// shop_task stands in for Task_BuyMenu, the Buy list's task (its two
// literals point here). On L or R, with no fade under way, it points the
// shop's item list at the next shelf, then leaves the list the way the
// game's own exit does (Task_ExitBuyMenu: the money label, the list's
// memory, a fade to black), except that it starts the Buy list again
// (CB2_InitBuyMenu) instead of going back to the field. The list is only
// ever built by the game, from a list it never reads past.
//
// Built by tools/golden/build.sh at 0x08F00000 (ROM free from 0x08E3CF64);
// emerald.py appends the shelves' table at SHOP_TABLE. Addresses are
// pokeemerald.sym's for this ROM (BPEE, CRC32 1F1C08FB).
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define L_BUTTON 0x200
#define R_BUTTON 0x100
#define SE_SELECT 5
#define MART_TYPE_NORMAL 0

struct MartInfo {
	void (*callback)(void);
	const void *menuActions;
	const u16 *itemList;
	u16 itemCount;
	u8 windowId;
	u8 martType;
};

// The shelves of each kind of shop: kinds[martType != NORMAL]
struct shelves {
	u32 count;
	const u16 *lists[15];
};
struct table {
	struct shelves kinds[2];
};

#define SHOP_TABLE ((const struct table *)0x08F00400)
#define sMartInfo (*(struct MartInfo *)0x02039F60)
#define NEW_KEYS (*(volatile u16 *)(0x030022C0 + 0x2E))       // gMain.newKeys
#define FADE_ACTIVE (*(volatile u8 *)(0x02037FD4 + 7) & 0x80)  // gPaletteFade.active
#define TASK_FUNC(id) (*(void (**)(u8))(0x03005E00 + (u32)(id) * 0x28))
// The shop's own list while another shelf shows: free EWRAM, past the
// game's last variable (0x0203CF64) and clear of Torii's at 0x0203FE00
#define OWN (*(const u16 **)0x0203FF18)

#define Task_BuyMenu ((void (*)(u8))0x080E0AC9)
#define RemoveMoneyLabelObject ((void (*)(void))0x080E53F5)
#define BuyMenuFreeMemory ((void (*)(void))0x080DFEFD)
#define CB2_InitBuyMenu ((void (*)(void))0x080DFD95)
#define SetMainCallback2 ((void (*)(void (*)(void)))0x08000541)
#define DestroyTask ((void (*)(u8))0x080A909D)
#define PlaySE ((void (*)(u16))0x080A37A5)
#define BeginNormalPaletteFade ((u8 (*)(u32, int, u8, u8, u16))0x080A1AD5)

// Once the fade is done: out as Task_ExitBuyMenu goes, into the list again
static void shop_switch(u8 taskId)
{
	if (FADE_ACTIVE)
		return;
	RemoveMoneyLabelObject();
	BuyMenuFreeMemory();
	SetMainCallback2(CB2_InitBuyMenu);
	DestroyTask(taskId);
}

// A list the game could have made: in ROM, halfword-aligned
static int rom_list(const u16 *list)
{
	u32 a = (u32)list;
	return a >= 0x08000000 && a < 0x0A000000 && !(a & 1);
}

__attribute__((section(".text.entry"))) void shop_task(u8 taskId)
{
	u16 keys = NEW_KEYS & (L_BUTTON | R_BUTTON);
	const struct shelves *s = &SHOP_TABLE->kinds[sMartInfo.martType != MART_TYPE_NORMAL];
	if (!keys || FADE_ACTIVE || !s->count) {
		Task_BuyMenu(taskId);
		return;
	}
	// Which shelf shows: one of the table's, else the shop's own (n)
	u32 n = s->count, at = n;
	for (u32 i = 0; i < n; i++)
		if (s->lists[i] == sMartInfo.itemList)
			at = i;
	if (at == n)
		OWN = sMartInfo.itemList;
	int own = rom_list(OWN);
	// R on, L back: own, 0 .. n - 1, round (the own one skipped if it isn't known)
	if (keys & R_BUTTON)
		at = at == n ? 0 : at + 1 == n && !own ? 0 : at + 1;
	else
		at = at == n ? n - 1 : at == 0 ? (own ? n : n - 1) : at - 1;
	const u16 *list = at == n ? OWN : s->lists[at];
	u16 count = 0;
	while (list[count])
		count++;
	sMartInfo.itemList = list;
	sMartInfo.itemCount = count;
	PlaySE(SE_SELECT);
	BeginNormalPaletteFade(0xFFFFFFFF, 0, 0, 16, 0);
	TASK_FUNC(taskId) = shop_switch;
}
