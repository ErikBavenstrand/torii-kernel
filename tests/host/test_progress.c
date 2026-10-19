// progress_permille and progress_estimate (source/progress_calc.c).
#include "../../source/progress.h"
#include "../kernel/torii_test.h"
#include "tests.h"

void test_progress_permille(void)
{
	CHECK_EQ(progress_permille(0, 1000, 0, 100), 0);
	CHECK_EQ(progress_permille(0, 1000, 50, 100), 500);
	CHECK_EQ(progress_permille(100, 900, 1, 2), 500);
	CHECK_EQ(progress_permille(100, 900, 2, 2), 900);   // done: the step's end
	CHECK_EQ(progress_permille(100, 900, 3, 2), 900);   // past the end: clamped
	CHECK_EQ(progress_permille(100, 900, 0, 0), 900);   // nothing to do: done
	CHECK_EQ(progress_permille(500, 400, 1, 2), 500);   // hi below lo: lo
	// 32 MB files and 4 GB counts don't overflow
	CHECK_EQ(progress_permille(0, 1000, 0x1000000, 0x2000000), 500);
	CHECK_EQ(progress_permille(0, 1000, 0xFFFFFFFE, 0xFFFFFFFF), 999);
	CHECK_EQ(progress_permille(0, 100, 0x1FE0000, 0x2000000), 99);
	// Only forward as done grows
	u32 last = 0;
	for (u32 done = 0; done <= 0x2000000; done += 0x20000) {
		u32 p = progress_permille(0, 1000, done, 0x2000000);
		CHECK(p >= last);
		last = p;
	}
	CHECK_EQ(last, 1000);
}

void test_progress_estimate(void)
{
	CHECK_EQ(progress_estimate(0, 1000), 0);
	CHECK_EQ(progress_estimate(500, 1000), 475);
	CHECK_EQ(progress_estimate(1000, 1000), 950);
	CHECK_EQ(progress_estimate(2000, 1000), 970);
	CHECK(progress_estimate(0xFFFFFFFF, 1000) < 1000);
	CHECK_EQ(progress_estimate(0, 0), 950);
	CHECK_EQ(progress_estimate(5, 0), 990);
	// Forward only, below full, however long it takes
	u32 last = 0;
	for (u32 t = 0; t < 100000; t += 7) {
		u32 p = progress_estimate(t, 14336);
		CHECK(p >= last && p < PROGRESS_FULL);
		last = p;
	}
}
