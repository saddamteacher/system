/*
 * ELF is the executable format our own toolchain produces. A file starts
 * with a header that locates the "program headers"; each program header of
 * type LOAD says: put `file_size` bytes from file offset `offset` at
 * virtual address `vaddr`, make the region `memory_size` bytes long (the
 * rest is zero: that is .bss), with these permissions.
 *
 * Everything in the file is untrusted input and is validated before use.
 */
#include <sched/elf.h>

#include <saddev/errno.h>

#include <core/string.h>
#include <mm/aspace.h>

#define ELF_MAGIC "\x7f" "ELF"
#define ELF_CLASS_64 2
#define ELF_DATA_LITTLE_ENDIAN 1
#define ELF_TYPE_EXECUTABLE 2
#define ELF_MACHINE_X86_64 62

#define PROGRAM_TYPE_LOAD 1
#define PROGRAM_FLAG_EXECUTE 1
#define PROGRAM_FLAG_WRITE 2

struct elf_header {
    uint8_t magic[4];
    uint8_t class;
    uint8_t data;
    uint8_t ident_version;
    uint8_t ident_padding[9];
    uint16_t type;
    uint16_t machine;
    uint32_t version;
    uint64_t entry;
    uint64_t program_header_offset;
    uint64_t section_header_offset;
    uint32_t flags;
    uint16_t header_size;
    uint16_t program_header_size;
    uint16_t program_header_count;
    uint16_t section_header_size;
    uint16_t section_header_count;
    uint16_t section_name_index;
};

struct elf_program_header {
    uint32_t type;
    uint32_t flags;
    uint64_t offset;
    uint64_t vaddr;
    uint64_t paddr;
    uint64_t file_size;
    uint64_t memory_size;
    uint64_t alignment;
};

static bool header_valid(const struct elf_header *header, size_t size)
{
    if (size < sizeof(*header) || memcmp(header->magic, ELF_MAGIC, 4) != 0) {
        return false;
    }
    if (header->class != ELF_CLASS_64 || header->data != ELF_DATA_LITTLE_ENDIAN
        || header->type != ELF_TYPE_EXECUTABLE || header->machine != ELF_MACHINE_X86_64) {
        return false;
    }
    if (header->program_header_size != sizeof(struct elf_program_header)) {
        return false;
    }
    /* The whole program header table must lie inside the file. */
    uint64_t table_size = (uint64_t)header->program_header_count * sizeof(struct elf_program_header);
    return header->program_header_offset <= size && table_size <= size - header->program_header_offset;
}

static int load_segment(struct address_space *space, const uint8_t *image, size_t size,
                        const struct elf_program_header *segment)
{
    if (segment->file_size > segment->memory_size || segment->offset > size
        || segment->file_size > size - segment->offset) {
        return -ENOEXEC;
    }
    if (segment->memory_size == 0) {
        return 0;
    }
    uint64_t end = segment->vaddr + segment->memory_size;
    if (end < segment->vaddr) {
        return -ENOEXEC; /* wraps around the address space */
    }
    uintptr_t first_page = segment->vaddr & ~(ARCH_PAGE_SIZE - 1);
    uintptr_t last_page_end = (end + ARCH_PAGE_SIZE - 1) & ~(ARCH_PAGE_SIZE - 1);

    unsigned flags = 0;
    if (segment->flags & PROGRAM_FLAG_WRITE) {
        flags |= ARCH_MAP_WRITE;
    }
    if (segment->flags & PROGRAM_FLAG_EXECUTE) {
        flags |= ARCH_MAP_EXEC;
    }
    /* aspace_map() rejects addresses outside user space and overlaps with
     * earlier segments, so a hostile file cannot map over anything. Pages
     * arrive zero-filled, which takes care of .bss. */
    if (!aspace_map(space, first_page, (last_page_end - first_page) / ARCH_PAGE_SIZE, flags)) {
        return -ENOEXEC;
    }
    if (!aspace_write(space, segment->vaddr, image + segment->offset, segment->file_size)) {
        return -ENOEXEC;
    }
    return 0;
}

int elf_load(struct address_space *space, const void *image, size_t size, uintptr_t *entry)
{
    const struct elf_header *header = image;
    if (!header_valid(header, size)) {
        return -ENOEXEC;
    }

    const uint8_t *bytes = image;
    for (unsigned i = 0; i < header->program_header_count; i++) {
        /* Copy the entry: nothing guarantees it is aligned in the file. */
        struct elf_program_header segment;
        memcpy(&segment, bytes + header->program_header_offset + i * sizeof(segment), sizeof(segment));
        if (segment.type != PROGRAM_TYPE_LOAD) {
            continue;
        }
        int status = load_segment(space, bytes, size, &segment);
        if (status < 0) {
            return status;
        }
    }
    *entry = header->entry;
    return 0;
}
