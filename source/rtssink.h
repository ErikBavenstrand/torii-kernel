// /torii/rts.bin: the file every launch's table names as the save-state
// file (fattable.h), so the FPGA's write-back of cart SRAM's state buffer
// lands in a file the kernel owns, whatever starts it. Its contents mean
// nothing: Torii's states are filed by the kernel (statestore.h).
#ifndef TORII_RTSSINK_H
#define TORII_RTSSINK_H

#include <gba_base.h>

#define RTS_SINK_FILE "/torii/rts.bin"

// Makes the file (once: its clusters are allocated, not written), checks its
// size and cluster chain, and writes its runs into FAT_table_buffer from
// 0x300, keeping a copy for rts_sink_runs. Call it after init_FAT_table.
// 1 if done; 0 if the file can't be made or read, and the launch must stop.
int rts_sink_prepare(void);

// The CRC32 of the runs rts_sink_prepare last wrote (fat_table_rts_crc), or
// NULL when it hasn't
const u32 *rts_sink_crc(void);

#endif
