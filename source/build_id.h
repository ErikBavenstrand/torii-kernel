// The kernel's build ID: the CRC32 of its ROM image (ezkernelnew.gba, the
// upgrade file's first part up to __rom_end__) with the ID's own word read
// as 0. tools/build_id.py writes it after linking (source/build_id_word.s), and
// writes it at the top of ezkernelnew.sym, so tools/logs.py can tell which
// symbols name a log's addresses. The boot line, Diagnostics and crash dumps
// print it. 0 means the post-link step never ran.
#ifndef TORII_BUILD_ID_H
#define TORII_BUILD_ID_H

#include <gba_base.h>

extern const u32 torii_build_id;

// The CRC32 of the kernel's ROM, as tools/build_id.py computes it: equal to
// torii_build_id unless the ROM changed after the build.
u32 build_id_compute(void);

#endif
