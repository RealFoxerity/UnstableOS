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

    # provide symbols the libc init needs
    # we won't call it, but we need the symbols to link
    PROVIDE_HIDDEN (__preinit_array_start = .);
    PROVIDE_HIDDEN (__preinit_array_end = .);
    PROVIDE_HIDDEN (__init_array_start = .);
    PROVIDE_HIDDEN (__init_array_end = .);
    PROVIDE_HIDDEN (__fini_array_start = .);
    PROVIDE_HIDDEN (__fini_array_end = .);

	/DISCARD/ :
	{
		*(.note .note.*)
	}
}
