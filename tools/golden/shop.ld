SECTIONS { .text 0x08F00000 : { *(.text.entry) *(.text*) *(.rodata*) } /DISCARD/ : { *(*) } }
