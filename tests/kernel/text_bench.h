// The alternative blitters in text_bench.iwram.c (test kernel only).
#ifndef TORII_TEXT_BENCH_H
#define TORII_TEXT_BENCH_H

#include "../../source/engine/text.h"

// 0x01010101, defined in test_text.c
extern const u32 bench_spread_mask;

void bench_blit_c(const struct text_run *run);
void bench_blit_tonc(const struct text_run *run);

#endif
