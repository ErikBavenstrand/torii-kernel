// The v0.3 tile engine (docs/plans/v0.3-decisions.md): mode 0, a fixed
// 60 fps frame loop, shadow hardware state committed in VBlank, jobs for
// slow work, and allocators for VRAM, palettes and affine matrices.
//
//   engine/frame.h   the frame loop, idle detection, frame-cycles figures
//   engine/gfx.h     shadow registers, palettes and OAM; the VRAM queue
//   engine/jobs.h    state-machine jobs within a cycle budget
//   engine/res.h     VRAM, palette and affine allocators, view arenas
//   engine/budget.h  checking a layout's needs up front
//   engine/cycles.h  the TM2/TM3 cycle counter
//   engine/pool.h    the range allocator under res.h (hardware-free)
#ifndef TORII_ENGINE_H
#define TORII_ENGINE_H

#include "budget.h"
#include "cycles.h"
#include "frame.h"
#include "gfx.h"
#include "jobs.h"
#include "res.h"

// Resets the shadow state, the pools and the jobs, then frame_init. The
// screen stays blank (forced blank) until a scene sets DISPCNT.
void engine_init(void);

#endif
