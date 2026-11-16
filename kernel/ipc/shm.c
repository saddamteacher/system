/*
 * State: a shm object is immutable after creation except for its reference
 * count, which is atomic. One reference is held by each open file and each
 * mapping.
 */
#include <ipc/shm.h>

#include <stdatomic.h>

#include <saddev/errno.h>
#include <saddev/syscall.h>

#include <arch/mmu.h>
#include <core/string.h>
#include <fs/vfs.h>
#include <mm/aspace.h>
#include <mm/heap.h>
#include <mm/pmm.h>

struct shm {
    atomic_uint references;
    size_t pages;
    phys_addr_t frames[];
};

static void shm_release(void *context)
{
    struct shm *shm = context;
    if (atomic_fetch_sub(&shm->references, 1) != 1) {
        return;
    }
    for (size_t i = 0; i < shm->pages; i++) {
        pmm_free(shm->frames[i]);
    }
    kfree(shm);
}

static void shm_file_close(struct file *file)
{
    shm_release(file->context);
}

static const struct file_ops shm_ops = {
    .close = shm_file_close,
};

int shm_is_object(const struct file *file)
{
    return file->ops == &shm_ops;
}

int shm_create(size_t size, struct file **result)
{
    if (size == 0 || size > SD_SHM_MAX_SIZE) {
        return -EINVAL;
    }
    size_t pages = (size + ARCH_PAGE_SIZE - 1) / ARCH_PAGE_SIZE;

    struct shm *shm = kmalloc(sizeof(*shm) + pages * sizeof(phys_addr_t));
    if (shm == NULL) {
        return -ENOMEM;
    }
    atomic_init(&shm->references, 1);
    shm->pages = 0;
    for (size_t i = 0; i < pages; i++) {
        phys_addr_t frame = pmm_alloc();
        if (frame == 0) {
            shm_release(shm); /* frees the frames obtained so far */
            return -ENOMEM;
        }
        /* The first process to map this must not see old memory contents. */
        memset(arch_phys_to_virt(frame), 0, ARCH_PAGE_SIZE);
        shm->frames[shm->pages++] = frame;
    }

    struct file *file = vfs_file_create(&shm_ops, shm, SD_TYPE_SHM);
    if (file == NULL) {
        shm_release(shm);
        return -ENOMEM;
    }
    file->size = pages * ARCH_PAGE_SIZE;
    *result = file;
    return 0;
}

uintptr_t shm_map(struct file *file, struct address_space *space)
{
    struct shm *shm = file->context;

    /* The mapping's own reference, dropped by shm_release when the region
     * is unmapped or its address space destroyed. */
    atomic_fetch_add(&shm->references, 1);
    uintptr_t address = aspace_map_frames(space, shm->frames, shm->pages, shm_release, shm);
    if (address == 0) {
        shm_release(shm);
    }
    return address;
}
