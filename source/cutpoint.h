// Power-cut markers for the tests. cutpoint() goes between the FatFs steps of
// an operation that must survive a power cut (the save copy, a slot rename).
// It does nothing but log "cutpoint N: NAME" at debug level, N counting from 1
// since the boot; `mgba-shot -X N` powers the emulated cart off at the Nth
// such line, with the SD image, SRAM and kernel NOR as they stand, and
// tests/cutsweep.sh runs a scenario with a cut at each one in turn.
#ifndef TORII_CUTPOINT_H
#define TORII_CUTPOINT_H

// Marks a step boundary; NAME says which (fixed text, no format). Safe to
// call from IWRAM code and anywhere tlog_debug is.
void cutpoint(const char *name);

#endif
