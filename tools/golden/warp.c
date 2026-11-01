// Pokémon Emerald's menu Warp: the warp of the place chosen, as the game's
// script command warp does (ScrCmd_warp), from the list's value (menu.c's
// LIST_VALUES): VAR_0x8004 the map's group and number, VAR_0x8005 its x and
// y, or x 0xFF for the map's first warp. Called by the Warp list's script
//     callnative WARP+1
//     waitstate
// Position-independent: emerald.py places it in the Warp section's blob.
// Built by tools/golden/build.sh.
typedef unsigned char u8;
typedef unsigned short u16;

#define VAR_8004 (*(volatile u16 *)0x020375E0)  // gSpecialVar_0x8004
#define VAR_8005 (*(volatile u16 *)0x020375E2)
#define SetWarpDestination ((void (*)(int, int, int, int, int))0x08084BED)
#define DoWarp ((void (*)(void))0x080AF735)
#define ResetInitialPlayerAvatarState ((void (*)(void))0x08085269)

__attribute__((section(".text.entry"))) int warp_to(void)
{
	u16 map = VAR_8004, at = VAR_8005;
	u8 x = (u8)at, y = (u8)(at >> 8);
	if (x == 0xFF)
		SetWarpDestination(map & 0xFF, map >> 8, 0, -1, -1);
	else
		SetWarpDestination(map & 0xFF, map >> 8, -1, x, y);
	DoWarp();
	ResetInitialPlayerAvatarState();
	return 0;
}
