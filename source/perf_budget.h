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
	{"confirm-draw", 169808},
	{"confirm-motion", 79551},
	{"confirm-move", 84557},
	{"cover-decode", 38610},
	{"cover-demo", 19425},
	{"cover-upload", 3427},
	{"diag-cover-decode", 44026},
	{"diag-cover-upload", 3151},
	{"diag-glyph", 380},
	{"diag-page", 128013},
	{"diag-psram-menu", 20963},
	{"diag-psram-slow", 25429},
	{"diag-row38", 9525},
	{"diag-scroll", 143528},
	{"fields-move", 54341},
	{"games-move", 92854},
	{"info-page", 236388},
	{"letter-show", 154468},
	{"nav-screen", 239886},
	{"nor-move", 95493},
	{"popup-draw", 146532},
	{"popup-motion", 82724},
	{"popup-move", 102755},
	{"row-copy14", 6628},
	{"sd-folder", 143097},
	{"sd-move", 139621},
	{"settings-edit", 134944},
	{"settings-move", 150351},
	{"settings-scroll", 153100},
	{"text-clear-row", 1644},
	{"text-clear-row14", 2579},
	{"text-clear-row8-unaligned", 2235},
	{"text-glyph", 353},
	{"text-heading20", 8472},
	{"text-row14-25", 9103},
	{"text-row14-40", 13566},
	{"text-row38", 9498},
	{"ui-dialog-motion", 169808},
	{"ui-dialog-open", 161603},
	{"ui-fade", 34439},
	{"ui-list-scroll", 136686},
	{"ui-list-step", 81851},
	{"ui-page-change", 134332},
	{"ui-transition", 239886},
};

#endif
