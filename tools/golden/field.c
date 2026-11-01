// Pokémon Emerald's Torii menu, opened with L+R+A where the game opens its
// own: DoCB1_Overworld's call to ProcessPlayerFieldInput comes here. The
// game's input goes first (a trainer's sight, map scripts, warps, talking
// to what is in front, START's menu); only if none of it started anything,
// and A was just pressed (the player standing still, as the game counts
// it) with L and R held, the menu's script starts as a sign's or a
// person's does (ScriptContext_SetupScript), and the caller locks the
// field's controls as it does for them. Never on another screen, in a
// battle, a fade or a script: DoCB1_Overworld runs only on the overworld,
// with the controls free.
//
// Built by tools/golden/build.sh over Unused_LoadBgPalette (0x08001A4C,
// 0x88 bytes, never called). Addresses are pokeemerald.sym's for this ROM
// (BPEE, CRC32 1F1C08FB).
typedef unsigned char u8;
typedef unsigned short u16;

#define L_BUTTON 0x200
#define R_BUTTON 0x100
#define HELD_KEYS (*(volatile u16 *)(0x030022C0 + 0x2C))  // gMain.heldKeys
#define MENU ((const u8 *)0x08F00800)  // emerald.py's MENU_CORE: the menu base's first script

#define ProcessPlayerFieldInput ((int (*)(void *))0x0809C015)
#define ScriptContext_SetupScript ((void (*)(const u8 *))0x08098EF9)

__attribute__((section(".text.entry"))) int field_input(void *input)
{
	int started = ProcessPlayerFieldInput(input);
	if (started)
		return started;
	// struct FieldInput's pressedAButton, its first bit
	if ((*(const u8 *)input & 1) && (HELD_KEYS & (L_BUTTON | R_BUTTON)) == (L_BUTTON | R_BUTTON)) {
		ScriptContext_SetupScript(MENU);
		return 1;
	}
	return 0;
}
