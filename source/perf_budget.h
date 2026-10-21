// The cycle budget, tests/perf/budget.txt, for Diagnostics' renderer step
// (source/diag_render.c). Written by tests/perf.py with the budget
// (`just perf-update`): don't edit.
#ifndef TORII_PERF_BUDGET_H
#define TORII_PERF_BUDGET_H

static const struct {
	const char *name;
	unsigned long cycles;
} perf_budget[] = {
	{"carousel-step", 110752},
	{"confirm-draw", 145728},
	{"confirm-motion", 169684},
	{"confirm-move", 85254},
	{"cover-load", 141533},
	{"diag-cover-decode", 44026},
	{"diag-cover-upload", 3151},
	{"diag-glyph", 380},
	{"diag-page", 127438},
	{"diag-psram-menu", 20963},
	{"diag-psram-slow", 25429},
	{"diag-row38", 9525},
	{"diag-scroll", 143026},
	{"fields-move", 54665},
	{"games-artlist", 180127},
	{"games-move", 95928},
	{"info-page", 240841},
	{"letter-show", 178198},
	{"nav-screen", 238109},
	{"nor-move", 96082},
	{"popup-draw", 147365},
	{"popup-motion", 80761},
	{"popup-move", 104024},
	{"rename-key", 128627},
	{"row-copy14", 6628},
	{"sd-count", 113712},
	{"sd-folder", 143874},
	{"sd-move", 140251},
	{"settings-edit", 135630},
	{"settings-move", 150495},
	{"settings-scroll", 151749},
	{"text-clear-row", 1644},
	{"text-clear-row14", 2579},
	{"text-clear-row8-unaligned", 2235},
	{"text-glyph", 353},
	{"text-heading20", 8472},
	{"text-row14-25", 9103},
	{"text-row14-40", 13566},
	{"text-row38", 9498},
	{"ui-close", 124415},
	{"ui-dialog-motion", 169684},
	{"ui-dialog-open", 162457},
	{"ui-fade", 35271},
	{"ui-list-scroll", 136957},
	{"ui-list-step", 82577},
	{"ui-open", 238109},
	{"ui-page-change", 134833},
	{"ui-transition", 240841},
	{"ui-wipe", 118054},
};

#endif
