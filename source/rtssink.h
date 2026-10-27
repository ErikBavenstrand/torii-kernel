// /torii/rts.bin: the file every launch's table names as the save-state
// file (fattable.h), so the FPGA's write-back of cart SRAM's state buffer
// lands in a file the kernel owns, whatever starts it. Its contents mean
// nothing: Torii's states are filed by the kernel (statestore.h).
#ifndef TORII_RTSSINK_H
#define TORII_RTSSINK_H

#include <gba_base.h>

#define RTS_SINK_FILE "/torii/rts.bin"

// What rts_sink_prepare returns: 0 when ready, else the error to show
// (Show_error_num): the card has no room for the file, or it didn't read
#define RTS_SINK_NO_ROOM 10
#define RTS_SINK_CARD 1

// Makes the file (once: its clusters are allocated, not written), checks its
// size and that its cluster chain covers it exactly (else makes it again,
// once), writes its runs into FAT_table_buffer from 0x300, keeping their
// CRC for rts_sink_crc, and sends the FPGA a table of them alone (no game's
// runs, parameters only), so cart SRAM's state buffer can be written from
// then on. A file it makes leaves 128 KB free, for the .sav the boot after
// the game makes. Call it after init_FAT_table, before the launch loads the
// save (a refused launch leaves none waiting) or writes the state buffer.
// 0 if done; else RTS_SINK_NO_ROOM or RTS_SINK_CARD, and the launch stops:
// no table goes without the file's runs.
u8 rts_sink_prepare(void);

// The CRC32 of the runs rts_sink_prepare last wrote (fat_table_rts_crc), or
// NULL when it hasn't
const u32 *rts_sink_crc(void);

#endif
