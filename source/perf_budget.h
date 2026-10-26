// The cycle budget, tests/perf/budget.txt, for Diagnostics' renderer step
// (source/diag_render.c). Written by tests/perf.py with the budget
// (`just perf-update`): don't edit.
#ifndef TORII_PERF_BUDGET_H
#define TORII_PERF_BUDGET_H

static const struct {
	const char *name;
	unsigned long cycles;
} perf_budget[] = {
	{"carousel-step", 111068},
	{"confirm-draw", 145731},
	{"confirm-motion", 160179},
	{"confirm-move", 85336},
	{"cover-load", 143227},
	{"diag-cover-decode", 44026},
	{"diag-cover-upload", 3151},
	{"diag-glyph", 380},
	{"diag-page", 147086},
	{"diag-psram-menu", 20963},
	{"diag-psram-slow", 25429},
	{"diag-row38", 9525},
	{"diag-scroll", 117521},
	{"fields-move", 54661},
	{"games-artlist", 180404},
	{"games-move", 104363},
	{"info-page", 241044},
	{"keypad-key", 157334},
	{"letter-show", 182285},
	{"nav-screen", 242701},
	{"nor-move", 96128},
	{"popup-draw", 152231},
	{"popup-motion", 93842},
	{"popup-move", 138696},
	{"rename-key", 128584},
	{"row-copy14", 6628},
	{"sd-count", 113981},
	{"sd-folder", 143903},
	{"sd-move", 140245},
	{"settings-edit", 135797},
	{"settings-move", 151098},
	{"settings-scroll", 151098},
	{"text-clear-row", 1644},
	{"text-clear-row14", 2579},
	{"text-clear-row8-unaligned", 2235},
	{"text-glyph", 353},
	{"text-heading20", 8472},
	{"text-row14-25", 9103},
	{"text-row14-40", 13566},
	{"text-row38", 9498},
	{"ui-close", 124744},
	{"ui-dialog-motion", 162463},
	{"ui-dialog-open", 162463},
	{"ui-fade", 35481},
	{"ui-list-scroll", 137199},
	{"ui-list-step", 82665},
	{"ui-open", 242701},
	{"ui-page-change", 134962},
	{"ui-transition", 241044},
	{"ui-wipe", 117842},
};

#endif
