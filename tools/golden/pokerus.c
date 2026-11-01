// Pokémon Emerald: a Pokémon you catch has Pokérus. Cmd_givecaughtmon's
// call to GiveMonToPlayer (0x080566CE), which only a caught Pokémon goes
// through (Safari Zone's too), comes here: Pokérus is set as the game sets
// it (RandomlyGivePartyPokerus' value for strain 1: 2 days left), then the
// Pokémon goes to the party or the PC as before. Nothing else changes: its
// met location, ball and the rest are the game's.
//
// Built by tools/golden/build.sh over Unused_ApplyRandomDmgMultiplier
// (0x080478B8, 0x3C bytes, never called), within a bl of the catch.
// Addresses are pokeemerald.sym's for this ROM (BPEE, CRC32 1F1C08FB).
typedef unsigned char u8;

#define MON_DATA_POKERUS 34
#define POKERUS_STRAIN_1 0x12  // strain 1 in the high nibble, 2 days in the low

#define SetMonData ((void (*)(void *, int, const void *))0x0806ACAD)
#define GiveMonToPlayer ((u8 (*)(void *))0x0806B415)

__attribute__((section(".text.entry"))) u8 give_caught_mon(void *mon)
{
	u8 pokerus = POKERUS_STRAIN_1;
	SetMonData(mon, MON_DATA_POKERUS, &pokerus);
	return GiveMonToPlayer(mon);
}
