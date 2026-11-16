/*
 * x86_64 4-level paging.
 *
 * A virtual address is split into four 9-bit table indices and a 12-bit
 * offset. Translation walks four tables (PML4 -> PDPT -> PD -> PT), each a
 * 4 KiB frame holding 512 entries of 8 bytes. An entry holds the physical
 * address of the next table (or, in the last level, of the data frame) plus
 * permission bits. The register CR3 holds the physical address of the PML4.
 *
 * Tables are addressed physically but the kernel can only dereference
 * virtual addresses, so every table access goes through the direct map.
 *
 * State:
 *   frame_ops, kernel_space  set once in arch_mmu_init() during
 *                            single-threaded boot.
 *   the page tables          modified by map/unmap; the caller serialises
 *                            access per address space (see <arch/mmu.h>).
 */
#include <arch/mmu.h>

#include <arch/boot.h>

#include "boot/boot.h"
#include "msr.h"

#define ENTRIES_PER_TABLE 512
#define TABLE_LEVELS 4

#define PTE_PRESENT    (1ULL << 0)
#define PTE_WRITABLE   (1ULL << 1)
#define PTE_USER       (1ULL << 2)
#define PTE_NO_EXECUTE (1ULL << 63)
/* Bits 12-51 of an entry hold the frame address. */
#define PTE_ADDRESS_MASK 0x000FFFFFFFFFF000ULL

/* Extended Feature Enable Register and its "no-execute enable" bit. Without
 * it the CPU treats PTE_NO_EXECUTE as a reserved bit and faults. */
#define MSR_EFER 0xC0000080u
#define EFER_NXE (1ULL << 11)
/* CR0 write protect: ring 0 honours read-only pages too. Without it the
 * kernel could silently write to its own code. */
#define CR0_WP (1ULL << 16)

#define CPUID_EXTENDED_FEATURES 0x80000001u
#define CPUID_EDX_NX (1u << 20)

/*
 * Kernel virtual area left for kernel/mm to manage. It sits far above the
 * direct map (which starts at the bottom of the higher half) and below the
 * kernel image (top 2 GiB).
 */
#define KERNEL_DYNAMIC_BASE 0xFFFFC00000000000ULL
#define KERNEL_DYNAMIC_SIZE (4ULL << 30)

struct arch_address_space {
    phys_addr_t root; /* physical address of the PML4 */
};

static const struct arch_mmu_frame_ops *frame_ops;
static struct arch_address_space kernel_space;

/* Section boundaries from boot/linker.ld. Only their addresses matter. */
extern const char __text_start[], __text_end[];
extern const char __rodata_start[], __rodata_end[];
extern const char __data_start[], __data_end[];

void *arch_phys_to_virt(phys_addr_t address)
{
    return (void *)(uintptr_t)(address + boot_hhdm_offset());
}

struct arch_address_space *arch_mmu_kernel_space(void)
{
    return &kernel_space;
}

uintptr_t arch_mmu_kernel_dynamic_base(void)
{
    return KERNEL_DYNAMIC_BASE;
}

size_t arch_mmu_kernel_dynamic_size(void)
{
    return KERNEL_DYNAMIC_SIZE;
}

static uint64_t *table_at(phys_addr_t frame)
{
    return arch_phys_to_virt(frame);
}

/* Level 3 is the PML4 (top), level 0 the page table whose entries map pages. */
static unsigned table_index(uintptr_t virt, unsigned level)
{
    return (virt >> (12 + 9 * level)) & (ENTRIES_PER_TABLE - 1);
}

static phys_addr_t alloc_table(void)
{
    phys_addr_t frame = frame_ops->alloc();
    if (frame != 0) {
        /* A fresh table must not contain stale "present" entries. */
        uint64_t *table = table_at(frame);
        for (unsigned i = 0; i < ENTRIES_PER_TABLE; i++) {
            table[i] = 0;
        }
    }
    return frame;
}

/*
 * Returns the last-level entry for `virt`, or NULL if an intermediate table
 * is missing and `create` is false (or allocating one failed).
 */
static uint64_t *walk(struct arch_address_space *space, uintptr_t virt, bool create, bool user)
{
    uint64_t *table = table_at(space->root);

    for (unsigned level = TABLE_LEVELS - 1; level > 0; level--) {
        uint64_t *entry = &table[table_index(virt, level)];

        if ((*entry & PTE_PRESENT) == 0) {
            if (!create) {
                return NULL;
            }
            phys_addr_t frame = alloc_table();
            if (frame == 0) {
                return NULL;
            }
            /* The CPU combines permissions along the walk: an access is
             * allowed only if every level allows it. Intermediate entries
             * are therefore permissive and the last level decides. */
            *entry = frame | PTE_PRESENT | PTE_WRITABLE | (user ? PTE_USER : 0);
        }
        table = table_at(*entry & PTE_ADDRESS_MASK);
    }
    return &table[table_index(virt, 0)];
}

static uint64_t entry_bits_from_flags(unsigned flags)
{
    uint64_t bits = PTE_PRESENT;
    if (flags & ARCH_MAP_WRITE) {
        bits |= PTE_WRITABLE;
    }
    if (flags & ARCH_MAP_USER) {
        bits |= PTE_USER;
    }
    if ((flags & ARCH_MAP_EXEC) == 0) {
        bits |= PTE_NO_EXECUTE;
    }
    return bits;
}

static unsigned flags_from_entry(uint64_t entry)
{
    unsigned flags = 0;
    if (entry & PTE_WRITABLE) {
        flags |= ARCH_MAP_WRITE;
    }
    if (entry & PTE_USER) {
        flags |= ARCH_MAP_USER;
    }
    if ((entry & PTE_NO_EXECUTE) == 0) {
        flags |= ARCH_MAP_EXEC;
    }
    return flags;
}

