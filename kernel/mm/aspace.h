#ifndef MM_ASPACE_H
#define MM_ASPACE_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include <arch/mmu.h>

/*
 * A user address space: page tables plus the list of regions mapped in
 * them. All memory is "anonymous": private, zero-filled frames.
 *
 * Functions are safe to call from several threads; the space does not have
 * to be the active one.
 */
struct address_space;

struct address_space *aspace_create(void);

/* Unmaps everything and frees all memory. The space must not be active on
 * any CPU. */
void aspace_destroy(struct address_space *space);

/* For the scheduler. */
struct arch_address_space *aspace_arch(struct address_space *space);

/*
 * Maps `pages` zero-filled pages at `start` (page aligned, inside the user
 * range, not overlapping an existing region). `flags` are ARCH_MAP_WRITE
 * and/or ARCH_MAP_EXEC; user access is implied.
 */
bool aspace_map(struct address_space *space, uintptr_t start, size_t pages, unsigned flags);

/* Like aspace_map(), at an address the kernel picks. Returns 0 on failure. */
uintptr_t aspace_map_anywhere(struct address_space *space, size_t pages, unsigned flags);

/*
 * Maps frames the caller already owns (shared memory) at an address the
 * kernel picks, writable. The address space does not take ownership: when
 * the region is unmapped, or the space destroyed, the frames are left alone
 * and release(context) is called once instead. Returns 0 on failure, in
 * which case release is not called.
 */
uintptr_t aspace_map_frames(struct address_space *space, const phys_addr_t *frames, size_t pages,
                            void (*release)(void *context), void *context);

/* Removes a region created by one of the calls above; `start` and `pages`
 * must match it exactly. */
bool aspace_unmap(struct address_space *space, uintptr_t start, size_t pages);

/*
 * Copies into the space's memory, regardless of page permissions and of
 * which space is active. Used to load programs. Fails if any byte of the
 * destination is unmapped.
 */
bool aspace_write(struct address_space *space, uintptr_t destination, const void *source, size_t length);

/* True if [address, address + length) is mapped for user access (and
 * writable, if `write`). Checks what a system call may touch. */
bool aspace_user_range_valid(struct address_space *space, uintptr_t address, size_t length, bool write);

#endif
