/* Two-segment layout for vita-elf-create: text (RX) + data (RW).
   .vitalink.* stub sections are left as orphans so each library keeps
   its own section name (vita-elf-create derives the library from it). */
OUTPUT_FORMAT("elf32-littlearm")
OUTPUT_ARCH(arm)
ENTRY(_start)

PHDRS
{
	text PT_LOAD FLAGS(5);
	data PT_LOAD FLAGS(6);
}

SECTIONS
{
	. = 0x81000000;
	.text : {
		KEEP(*(.text.start))
		*(.text .text.* .gnu.linkonce.t.*)
		*(.glue_7) *(.glue_7t) *(.vfp11_veneer) *(.v4_bx)
	} :text
	.rodata : { *(.rodata .rodata.* .gnu.linkonce.r.*) } :text
	.ARM.extab : { *(.ARM.extab* .gnu.linkonce.armextab.*) } :text
	.ARM.exidx : {
		__exidx_start = .;
		*(.ARM.exidx* .gnu.linkonce.armexidx.*)
		__exidx_end = .;
	} :text

	. = ALIGN(0x10000);
	.data : {
		*(.data .data.* .gnu.linkonce.d.*)
		. = ALIGN(4);
		__preinit_array_start = .; KEEP(*(.preinit_array)) __preinit_array_end = .;
		__init_array_start = .; KEEP(*(SORT(.init_array.*))) KEEP(*(.init_array)) __init_array_end = .;
		__fini_array_start = .; KEEP(*(SORT(.fini_array.*))) KEEP(*(.fini_array)) __fini_array_end = .;
	} :data
	.bss (NOLOAD) : {
		__bss_start__ = .;
		*(.bss .bss.* .gnu.linkonce.b.*) *(COMMON)
		. = ALIGN(4);
		__bss_end__ = .;
		end = .; _end = .;
	} :data

	/DISCARD/ : { *(.note.GNU-stack) *(.comment) }
}
