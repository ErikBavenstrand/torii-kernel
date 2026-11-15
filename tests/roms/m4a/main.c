// Torii test ROM: the sound DMA of Nintendo's MP2000 (m4a, "Sappy") engine, as
// pret's decomps have it (pokeemerald's include/gba/m4a_internal.h, src/m4a.c,
// src/m4a_1.s), for tests/m4a_sound.py: a SoundInfo the word at 0x3007FF0
// points at (ident "Smsh", pcmDmaCounter, pcmDmaPeriod, pcmSamplesPerVBlank,
// pcmBuffer at 0x350), SoundInit's DMA1 and DMA2 to FIFO A and B, timer 0
// started at a VBlank (SampleFreqSet), m4aSoundVSync's re-arm in the VBlank
// IRQ and SoundMain's mixing into the segment ahead in the main loop. The
// samples are a ramp, one step a sample, whose phase runs on across frames.
#include <stddef.h>
#include <stdint.h>

#define REG16(a) (*(volatile uint16_t *)(a))
#define REG32(a) (*(volatile uint32_t *)(a))
#define REG_DISPCNT REG16(0x04000000)
#define REG_DISPSTAT REG16(0x04000004)
#define REG_VCOUNT REG16(0x04000006)
#define REG_SOUNDCNT_H REG16(0x04000082)
#define REG_SOUNDCNT_X REG16(0x04000084)
#define REG_DMA1SAD REG32(0x040000BC)
#define REG_DMA1DAD REG32(0x040000C0)
#define REG_DMA1CNT REG32(0x040000C4)
#define REG_DMA1CNT_H REG16(0x040000C6)
#define REG_DMA2SAD REG32(0x040000C8)
#define REG_DMA2DAD REG32(0x040000CC)
#define REG_DMA2CNT REG32(0x040000D0)
#define REG_DMA2CNT_H REG16(0x040000D2)
#define REG_TM0CNT_L REG16(0x04000100)
#define REG_TM0CNT_H REG16(0x04000102)
#define REG_IE REG16(0x04000200)
#define REG_IF REG16(0x04000202)
#define REG_IME REG16(0x04000208)
#define IRQ_HANDLER (*(void (*volatile *)(void))0x03007FFC)
#define BIOS_IF REG16(0x03007FF8)
#define SOUND_INFO_PTR (*(struct SoundInfo *volatile *)0x03007FF0)

// m4a-other.gba (OTHER_IDENT): the same DMA under another ident, which the
// stub must leave as it leaves a game not on the engine
#ifdef OTHER_IDENT
#define ID_NUMBER 0x68736D60u
#else
#define ID_NUMBER 0x68736D53u
#endif
#define PCM_DMA_BUF_SIZE 1584
#define SAMPLES 224  // 13379 Hz: gPcmSamplesPerVBlankTable[3]

// Lets the kernel's save-type detector classify this as SRAM, as hello's
const char save_tag[] __attribute__((used)) = "SRAM_V113";

struct SoundInfo {
	uint32_t ident;
	volatile uint8_t pcmDmaCounter;
	uint8_t reverb, maxChans, masterVolume, freq, mode, c15;
	uint8_t pcmDmaPeriod;
	uint8_t maxLines, gap[3];
	int32_t pcmSamplesPerVBlank;
	uint8_t rest[0x350 - 0x14];  // the engine's other fields and its 12 channels
	int8_t pcmBuffer[PCM_DMA_BUF_SIZE * 2] __attribute__((aligned(4)));
};
_Static_assert(offsetof(struct SoundInfo, pcmDmaPeriod) == 0x0B, "pcmDmaPeriod");
_Static_assert(offsetof(struct SoundInfo, pcmSamplesPerVBlank) == 0x10, "pcmSamplesPerVBlank");
_Static_assert(offsetof(struct SoundInfo, pcmBuffer) == 0x350, "pcmBuffer");

static struct SoundInfo sound_info __attribute__((aligned(4)));
static volatile unsigned frames;

