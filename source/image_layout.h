#ifndef IMAGE_LAYOUT_H
#define IMAGE_LAYOUT_H

// What ezkernelnew.bin holds after the kernel's 1 MB, at the ROM addresses
// the kernel reads it from. tools/omega_image.py reads this file and builds
// that part from EZ-Flash's image.bin, and tests/image_test.py checks the two
// agree; docs/research/omega-de-hardware.md has the layout and its history.
// Each part is copied from image.bin with the bytes after it up to a whole
// 256-byte page: the firmware update sends whole pages, so the FPGA's flash
// gets the same bytes as from the official layout. image.bin's 16bpp menu
// pictures, which nothing draws since v0.3, are left out.

#define IMAGE_ADDRESS 0x08100000

// FPGA firmware, written by Check_FW_update (Ezcard_OP.c) to the FPGA's
// config flash at *_wirte_address, and CRC-checked by Diagnostics
#define LX9_newomega_top_bin_address (void *)0x08100000
#define LX9_newomega_top_bin_size 0x535bc
#define LX9_FW_crc32 0x7E33C4FB

#define LX16_newomega_top_bin_address (void *)0x08154000
#define LX16_newomega_top_bin_size 0x716A0
#define LX16_FW_crc32 0x709EF09C

// 16 KB of the official SD list's background picture, which Diagnostics'
// LZ77 timing packs and unpacks, so its figure compares with older reports
#define LZ77_SAMPLE_address (void *)0x081C6000
#define LZ77_SAMPLE_size 0x4000
#define LZ77_SAMPLE_crc32 0xD003873B

// The end of the upgrade file: EZ-Flash's loader installs at most 3 MB of it
#define IMAGE_END 0x081CA000

#endif
