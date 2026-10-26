/* The in-game overlay (docs/design/v0.8/ingame-menu.md), linked to run from
 * the EWRAM window it borrows (INGAME_EWRAM in source/ingame_abi.h). The
 * stub loads the image (header, code, data) there; bss and the stack follow
 * it in the window, which ends at __ov_stack_top. */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(ov_entry)

MEMORY {
	window : ORIGIN = 0x02000000, LENGTH = 0x6000
}

SECTIONS {
	.header : { KEEP(*(.ov_header)) } > window
	.text : { *(.text .text.*) *(.glue_7) *(.glue_7t) *(.vfp11_veneer) *(.v4_bx) } > window
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

	ASSERT(__ov_image_end - 0x02000000 <= 0x8000, "the overlay's image passes INGAME_IMAGE_MAX")

	/DISCARD/ : { *(.ARM.exidx*) *(.ARM.extab*) *(.comment) *(.note*) }
}
