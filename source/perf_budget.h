// The cycle budget, tests/perf/budget.txt, for Diagnostics' renderer step
// (source/diag_render.c). Written by tests/perf.py with the budget
// (`just perf-update`): don't edit.
#ifndef TORII_PERF_BUDGET_H
#define TORII_PERF_BUDGET_H

static const struct {
	const char *name;
	unsigned long cycles;
} perf_budget[] = {
	{"carousel-step", 102544},
	{"confirm-draw", 169704},
	{"confirm-motion", 79428},
	{"confirm-move", 84444},
	{"cover-decode", 38610},
	{"cover-demo", 19425},
	{"cover-upload", 3427},
	{"diag-cover-decode", 44026},
	{"diag-cover-upload", 3151},
	{"diag-glyph", 380},
	{"diag-page", 127050},
	{"diag-psram-menu", 20963},
	{"diag-psram-slow", 25429},
	{"diag-row38", 9525},
	{"diag-scroll", 142466},
	{"info-page", 233820},
	{"letter-show", 96297},
	{"nav-screen", 212562},
	{"popup-draw", 143145},
	{"popup-motion", 68065},
	{"popup-move", 89541},
	{"row-copy14", 6628},
	{"text-clear-row", 1644},
	{"text-clear-row14", 2579},
	{"text-clear-row8-unaligned", 2235},
	{"text-glyph", 353},
	{"text-heading20", 8472},
	{"text-row14-25", 9103},
	{"text-row14-40", 13566},
	{"text-row38", 9498},
	{"ui-dialog-motion", 169704},
	{"ui-dialog-open", 160953},
	{"ui-fade", 34294},
	{"ui-list-scroll", 135268},
	{"ui-list-step", 80950},
	{"ui-page-change", 133108},
	{"ui-transition", 243125},
};

#endif
