// The build ID (source/build_id.h) that tools/build_id.py writes after linking.
#include "../../source/build_id.h"
#include "torii_test.h"
#include "tests.h"

void test_build_id_matches_the_rom(void)
{
	CHECK(torii_build_id != 0);  // 0: built without tools/build_id.py
	CHECK_EQ(build_id_compute(), torii_build_id);
}
