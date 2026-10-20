// The cycle budget, tests/perf/budget.txt, for Diagnostics' renderer step
// (source/diag_render.c). Written by tests/perf.py with the budget
// (`just perf-update`): don't edit.
#ifndef TORII_PERF_BUDGET_H
#define TORII_PERF_BUDGET_H

static const struct {
	const char *name;
	unsigned long cycles;
} perf_budget[] = {
	{"carousel-step", 108834},
	{"confirm-draw", 169686},
	{"confirm-motion", 79367},
	{"confirm-move", 84443},
	{"cover-decode", 38610},
	{"cover-demo", 19425},
	{"cover-upload", 3427},
	{"diag-cover-decode", 44026},
	{"diag-cover-upload", 3151},
	{"diag-glyph", 380},
	{"diag-page", 126917},
	{"diag-psram-menu", 20963},
	{"diag-psram-slow", 25429},
	{"diag-row38", 9525},
	{"diag-scroll", 142622},
	{"info-page", 234248},
	{"letter-show", 96341},
	{"nav-screen", 223234},
	{"nor-move", 95483},
	{"popup-draw", 143196},
	{"popup-motion", 68151},
	{"popup-move", 89540},
	{"row-copy14", 6628},
	{"text-clear-row", 1644},
	{"text-clear-row14", 2579},
	{"text-clear-row8-unaligned", 2235},
	{"text-glyph", 353},
	{"text-heading20", 8472},
	{"text-row14-25", 9103},
	{"text-row14-40", 13566},
	{"text-row38", 9498},
	{"ui-dialog-motion", 169686},
	{"ui-dialog-open", 161137},
	{"ui-fade", 34327},
	{"ui-list-scroll", 135595},
	{"ui-list-step", 81398},
	{"ui-page-change", 133448},
	{"ui-transition", 241143},
};

#endif