// DMA_ENABLE | DMA_START_NOW | DMA_32BIT | DMA_SRC_INC | DMA_DEST_FIXED, 4 words
#define DMA_REARM ((0x8000u | 0x0400u | 0x0040u) << 16 | 4)

// m4aSoundVSync
__attribute__((target("arm"))) static void on_irq(void)
{
	REG_IF = 1;
	BIOS_IF |= 1;
	++frames;
	struct SoundInfo *si = SOUND_INFO_PTR;
	if (si->ident - ID_NUMBER > 1)
		return;
	int counter = si->pcmDmaCounter - 1;
	si->pcmDmaCounter = (uint8_t)counter;
	if (counter > 0)
		return;
	si->pcmDmaCounter = si->pcmDmaPeriod;
	if (REG_DMA1CNT & (0x0200u << 16))  // repeat
		REG_DMA1CNT = DMA_REARM;
	if (REG_DMA2CNT & (0x0200u << 16))
		REG_DMA2CNT = DMA_REARM;
	REG_DMA1CNT_H = 0x0400;
	REG_DMA2CNT_H = 0x0400;
	REG_DMA1CNT_H = 0xB600;  // on, repeat, 32-bit, FIFO timing
	REG_DMA2CNT_H = 0xB600;
}

// SoundMain: the segment the DMAs play after the next VSync's
static void sound_main(void)
{
	static unsigned phase;
	struct SoundInfo *si = SOUND_INFO_PTR;
	if (si->ident != ID_NUMBER)
		return;
	si->ident++;
	int ahead = si->pcmDmaCounter - 1;
	int8_t *out = si->pcmBuffer;
	if (ahead > 0)
		out += (si->pcmDmaPeriod - ahead) * si->pcmSamplesPerVBlank;
	for (int i = 0; i < si->pcmSamplesPerVBlank; i++, phase++) {
		out[i] = (int8_t)(phase & 0x7F);
		out[i + PCM_DMA_BUF_SIZE] = (int8_t)(-(int)(phase & 0x7F));
	}
	si->ident--;
}

// SoundInit and SampleFreqSet
static void sound_init(void)
{
	struct SoundInfo *si = &sound_info;
	REG_DMA1CNT_H = 0x0400;
	REG_DMA2CNT_H = 0x0400;
	REG_SOUNDCNT_X = 0x8F;
	REG_SOUNDCNT_H = 0xA90E;  // FIFOs reset, both on timer 0, A right, B left, full
	REG_DMA1SAD = (uint32_t)si->pcmBuffer;
	REG_DMA1DAD = 0x040000A0;
	REG_DMA2SAD = (uint32_t)si->pcmBuffer + PCM_DMA_BUF_SIZE;
	REG_DMA2DAD = 0x040000A4;
	SOUND_INFO_PTR = si;
	si->pcmSamplesPerVBlank = SAMPLES;
	si->pcmDmaPeriod = PCM_DMA_BUF_SIZE / SAMPLES;
	REG_TM0CNT_H = 0;
	REG_TM0CNT_L = (uint16_t)-(280896 / SAMPLES);
	// m4aSoundVSyncOn
	REG_DMA1CNT_H = 0xB600;
	REG_DMA2CNT_H = 0xB600;
	si->pcmDmaCounter = 0;
	while (REG_VCOUNT == 159)
		;
	while (REG_VCOUNT != 159)
		;
	REG_TM0CNT_H = 0x80;
	si->ident = ID_NUMBER;
}

int main(void)
{
	REG_DISPCNT = 0;  // mode 0, no layers: the backdrop
	sound_init();
	IRQ_HANDLER = on_irq;
	REG_DISPSTAT = 1 << 3;  // VBlank interrupt
	REG_IE = 1;
	REG_IME = 1;
	for (;;) {
		// Polled, as hello's: many games poll
		for (unsigned last = frames; frames == last;)
			;
		sound_main();
	}
}