bool arch_mmu_map(struct arch_address_space *space, uintptr_t virt, phys_addr_t phys, unsigned flags)
{
    uint64_t *entry = walk(space, virt, true, (flags & ARCH_MAP_USER) != 0);
    if (entry == NULL || (*entry & PTE_PRESENT) != 0) {
        return false;
    }
    /* No TLB flush needed: the CPU does not cache "not present". */
    *entry = (phys & PTE_ADDRESS_MASK) | entry_bits_from_flags(flags);
    return true;
}

bool arch_mmu_unmap(struct arch_address_space *space, uintptr_t virt, phys_addr_t *phys)
{
    uint64_t *entry = walk(space, virt, false, false);
    if (entry == NULL || (*entry & PTE_PRESENT) == 0) {
        return false;
    }
    if (phys != NULL) {
        *phys = *entry & PTE_ADDRESS_MASK;
    }
    *entry = 0;

    /* The CPU caches translations in the TLB. Without this the old mapping
     * would keep working until the entry happens to be evicted. */
    __asm__ volatile("invlpg (%0)" : : "r"(virt) : "memory");
    return true;
}

bool arch_mmu_translate(struct arch_address_space *space, uintptr_t virt, phys_addr_t *phys, unsigned *flags)
{
    uint64_t *entry = walk(space, virt, false, false);
    if (entry == NULL || (*entry & PTE_PRESENT) == 0) {
        return false;
    }
    if (phys != NULL) {
        *phys = (*entry & PTE_ADDRESS_MASK) + (virt & (ARCH_PAGE_SIZE - 1));
    }
    if (flags != NULL) {
        *flags = flags_from_entry(*entry);
    }
    return true;
}

/* Maps [virt, virt + length) to [phys, ...). All three are page aligned. */
static bool map_range(uintptr_t virt, phys_addr_t phys, uint64_t length, unsigned flags)
{
    for (uint64_t offset = 0; offset < length; offset += ARCH_PAGE_SIZE) {
        if (!arch_mmu_map(&kernel_space, virt + offset, phys + offset, flags)) {
            return false;
        }
    }
    return true;
}

static bool map_kernel_section(const char *start, const char *end, unsigned flags)
{
    uintptr_t virt = (uintptr_t)start;
    phys_addr_t phys = boot_kernel_physical_base() + (virt - boot_kernel_virtual_base());
    return map_range(virt, phys, (uint64_t)(end - start), flags);
}

static bool map_kernel_image(void)
{
    /* W^X: nothing is both writable and executable, so a stray write cannot
     * change code and injected data cannot be run. */
    return map_kernel_section(__text_start, __text_end, ARCH_MAP_EXEC)
        && map_kernel_section(__rodata_start, __rodata_end, 0)
        && map_kernel_section(__data_start, __data_end, ARCH_MAP_WRITE);
}

static bool belongs_in_direct_map(enum arch_memory_type type)
{
    switch (type) {
    case ARCH_MEMORY_USABLE:
    case ARCH_MEMORY_BOOT_RECLAIMABLE:
    case ARCH_MEMORY_ACPI_RECLAIMABLE:
    case ARCH_MEMORY_ACPI_NVS:
        return true;
    default:
        /* The kernel image is deliberately left out: a second, writable
         * mapping of the kernel's code would defeat W^X. Device memory and
         * reserved ranges are mapped on demand by whoever drives them. */
        return false;
    }
}

static bool map_direct_map(void)
{
    size_t count = arch_boot_memory_region_count();

    for (size_t i = 0; i < count; i++) {
        struct arch_memory_region region = arch_boot_memory_region(i);
        if (!belongs_in_direct_map(region.type)) {
            continue;
        }
        /* ACPI regions need not be page aligned; cover every page they touch. */
        uint64_t start = region.base & ~(ARCH_PAGE_SIZE - 1);
        uint64_t end = (region.base + region.length + ARCH_PAGE_SIZE - 1) & ~(ARCH_PAGE_SIZE - 1);

        if (!map_range((uintptr_t)arch_phys_to_virt(start), start, end - start, ARCH_MAP_WRITE)) {
            return false;
        }
    }
    return true;
}

static bool cpu_supports_no_execute(void)
{
    uint32_t eax = CPUID_EXTENDED_FEATURES, ebx, ecx = 0, edx;
    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    return (edx & CPUID_EDX_NX) != 0;
}

static void enable_page_protection_features(void)
{
    msr_write(MSR_EFER, msr_read(MSR_EFER) | EFER_NXE);

    uint64_t cr0;
    __asm__ volatile("mov %%cr0, %0" : "=r"(cr0));
    cr0 |= CR0_WP;
    __asm__ volatile("mov %0, %%cr0" : : "r"(cr0));
}

const char *arch_mmu_init(const struct arch_mmu_frame_ops *ops)
{
    frame_ops = ops;

    if (!cpu_supports_no_execute()) {
        return "CPU does not support no-execute pages";
    }
    enable_page_protection_features();

    kernel_space.root = alloc_table();
    if (kernel_space.root == 0) {
        return "out of memory allocating the kernel page table root";
    }
    if (!map_kernel_image()) {
        return "could not map the kernel image";
    }
    if (!map_direct_map()) {
        return "could not build the direct map";
    }

    /* Writing CR3 switches address spaces and flushes the TLB. From the next
     * instruction on, everything the kernel touches (this code, the stack,
     * the tables themselves) must be mapped in the new tables. */
    __asm__ volatile("mov %0, %%cr3" : : "r"(kernel_space.root) : "memory");
    return NULL;
}
