/* The in-game overlay (docs/design/v0.8/ingame-menu.md), linked to run from
 * the EWRAM window it borrows (INGAME_EWRAM in source/ingame_abi.h). The
 * stub loads the image (header, code, data) there; bss and the stack follow
 * it in the window, which ends at __ov_stack_top. */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(ov_entry)

MEMORY {
	window : ORIGIN = 0x02000000, LENGTH = 0x6000
	/* The game's IWRAM a screenshot borrows for its code, after its table
	 * and buffer (INGAME_SHOT_IWRAM, ov.c's struct shot_scratch) */
	iwram : ORIGIN = 0x03000A00, LENGTH = 0x800
}

SECTIONS {
	.header : { KEEP(*(.ov_header)) } > window
	.text : { EXCLUDE_FILE(*cheatsearch.o *shotpack.o) *(.text .text.*) *(.glue_7) *(.glue_7t) *(.vfp11_veneer) *(.v4_bx) } > window
	.rodata : { *(.rodata .rodata.*) } > window
	.data : { *(.data .data.*) . = ALIGN(4); } > window
	__ov_image_end = .;

	.bss (NOLOAD) : {
		__ov_bss_start = .;
		*(.bss .bss.*) *(COMMON)
		. = ALIGN(4);
		__ov_bss_end = .;
	} > window
	.stack (NOLOAD) : {
		. = . + 0x400;
		. = ALIGN(8);
		__ov_stack_top = .;
	} > window

	/* The cheat search's extension (ov.c's EXT): after the window, which it
	 * extends while loaded, and after the image in the file, which the stub
	 * doesn't load: the overlay loads it itself */
	.ext __ov_stack_top : AT(__ov_image_end) {
		__ov_ext_start = .;
		*(.ext.text)
		*cheatsearch.o(.text .text.cheatsearch_digits .text.cheatsearch_decimal .text.cheatsearch_edit .text.cheatsearch_fill .text.cheatsearch_get .text.cheatsearch_count)
		*(.ext.data)
		. = ALIGN(4);
		__ov_ext_end = .;
	}
	__ov_ext_load = LOADADDR(.ext);
	/* The screenshot's packer and the cheat search's compare, ARM: run from
	 * the IWRAM they borrow (ov.c's iwram_borrow), their bytes after the
	 * extension's in the image */
	.fastext : AT(__ov_ext_load + SIZEOF(.ext)) {
		__ov_fastext_start = .;
		*shotpack.o(.text .text.*)
		*cheatsearch.o(.text.cheatsearch_narrow)
		. = ALIGN(4);
		__ov_fastext_end = .;
	} > iwram
	__ov_fastext_load = LOADADDR(.fastext);

	ASSERT(__ov_fastext_load + SIZEOF(.fastext) - 0x02000000 <= 0x6000, "the overlay's image passes INGAME_IMAGE_MAX")
	ASSERT(__ov_fastext_end - 0x03000000 <= 0x1200, "the IWRAM a pass borrows passes INGAME_BACKUP_SHOT_BYTES")
	ASSERT(__ov_ext_end - 0x02000000 <= 0x6000, "the window and the extension pass INGAME_WINDOW_MAX")

	/DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
