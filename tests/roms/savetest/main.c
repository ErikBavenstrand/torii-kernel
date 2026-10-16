// Torii save test ROM. Built once per save chip (SAVE_FLASH128,
// SAVE_FLASH64, SAVE_EEPROM8K, SAVE_EEPROM512): talks to the chip with the
// real protocol, counts boots in it, and draws one bar per boot.
//
// Record layout: "OMRX" + boot count. Flash 128K writes it to bank 1 offset 0,
// so a correct save file holds it at 0x10000. EEPROM writes it to block 1, so
// it lands at byte 8 of the save file.
#include <stdint.h>

#define REG_DISPCNT (*(volatile uint16_t *)0x04000000)
#define REG_DMA3SAD (*(volatile uint32_t *)0x040000D4)
#define REG_DMA3DAD (*(volatile uint32_t *)0x040000D8)
#define REG_DMA3CNT (*(volatile uint32_t *)0x040000DC)
#define REG_WAITCNT (*(volatile uint16_t *)0x04000204)
#define VRAM ((volatile uint16_t *)0x06000000)
#define FLASH ((volatile uint8_t *)0x0E000000)
#define EEPROM ((volatile uint16_t *)0x0D000000)
#define RGB(r, g, b) ((r) | ((g) << 5) | ((b) << 10))

static const uint8_t MAGIC[4] = {'O', 'M', 'R', 'X'};

#if defined(SAVE_FLASH128) || defined(SAVE_FLASH64)

const char save_tag[] __attribute__((used)) = "FLASH1M_V103";

static void flash_cmd(uint8_t cmd)
{
	FLASH[0x5555] = 0xAA;
	FLASH[0x2AAA] = 0x55;
	FLASH[0x5555] = cmd;
}

static void flash_bank(int bank)
{
#ifdef SAVE_FLASH128
	flash_cmd(0xB0);
	FLASH[0x0000] = bank;
#else
	(void)bank;
#endif
}

static void flash_erase_sector(uint32_t offset)
{
	flash_cmd(0x80);
	FLASH[0x5555] = 0xAA;
	FLASH[0x2AAA] = 0x55;
	FLASH[offset] = 0x30;
	while (FLASH[offset] != 0xFF)
		;
}

static void flash_program(uint32_t offset, uint8_t value)
{
	flash_cmd(0xA0);
	FLASH[offset] = value;
	while (FLASH[offset] != value)
		;
}

static void read_record(uint8_t out[5])
{
#ifdef SAVE_FLASH128
	flash_bank(1);
#endif
	for (int i = 0; i < 5; i++)
		out[i] = FLASH[i];
}

static void write_record(const uint8_t rec[5])
{
#ifdef SAVE_FLASH128
	flash_bank(1);
#endif
	flash_erase_sector(0);
	for (int i = 0; i < 5; i++)
		flash_program(i, rec[i]);
	flash_bank(0);
}

#else  // EEPROM

#ifdef SAVE_EEPROM8K
#define ADDR_BITS 14
const char save_tag[] __attribute__((used)) = "EEPROM_V124";
#else
#define ADDR_BITS 6
const char save_tag[] __attribute__((used)) = "EEPROM_V124";
#endif
#define BLOCK 1

static uint16_t bits[ADDR_BITS + 67];

// DMA reads and writes bits[] behind the compiler's back.
#define MEMORY_BARRIER() __asm__ volatile("" ::: "memory")

static void dma_to_eeprom(int count)
{
	MEMORY_BARRIER();
	REG_DMA3SAD = (uint32_t)bits;
	REG_DMA3DAD = (uint32_t)EEPROM;
	REG_DMA3CNT = count | 0x80000000;
	while (REG_DMA3CNT & 0x80000000)
		;
}

static void put_address(int pos, unsigned address)
{
	for (int i = 0; i < ADDR_BITS; i++)
		bits[pos + i] = (address >> (ADDR_BITS - 1 - i)) & 1;
}

static void read_block(unsigned address, uint8_t out[8])
{
	bits[0] = 1;
	bits[1] = 1;
	put_address(2, address);
	bits[2 + ADDR_BITS] = 0;
	dma_to_eeprom(3 + ADDR_BITS);

	REG_DMA3SAD = (uint32_t)EEPROM;
	REG_DMA3DAD = (uint32_t)bits;
	REG_DMA3CNT = 68 | 0x80000000;
	while (REG_DMA3CNT & 0x80000000)
		;
	MEMORY_BARRIER();
	for (int byte = 0; byte < 8; byte++) {
		uint8_t v = 0;
		for (int b = 0; b < 8; b++)
			v = (v << 1) | (bits[4 + byte * 8 + b] & 1);
		out[byte] = v;
	}
}

static void write_block(unsigned address, const uint8_t in[8])
{
	bits[0] = 1;
	bits[1] = 0;
	put_address(2, address);
	for (int byte = 0; byte < 8; byte++)
		for (int b = 0; b < 8; b++)
			bits[2 + ADDR_BITS + byte * 8 + b] = (in[byte] >> (7 - b)) & 1;
	bits[2 + ADDR_BITS + 64] = 0;
	dma_to_eeprom(3 + ADDR_BITS + 64);
	while (!(EEPROM[0] & 1))
		;
}

static void read_record(uint8_t out[5])
{
	uint8_t block[8];
	read_block(BLOCK, block);
	for (int i = 0; i < 5; i++)
		out[i] = block[i];
}

static void write_record(const uint8_t rec[5])
{
	uint8_t block[8] = {0};
	for (int i = 0; i < 5; i++)
		block[i] = rec[i];
	write_block(BLOCK, block);
}

#endif

int main(void)
{
	REG_WAITCNT = 0x4317;  // the cart waitstates commercial games use

	uint8_t rec[5];
	read_record(rec);
	int valid = 1;
	for (int i = 0; i < 4; i++)
		valid &= rec[i] == MAGIC[i];
	unsigned boots = valid ? rec[4] : 0;
	boots = (boots + 1) & 0xFF;
	for (int i = 0; i < 4; i++)
		rec[i] = MAGIC[i];
	rec[4] = boots;
	write_record(rec);

	REG_DISPCNT = 3 | (1 << 10);
	for (int i = 0; i < 240 * 160; i++)
		VRAM[i] = RGB(2, 6, 20);
	for (unsigned b = 0; b < boots && b < 20; b++)
		for (int y = 40; y < 120; y++)
			for (int x = 0; x < 8; x++)
				VRAM[y * 240 + 12 + b * 11 + x] = RGB(31, 28, 4);
	for (;;)
		;
}
