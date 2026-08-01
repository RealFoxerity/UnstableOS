ENTRY(_start)

PHDRS
{
	text PT_LOAD;
	rodata PT_LOAD;
	data PT_LOAD;
}

SECTIONS
{
	. = 1280K; # slightly above 1M (1.25M) to allow for address wraparound in vm86

	. = ALIGN(CONSTANT(MAXPAGESIZE));
	KERNEL_START = .;
	KERNEL_SECT_TEXT_START = ABSOLUTE(.);

	.text :
	{
		*(.multiboot)
		*(.text .text.*)
	} : text

	. = ALIGN(CONSTANT(MAXPAGESIZE));
	KERNEL_SECT_TEXT_END = ABSOLUTE(.);
	KERNEL_SECT_RODATA_START = ABSOLUTE(.);

	.rodata :
	{
		*(.rodata .rodata.*)
	} : rodata

	. = ALIGN(CONSTANT(MAXPAGESIZE));
	KERNEL_SECT_RODATA_END = ABSOLUTE(.);
	KERNEL_SECT_DATA_START = ABSOLUTE(.);

	.data :
	{
		*(.data .data.*)
	} : data

	. = ALIGN(CONSTANT(MAXPAGESIZE));
	KERNEL_SECT_DATA_END = ABSOLUTE(.);
	KERNEL_SECT_BSS_START = ABSOLUTE(.);

	.bss :
	{
		*(.bss .bss.*)
		*(COMMON)
	} : data

	. = ALIGN(CONSTANT(MAXPAGESIZE));
	KERNEL_SECT_DATA_END = ABSOLUTE(.);

	KERNEL_END = .;

	/DISCARD/ :
	{
		*(.note .note.*)
	}
}
