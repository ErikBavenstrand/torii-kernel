// pReadCache, 128 KB of EWRAM, is the kernel's scratch buffer. It holds
// nothing between two passes of a menu loop: each owner below borrows it for
// one operation and needs nothing in it afterwards. Its owners, by offset:
// - READCACHE_SORT_WORK: sort_by_name's work buffer, while a folder is listed.
// - [0x2000, ...): the cheat screen's parsed list, and READCACHE_CHEAT_WORK
//   at the end, its line and value buffers (showcht.c), until it returns.
// - All of it: bulk SD, NOR, SRAM and PSRAM transfers inside one operation:
//   starting a game (chunks, patch files, the save), writing a game to NOR,
//   the NOR list's record, save backup and restore, GameID2cht.bin,
//   Diagnostics, and the unit tests (in Diagnostics, and at boot in a test build).
// - READCACHE_STAGE: the engine's asset jobs (engine/stage.h), within one job
//   step: an SD chunk on its way to PSRAM, a decoded cover, the path being
//   opened. Nothing in it outlives the step, and steps run only from
//   jobs_run, never inside one of the operations above, so it can overlap
//   them all. An owner that wants to keep data here across frames that run
//   jobs must stop the asset jobs first (cover_shutdown).
// - READCACHE_STAGE [0, 0x2000), from ui_frame: the SD theme's wallpaper is
//   decoded there when it is put back after a push or pop (ui/ui_sdtheme.c),
//   at most 8 KB (plus up to 17 bytes for a damaged LZ77 stream), so it ends
//   at the cheat list (+0x2000), the one owner live across frames.
// - READCACHE_SAVE, 64 KB: the SRAM on its way to a .sav (Save_savefile),
//   clear of everything a frame writes, so its progress dialog can draw.
// - READCACHE_THEME, with READCACHE_STAGE: loading an SD theme (ui/sdtheme.h),
//   within one call: the reader, the parsed theme.ini, the decoded wallpaper.
#define MAX_pReadCache_size 0x20000
#define READCACHE_STAGE 0x0
#define READCACHE_STAGE_SIZE 0x8000
#define READCACHE_SORT_WORK 0x800  // SORT_WORK_SIZE bytes
#define READCACHE_THEME 0x14C00
#define READCACHE_THEME_SIZE 0x800
#define READCACHE_SAVE 0x10000  // 64 KB, to the end
#define MAX_files 0x200
#define MAX_folder 0x100
#define MAX_NOR 0x40

#define MAX_path_len 0x100

#define FAT_table_size 0x400
//#define FAT_table_SAV_offset 0x200
#define FAT_table_RTS_offset 0x300

#define DEBUG

#define RGB(r, g, b) ((r) + ((g) << 5) + ((b) << 10))

#define PSRAMBase_S98 (void *)0x08800000
#define FlashBase_S98 (u32)0x09000000
#define FlashBase_S98_end (u32)0x09800000

#define SAVE_sram_base (u32)0x0E000000
#define SRAMSaver (u32)0x0E000000

#define UNBCD(x) (((x) & 0xF) + (((x) >> 4) * 10))
#define _BCD(x) ((((x) / 10) << 4) + ((x) % 10))
#define _YEAR 0
#define _MONTH 1
#define _DAY 2
#define _WKD 3
#define _HOUR 4
#define _MIN 5
#define _SEC 6

#define _UnusedVram (void *)0x06012c00

#define FlashBase_S71 (void *)0x08000000
// Torii: where the kernel NOR may be written, and the boot loader it must
// never reach: bootloader.h
#include "bootloader.h"

#define SAVE_info_offset 0x790000
#define NOR_info_offset 0x7A0000
#define SET_info_offset 0x7B0000

#define SAVER_FOLDER "/SAVER"

#define DMA_COPY_MODE 0X1
#define SET_PARAMETER_MODE 0x2

// image.bin, at 0x08100000, holds EZ-Flash's 16bpp menu pictures, which the
// kernel no longer draws (Diagnostics' LZ77 timing still reads one), then the
// FPGA firmware Check_FW_update writes (LX9_ and LX16_newomega_top_bin_address).

#define assress_language 0
#define assress_v_reset 1
#define assress_v_rts 2
#define assress_v_sleep 3
#define assress_v_cheat 4
#define assress_edit_sleephotkey_0 5
#define assress_edit_sleephotkey_1 6
#define assress_edit_sleephotkey_2 7
#define assress_edit_rtshotkey_0 8
#define assress_edit_rtshotkey_1 9
#define assress_edit_rtshotkey_2 10
#define assress_engine_sel 11
#define assress_show_Thumbnail 12
#define assress_game_clock 13 //games can read the cart's clock
#define assress_auto_save_sel 14
#define assress_ModeB_INIT 15
#define assress_led_open_sel 16
#define assress_Breathing_R 17
#define assress_Breathing_G 18
#define assress_Breathing_B 19
#define assress_SD_R 20
#define assress_SD_G 21
#define assress_SD_B 22
//23 and 24 are DS Style's reset and backup toggles
#define assress_game_rumble 25 //games can use the cart's rumble motor
#define assress_logging 26 //Torii: LOGGING level, source/tlog.h (0xFFFF: off)
#define assress_dim 27 //Torii: DIM, idle minutes before the screen dims, 1-60 (else off), source/ui/ui.h
#define assress_theme 28 //Torii: THEME, 1 light, 2 the SD theme in /torii/theme.txt (else dark), source/ui/ui.h
#define assress_accent 29 //Torii: ACCENT, 1 + an index into ui_accents (else the theme's own)

#define assress_saveMODE 64
#define assress_max 65

#define LX9_FW_built_in_ver 5
#define LX16_FW_built_in_ver 7

#define LX9_newomega_top_bin_address (void *)0x08195000
#define LX9_newomega_top_bin_size 0x535bc////0x536d0//0x534A8//
#define LX9_wirte_address 0x80000
#define LX9_FW_crc32 0x7E33C4FB //0x76352215:fw4

#define LX16_newomega_top_bin_address (void *)0x08200000
#define LX16_newomega_top_bin_size 0x716A0
#define LX16_wirte_address 0x100000
#define LX16_FW_crc32 0x709EF09C