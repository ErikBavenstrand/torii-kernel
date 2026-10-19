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
	{"cover-upload", 3427},
	{"diag-cover-decode", 44025},
	{"diag-cover-upload", 3151},
	{"diag-glyph", 380},
	{"diag-page", 236106},
	{"diag-psram-menu", 20963},
	{"diag-psram-slow", 25429},
	{"diag-row38", 9525},
	{"diag-scroll", 85397},
	{"lists-menu-open", 202241},
	{"lists-screen", 212561},
	{"lists-scroll", 68482},
	{"text-clear-row", 1662},
	{"text-clear-row8-unaligned", 3535},
	{"text-glyph", 353},
	{"text-heading20", 8472},
	{"text-row38", 9498},
	{"ui-dialog-motion", 95468},
	{"ui-dialog-open", 161340},
	{"ui-grid-scroll", 27635},
	{"ui-list-scroll", 117787},
	{"ui-page-change", 247706},
	{"ui-screen-switch", 201863},
	{"ui-transition", 244736},
	{"ui-wallpaper", 174667},
};

#endif
