// The cycle budget, tests/perf/budget.txt, for Diagnostics' renderer step
// (source/diag_render.c). Written by tests/perf.py with the budget
// (`just perf-update`): don't edit.
#ifndef TORII_PERF_BUDGET_H
#define TORII_PERF_BUDGET_H

static const struct {
	const char *name;
	unsigned long cycles;
} perf_budget[] = {
	{"carousel-step", 110619},
	{"confirm-draw", 170415},
	{"confirm-motion", 80000},
	{"confirm-move", 85123},
	{"cover-decode", 38610},
	{"cover-demo", 19503},
	{"cover-upload", 3427},
	{"diag-cover-decode", 44026},
	{"diag-cover-upload", 3151},
	{"diag-glyph", 380},
	{"diag-page", 128119},
	{"diag-psram-menu", 20963},
	{"diag-psram-slow", 25429},
	{"diag-row38", 9525},
	{"diag-scroll", 143647},
	{"fields-move", 54523},
	{"games-move", 93479},
	{"info-page", 237247},
	{"letter-show", 155014},
	{"nav-screen", 233256},
	{"nor-move", 96034},
	{"popup-draw", 147264},
	{"popup-motion", 83203},
	{"popup-move", 103312},
	{"row-copy14", 6628},
	{"sd-folder", 144053},
	{"sd-move", 140171},
	{"settings-edit", 135310},
	{"settings-move", 150844},
	{"settings-scroll", 153524},
	{"text-clear-row", 1644},
	{"text-clear-row14", 2579},
	{"text-clear-row8-unaligned", 2235},
	{"text-glyph", 353},
	{"text-heading20", 8472},
	{"text-row14-25", 9103},
	{"text-row14-40", 13566},
	{"text-row38", 9498},
	{"ui-close", 129130},
	{"ui-dialog-motion", 170415},
	{"ui-dialog-open", 162423},
	{"ui-fade", 35031},
	{"ui-list-scroll", 136906},
	{"ui-list-step", 82484},
	{"ui-open", 233256},
	{"ui-page-change", 134898},
	{"ui-transition", 239439},
	{"ui-wipe", 117842},
};

#endif
