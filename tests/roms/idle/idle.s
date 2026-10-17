@ A ROM that does nothing: no crt0, so it never touches memory, and a dump
@ after launch shows exactly what the kernel left behind (tests/clean_boot.sh).
	.section .text
	.arm
	.global _start
_start:
	b	idle
	.fill	0xBC, 1, 0	@ header, filled in by gbafix
idle:
	b	idle
