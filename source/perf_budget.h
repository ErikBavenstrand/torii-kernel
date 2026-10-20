// The cycle budget, tests/perf/budget.txt, for Diagnostics' renderer step
// (source/diag_render.c). Written by tests/perf.py with the budget
// (`just perf-update`): don't edit.
#ifndef TORII_PERF_BUDGET_H
#define TORII_PERF_BUDGET_H

static const struct {
	const char *name;
	unsigned long cycles;
} perf_budget[] = {
	{"confirm-draw", 169605},
	{"confirm-motion", 79316},
	{"confirm-move", 84488},
	{"cover-decode", 38610},
	{"cover-demo", 19425},
	{"cover-upload", 3427},
	{"diag-cover-decode", 44026},
	{"diag-cover-upload", 3151},
	{"diag-glyph", 380},
	{"diag-page", 126981},
	{"diag-psram-menu", 20963},
	{"diag-psram-slow", 25429},
	{"diag-row38", 9525},
	{"diag-scroll", 142397},
	{"letter-show", 96611},
	{"lists-menu-open", 268717},
	{"lists-page", 144551},
	{"lists-screen", 260134},
	{"lists-scroll", 119239},
	{"popup-draw", 143211},
	{"popup-motion", 68122},
	{"popup-move", 89524},
	{"row-copy14", 6628},
	{"text-clear-row", 1644},
	{"text-clear-row14", 2579},
	{"text-clear-row8-unaligned", 2235},
	{"text-glyph", 353},
	{"text-heading20", 8472},
	{"text-row14-25", 9103},
	{"text-row14-40", 13566},
	{"text-row38", 9498},
	{"ui-dialog-motion", 228451},
	{"ui-dialog-open", 160933},
	{"ui-fade", 34204},
	{"ui-list-scroll", 135182},
	{"ui-list-step", 80864},
	{"ui-page-change", 133018},
	{"ui-transition", 268717},
};

#endif
