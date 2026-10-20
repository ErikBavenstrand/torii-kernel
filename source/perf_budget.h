// The cycle budget, tests/perf/budget.txt, for Diagnostics' renderer step
// (source/diag_render.c). Written by tests/perf.py with the budget
// (`just perf-update`): don't edit.
#ifndef TORII_PERF_BUDGET_H
#define TORII_PERF_BUDGET_H

static const struct {
	const char *name;
	unsigned long cycles;
} perf_budget[] = {
	{"cover-decode", 38610},
	{"cover-demo", 19359},
	{"cover-upload", 3427},
	{"diag-cover-decode", 44026},
	{"diag-cover-upload", 3151},
	{"diag-glyph", 380},
	{"diag-page", 124627},
	{"diag-psram-menu", 20963},
	{"diag-psram-slow", 25429},
	{"diag-row38", 9525},
	{"diag-scroll", 97455},
	{"lists-menu-open", 201484},
	{"lists-page", 147357},
	{"lists-screen", 212860},
	{"lists-scroll", 89398},
	{"text-clear-row", 1662},
	{"text-clear-row8-unaligned", 3535},
	{"text-glyph", 353},
	{"text-heading20", 8472},
	{"text-row38", 9498},
	{"ui-dialog-motion", 158851},
	{"ui-dialog-open", 158851},
	{"ui-fade", 27877},
	{"ui-list-scroll", 167748},
	{"ui-page-change", 149332},
	{"ui-transition", 220372},
};

#endif
