SECTIONS { .text 0x08001A4C : { *(.text.entry) *(.text*) *(.rodata*) } /DISCARD/ : { *(*) } }
