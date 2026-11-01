// Pokémon Emerald's menu Evolve: the party Pokémon chosen (VAR_0x8004, as
// ChoosePartyMon leaves it) evolves as a trade would evolve it: the game's
// own GetEvolutionTargetSpecies in EVO_MODE_TRADE (which takes a trade
// item it holds, as a trade does), then the evolution scene, back to the
// field with the script going on. VAR_RESULT: 1 if it evolves, else 0 (an
// egg, nothing chosen, or no trade evolution). Called by the section's
// script as
//     callnative EVOLVE+1
//     waitstate          (only when VAR_RESULT is 1: the scene runs)
// Position-independent: emerald.py places it in the section's blob.
// Built by tools/golden/build.sh.
typedef unsigned char u8;
typedef unsigned short u16;
typedef unsigned int u32;

#define VAR_8004 (*(volatile u16 *)0x020375E0)  // gSpecialVar_0x8004
#define VAR_RESULT (*(volatile u16 *)0x020375F0)  // gSpecialVar_Result
#define PARTY ((u8 *)0x020244EC)  // gPlayerParty, struct Pokemon of 100 bytes
#define AFTER_EVOLUTION (*(void (**)(void))0x030061E8)  // gCB2_AfterEvolution
#define MON_SIZE 100
#define PARTY_SIZE 6
#define MON_DATA_IS_EGG 45
#define EVO_MODE_TRADE 1

#define GetMonData ((u32 (*)(void *, int, u8 *))0x0806A519)
#define GetEvolutionTargetSpecies ((u16 (*)(void *, u8, u16))0x0806D099)
#define BeginEvolutionScene ((void (*)(void *, u16, u8, u8))0x0813DA41)
#define CB2_ReturnToFieldContinueScriptPlayMapMusic ((void (*)(void))0x080861CD)

__attribute__((section(".text.entry"))) int evolve(void)
{
	u16 slot = VAR_8004;
	VAR_RESULT = 0;
	if (slot >= PARTY_SIZE)
		return 0;
	u8 *mon = PARTY + slot * MON_SIZE;
	if (GetMonData(mon, MON_DATA_IS_EGG, 0))
		return 0;
	u16 to = GetEvolutionTargetSpecies(mon, EVO_MODE_TRADE, 0);
	if (!to)
		return 0;
	VAR_RESULT = 1;
	AFTER_EVOLUTION = CB2_ReturnToFieldContinueScriptPlayMapMusic;
	BeginEvolutionScene(mon, to, 0, (u8)slot);  // no B to stop it, as a trade's
	return 0;
}
