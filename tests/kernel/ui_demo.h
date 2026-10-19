// The UI demo (test kernel only): see ui_demo.c.
#ifndef TORII_UI_DEMO_H
#define TORII_UI_DEMO_H

// Runs the demo; never returns.
void ui_demo(void) __attribute__((noreturn));
// The same in the SD theme /torii/themes/<sd_theme> (ui/sdtheme.h), or the
// dark theme if it doesn't load (tests/shots/ui-sd-theme.opts).
void ui_demo_theme(const char *sd_theme) __attribute__((noreturn));

#endif
