/*
 * Kernel image layout.
 *
 * The kernel is linked into the top 2 GiB of the address space ("higher
 * half"). Limine maps it there before jumping to _start, so the kernel never
 * runs at its physical load address. The top 2 GiB is what -mcmodel=kernel
 * assumes: every symbol fits in a sign-extended 32-bit displacement.
 */
OUTPUT_FORMAT(elf64-x86-64)
ENTRY(_start)

/* One segment per permission set, so each can later be mapped with the
 * tightest page protection (W^X). FLAGS bits: 1 = execute, 2 = write, 4 = read. */
PHDRS
{
    text   PT_LOAD FLAGS(5);
    rodata PT_LOAD FLAGS(4);
    data   PT_LOAD FLAGS(6);
}

SECTIONS
{
    . = 0xffffffff80000000;
    __kernel_start = .;

    /* The __*_start/__*_end symbols let the kernel map each part of its own
     * image with the right permissions when it builds its page tables. */
    __text_start = .;
    .text : {
        *(.text .text.*)
    } :text

    /* Segments with different permissions must not share a page. */
    . = ALIGN(CONSTANT(MAXPAGESIZE));
    __text_end = .;

    __rodata_start = .;
    .rodata : {
        *(.rodata .rodata.*)
    } :rodata

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    __rodata_end = .;

    __data_start = .;
    .data : {
        *(.data .data.*)

        /* Limine finds its requests by scanning the image. KEEP stops the
         * linker from discarding them: nothing in the kernel references the
         * markers, only the bootloader reads them. */
        KEEP(*(.limine_requests_start))
        KEEP(*(.limine_requests))
        KEEP(*(.limine_requests_end))
    } :data

    /* .bss must come last in its segment: it occupies memory but no file
     * bytes, and the loader zero-fills the tail of the segment. */
    .bss : {
        *(.bss .bss.*)
        *(COMMON)
    } :data

    . = ALIGN(CONSTANT(MAXPAGESIZE));
    __data_end = .;
    __kernel_end = .;

    /DISCARD/ : {
        *(.eh_frame*)
        *(.note .note.*)
        *(.comment)
    }
}
