@ The kernel's build ID (source/build_id.h). Linked as 0, then
@ tools/build_id.py writes the CRC32 of the kernel's ROM image into it,
@ counting this word as 0. The word keeps its place and size, so the ID never
@ changes the code it identifies. It is data in assembly so that GCC can't
@ fold the 0 into the code that reads it.

	.section .rodata.torii_build_id,"a",%progbits
	.align 2
	@ tools/build_id.py finds the ID as the word after this text
	.ascii	"TORII BUILD ID:\0"
	.global torii_build_id
torii_build_id:
	.word	0
