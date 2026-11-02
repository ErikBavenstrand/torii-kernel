// The cycle budget, tests/perf/budget.txt, for Diagnostics' renderer step
// (source/diag_render.c). Written by tests/perf.py with the budget
// (`just perf-update`): don't edit.
#ifndef TORII_PERF_BUDGET_H
#define TORII_PERF_BUDGET_H

static const struct {
	const char *name;
	unsigned long cycles;
} perf_budget[] = {
	{"carousel-step", 121814},
	{"cheats-move", 148907},
	{"cheats-toggle", 162233},
	{"confirm-draw", 162017},
	{"confirm-motion", 185148},
	{"confirm-move", 92764},
	{"cover-load", 144204},
	{"diag-cover-decode", 45302},
	{"diag-cover-upload", 3162},
	{"diag-glyph", 416},
	{"diag-page", 142456},
	{"diag-psram-menu", 21018},
	{"diag-psram-slow", 25429},
	{"diag-row38", 10001},
	{"diag-scroll", 149683},
	{"fields-move", 60681},
	{"games-artlist", 128273},
	{"games-move", 105368},
	{"info-page", 245933},
	{"keypad-key", 203303},
	{"letter-show", 177850},
	{"nav-screen", 259045},
	{"nor-move", 103870},
	{"popup-draw", 191256},
	{"popup-motion", 108142},
	{"popup-move", 156688},
	{"popup-scroll", 200246},
	{"rename-key", 155177},
	{"row-copy14", 6977},
	{"sd-count", 120617},
	{"sd-folder", 152303},
	{"sd-move", 141890},
	{"settings-edit", 147684},
	{"settings-move", 181358},
	{"settings-scroll", 181358},
	{"shots-ahead", 210679},
	{"shots-overlay", 106736},
	{"shots-show", 91315},
	{"text-clear-row", 1660},
	{"text-clear-row14", 2733},
	{"text-clear-row8-unaligned", 2401},
	{"text-glyph", 387},
	{"text-heading20", 8825},
	{"text-row14-25", 9587},
	{"text-row14-40", 14253},
	{"text-row38", 9972},
	{"ui-close", 139895},
	{"ui-dialog-motion", 191256},
	{"ui-dialog-open", 170590},
	{"ui-fade", 38583},
	{"ui-list-scroll", 137081},
	{"ui-list-step", 93437},
	{"ui-open", 259045},
	{"ui-page-change", 152189},
	{"ui-transition", 248600},
	{"ui-wipe", 129240},
};

#endif
