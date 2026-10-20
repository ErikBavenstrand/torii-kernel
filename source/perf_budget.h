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
	{"cover-demo", 19423},
	{"cover-upload", 3427},
	{"diag-cover-decode", 44026},
	{"diag-cover-upload", 3151},
	{"diag-glyph", 380},
	{"diag-page", 126878},
	{"diag-psram-menu", 20963},
	{"diag-psram-slow", 25429},
	{"diag-row38", 9525},
	{"diag-scroll", 142272},
	{"lists-menu-open", 268354},
	{"lists-page", 144195},
	{"lists-screen", 257725},
	{"lists-scroll", 118411},
	{"row-copy14", 6628},
	{"text-clear-row", 1644},
	{"text-clear-row14", 2579},
	{"text-clear-row8-unaligned", 2235},
	{"text-glyph", 353},
	{"text-heading20", 8472},
	{"text-row14-25", 9103},
	{"text-row14-40", 13566},
	{"text-row38", 9498},
	{"ui-dialog-motion", 227888},
	{"ui-dialog-open", 160941},
	{"ui-fade", 34138},
	{"ui-list-scroll", 135086},
	{"ui-list-step", 80768},
	{"ui-page-change", 132946},
	{"ui-transition", 268354},
};

#endif
