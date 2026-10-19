// See psram.h.
#include "psram.h"
#include "res.h"
#include "../Ezcard_OP.h"
#include "../waitcnt.h"

#define DMA3SAD (*(vu32 *)0x040000D4)
#define DMA3DAD (*(vu32 *)0x040000D8)
#define DMA3CNT (*(vu32 *)0x040000DC)
#define DMA_ON_16 0x80000000u
#define PSRAM_PIECE 1024

void psram_begin(void)
{
	SetPSRampage(PSRAM_ASSET_PAGE);
}

int psram_write(void *dst, const void *src, u32 bytes)
{
	bytes = (bytes + 1) & ~1u;
	if (!bytes || ((u32)dst | (u32)src) & 1 || !psram_holds(dst, bytes) || psram_holds(src, bytes)) {
		res_set_error("bad PSRAM write: %08lX from %08lX, %lu bytes", (unsigned long)(u32)dst,
			      (unsigned long)(u32)src, (unsigned long)bytes);
		return 0;
	}
	u16 waitcnt = WAITCNT_SLOW();
	for (u32 done = 0; done < bytes; done += PSRAM_PIECE) {
		u32 n = bytes - done < PSRAM_PIECE ? bytes - done : PSRAM_PIECE;
		DMA3SAD = (u32)src + done;
		DMA3DAD = (u32)dst + done;
		DMA3CNT = DMA_ON_16 | n / 2;
	}
	WAITCNT_RESTORE(waitcnt);
	return 1;
}
