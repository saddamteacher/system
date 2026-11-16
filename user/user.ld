/*
 * Layout of a user program. Programs are statically linked at a fixed
 * address in the lower half of the address space. As in the kernel, each
 * permission set gets its own page-aligned segment so the loader can map
 * code read-only and data non-executable.
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

PHDRS
{
    text   PT_LOAD FLAGS(5); /* read + execute */
    rodata PT_LOAD FLAGS(4); /* read */
    data   PT_LOAD FLAGS(6); /* read + write */
}

SECTIONS
{
    . = 0x400000;

    .text : { *(.text .text.*) } :text
    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .rodata : { *(.rodata .rodata.*) } :rodata
    . = ALIGN(CONSTANT(MAXPAGESIZE));

    .data : { *(.data .data.*) } :data
    .bss : { *(.bss .bss.*) *(COMMON) } :data

    /DISCARD/ : { *(.eh_frame*) *(.note .note.*) *(.comment) }
}
