// The cycle budget, tests/perf/budget.txt, for Diagnostics' renderer step
// (source/diag_render.c). Written by tests/perf.py with the budget
// (`just perf-update`): don't edit.
#ifndef TORII_PERF_BUDGET_H
#define TORII_PERF_BUDGET_H

static const struct {
	const char *name;
	unsigned long cycles;
} perf_budget[] = {
	{"cover-decode", 38607},
	{"cover-demo", 19138},
	{"cover-upload", 3442},
	{"diag-cover-decode", 44025},
	{"diag-cover-upload", 3151},
	{"diag-glyph", 380},
	{"diag-page", 234921},
	{"diag-psram-menu", 20963},
	{"diag-psram-slow", 25429},
	{"diag-row38", 9525},
	{"diag-scroll", 84381},
	{"lists-menu-open", 198255},
	{"lists-screen", 210632},
	{"lists-scroll", 67422},
	{"text-clear-row", 1662},
	{"text-clear-row8-unaligned", 3535},
	{"text-glyph", 353},
	{"text-heading20", 8472},
	{"text-row38", 9498},
	{"ui-dialog-open", 161638},
	{"ui-grid-scroll", 26581},
	{"ui-list-scroll", 116613},
	{"ui-page-change", 246531},
	{"ui-screen-switch", 200664},
};

#endif
