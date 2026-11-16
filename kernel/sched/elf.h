#ifndef SCHED_ELF_H
#define SCHED_ELF_H

#include <stddef.h>
#include <stdint.h>

struct address_space;

/*
 * Loads a statically linked 64-bit ELF executable from memory into an
 * address space: maps each loadable segment with its permissions and copies
 * its contents. Stores the program's entry point in *entry.
 *
 * Returns 0 or a negative error code. On failure the address space may be
 * partly populated and should be destroyed.
 */
int elf_load(struct address_space *space, const void *image, size_t size, uintptr_t *entry);

#endif
