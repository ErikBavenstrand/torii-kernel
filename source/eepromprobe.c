// The EEPROM probe's report (see eepromprobe.h): hardware-free, so the probe
// ROM, the kernel and tests/host share it.
#include <string.h>

#include "eepromprobe.h"

void eeprom_probe_pattern(uint8_t out[8], uint32_t width, uint32_t block, uint32_t nonce)
{
	out[0] = 0xE0 | (width & 0xF);
	out[1] = (uint8_t)(block >> 8);
	out[2] = (uint8_t)block;
	out[3] = (uint8_t)(nonce >> 8);
	out[4] = (uint8_t)nonce;
	out[5] = (uint8_t)~block;
	out[6] = (uint8_t)~(block >> 8);
	out[7] = (uint8_t)(0x3C ^ width);
}

static uint32_t step_count(const struct eeprom_probe *p)
{
	return p->steps < EEPROM_PROBE_STEPS ? p->steps : EEPROM_PROBE_STEPS;
}

int eeprom_probe_source(const struct eeprom_probe *p, uint32_t i, int *near)
{
	if (near)
		*near = 0;
	if (i >= step_count(p) || p->step[i].op != EEPROM_PROBE_READ)
		return -1;
	for (uint32_t j = i; j-- > 0;) {
		if (p->step[j].op == EEPROM_PROBE_WRITE && !memcmp(p->step[j].data, p->step[i].data, 8))
			return (int)j;
	}
	for (uint32_t j = i; j-- > 0;) {
		if (p->step[j].op == EEPROM_PROBE_WRITE && !memcmp(p->step[j].data, p->step[i].data, 7)) {
			if (near)
				*near = 1;
			return (int)j;
		}
	}
	return -1;
}

uint32_t eeprom_probe_verdict(const struct eeprom_probe *p)
{
	uint32_t flags = 0, n = step_count(p);
	int first_write = -1, read_1023 = 0;
	for (uint32_t i = 0; i < n; i++) {
		const struct eeprom_probe_step *s = &p->step[i];
		if (s->op == EEPROM_PROBE_WRITE) {
			if (s->polls == EEPROM_PROBE_TIMEOUT)
				flags |= EEPROM_PROBE_TIMED_OUT;
			if (first_write < 0)
				first_write = (int)i;
			continue;
		}
		if (s->op != EEPROM_PROBE_READ)
			continue;
		int near;
		int src = eeprom_probe_source(p, i, &near);
		if (src < 0)
			continue;
		if (near)
			flags |= EEPROM_PROBE_LAST_BYTE_LOST;
		uint32_t from = p->step[src].block;
		if (src == first_write && from == s->block && !near)
			flags |= EEPROM_PROBE_WORKS;
		if (from != s->block && from % 64 == s->block % 64)
			flags |= EEPROM_PROBE_WRAPS;
		if (from == 1023 && s->block == 1023)
			read_1023 = 1;
	}
	if (read_1023 && !(flags & EEPROM_PROBE_WRAPS))
		flags |= EEPROM_PROBE_8K;
	return flags;
}
