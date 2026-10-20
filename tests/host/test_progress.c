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

// The copy's estimate (progress_copy_plan and progress_copy_advance): what
// the VBlank interrupt moves the bar by during the cart's ROM copy
void test_progress_copy(void)
{
	struct progress_copy c;
	// 10 to 190 px over 64 frames: 90% of the way (172 px) at frame 64
	progress_copy_plan(&c, 10, 190, 64);
	u32 last = 10, at64 = 0;
	for (u32 f = 1; f <= 1000; f++) {
		u32 x = progress_copy_advance(&c);
		CHECK(x >= last && x <= 190);  // forward only, never past the end
		if (f == 64)
			at64 = x;
		last = x;
	}
	CHECK(at64 >= 170 && at64 <= 172);
	CHECK_EQ(last, 190);  // it reaches the end, then stays
	// After the knee, an eighth of the speed: 18 px take about 8 x 18 / 2.8 frames
	progress_copy_plan(&c, 10, 190, 64);
	u32 f = 0;
	while (progress_copy_advance(&c) < 190)
		f++;
	CHECK(f > 64 + 40 && f < 64 + 60);
	// A copy shorter than a frame: 90% in one, then the crawl
	progress_copy_plan(&c, 2, 100, 0);
	CHECK_EQ(progress_copy_advance(&c), 90);
	CHECK(progress_copy_advance(&c) <= 100);
	// Nothing to fill, and a step that ends before it starts
	progress_copy_plan(&c, 50, 50, 64);
	CHECK_EQ(progress_copy_advance(&c), 50);
	progress_copy_plan(&c, 80, 40, 64);
	CHECK_EQ(progress_copy_advance(&c), 80);
	// A 32 MB game at the slowest rate: thousands of frames, still moving
	progress_copy_plan(&c, 10, 230, 0x2000000 / 0x100);
	u32 a = progress_copy_advance(&c);
	for (u32 i = 0; i < 2000; i++)
		progress_copy_advance(&c);
	CHECK(progress_copy_advance(&c) > a);
}
