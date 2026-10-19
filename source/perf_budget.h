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
	{"cover-demo", 19242},
	{"cover-upload", 3427},
	{"diag-cover-decode", 44026},
	{"diag-cover-upload", 3151},
	{"diag-glyph", 380},
	{"diag-page", 237002},
	{"diag-psram-menu", 20963},
	{"diag-psram-slow", 25429},
	{"diag-row38", 9525},
	{"diag-scroll", 98337},
	{"lists-menu-open", 204098},
	{"lists-page", 171561},
	{"lists-screen", 215346},
	{"lists-scroll", 75050},
	{"text-clear-row", 1662},
	{"text-clear-row8-unaligned", 3535},
	{"text-glyph", 353},
	{"text-heading20", 8472},
	{"text-row38", 9498},
	{"ui-dialog-motion", 96337},
	{"ui-dialog-open", 163312},
	{"ui-grid-scroll", 28697},
	{"ui-list-scroll", 120423},
	{"ui-page-change", 248813},
	{"ui-screen-switch", 202849},
	{"ui-transition", 245892},
	{"ui-wallpaper", 175663},
};

#endif
