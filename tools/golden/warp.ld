SECTIONS { .text 0x00000000 : { *(.text.entry) *(.text*) *(.rodata*) } /DISCARD/ : { *(*) } }
