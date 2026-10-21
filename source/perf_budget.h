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
	{"confirm-motion", 80366},
	{"confirm-move", 85123},
	{"cover-load", 143244},
	{"diag-cover-decode", 44026},
	{"diag-cover-upload", 3151},
	{"diag-glyph", 380},
	{"diag-page", 127915},
	{"diag-psram-menu", 20963},
	{"diag-psram-slow", 25429},
	{"diag-row38", 9525},
	{"diag-scroll", 143616},
	{"fields-move", 54523},
	{"games-artlist", 172868},
	{"games-move", 97236},
	{"info-page", 237195},
	{"letter-show", 169373},
	{"nav-screen", 233786},
	{"nor-move", 95905},
	{"popup-draw", 147281},
	{"popup-motion", 83168},
	{"popup-move", 103247},
	{"row-copy14", 6628},
	{"sd-folder", 144092},
	{"sd-move", 140171},
	{"settings-edit", 135574},
	{"settings-move", 150628},
	{"settings-scroll", 153404},
	{"text-clear-row", 1644},
	{"text-clear-row14", 2579},
	{"text-clear-row8-unaligned", 2235},
	{"text-glyph", 353},
	{"text-heading20", 8472},
	{"text-row14-25", 9103},
	{"text-row14-40", 13566},
	{"text-row38", 9498},
	{"ui-close", 129013},
	{"ui-dialog-motion", 170415},
	{"ui-dialog-open", 162377},
	{"ui-fade", 35031},
	{"ui-list-scroll", 136906},
	{"ui-list-step", 82484},
	{"ui-open", 233786},
	{"ui-page-change", 134898},
	{"ui-transition", 239386},
	{"ui-wipe", 117777},
};

#endif
