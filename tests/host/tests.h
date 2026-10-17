// The host tests, each a void function run by tests/host/run.c.
#ifndef TORII_HOST_TESTS_H
#define TORII_HOST_TESTS_H

void test_sort_matches_kernel(void);
void test_sort_is_stable(void);
void test_sort_limits(void);
void test_sort_largest_list(void);
void test_cheat_parse(void);
void test_cheat_reads_only_len(void);
void test_cheat_long_fields(void);
void test_cheat_cap(void);
void test_cheat_garbage(void);
void test_cheat_empty_entries(void);
void test_cheat_whitespace(void);

#endif
