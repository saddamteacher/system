# 02 - Memory: frames, paging, heap

Three layers, each built on the one below:

```
kmalloc / kfree          kernel/mm/heap.c    arbitrary sizes
vmm_alloc_pages          kernel/mm/vmm.c     whole pages of kernel virtual memory
arch_mmu_map / unmap     arch/x86_64/paging.c page table format
pmm_alloc / pmm_free     kernel/mm/pmm.c     physical frames
```

## What the bootloader provides

Three more Limine requests (`arch/x86_64/boot`), exposed architecture-neutrally:

- the **memory map** (`arch_boot_memory_region*`): which physical ranges are
  usable RAM, reserved, ACPI, bootloader data, kernel image, framebuffer;
- the **HHDM offset**: Limine maps physical memory at `offset + address`, so
  the kernel can touch any frame before it has page tables of its own
  (`arch_phys_to_virt`);
- the **kernel load address**: the physical address behind the virtual
  address the kernel was linked at.

## Frame allocator (pmm)

One bit per 4 KiB frame, set = in use. For 256 MiB that is 8 KiB.

Initialisation has a chicken-and-egg problem: the bitmap needs memory before
the allocator exists. `pmm_init` takes it directly from the first usable
region large enough, then marks those frames used. The bitmap starts as "all
used" and only ranges the memory map calls *usable* are cleared, so holes and
reserved ranges need no special handling.

Frame 0 is never handed out, which makes physical address 0 a safe
"out of memory" return value.

*Bootloader-reclaimable* memory stays reserved: it holds the memory map and
command line we are still reading, and (until `kmain` switches away) the boot
stack.

A bitmap was chosen over a free list because it can find runs of consecutive
frames (`pmm_alloc_contiguous`). Searches start at a hint and skip fully used
64-bit words at once.

Freeing a frame that is not allocated panics: continuing would later give one
frame to two owners.

## Paging

### The format (arch/x86_64/paging.c)

A 48-bit virtual address is four 9-bit table indices plus a 12-bit offset.
Translation walks PML4 -> PDPT -> PD -> PT; each table is one frame of 512
8-byte entries containing the next frame's address and permission bits. `CR3`
points at the PML4. Tables are reached through the direct map, since only
virtual addresses can be dereferenced.

`walk()` descends the four levels, optionally creating missing tables.
Intermediate entries are permissive (present + writable); the CPU ANDs
permissions along the walk, so the last level decides.

- `arch_mmu_map` refuses to overwrite an existing mapping.
- `arch_mmu_unmap` clears the entry and executes `invlpg`. The CPU caches
  translations in the TLB; without the flush the old mapping keeps working.
- `arch_mmu_translate` returns the frame and the effective flags.

These functions do not lock. The caller serialises changes per address space;
for the kernel space that is the vmm lock.

### The kernel's own tables (arch_mmu_init)

1. Check via CPUID that the CPU has no-execute support; set `EFER.NXE` and
   `CR0.WP` (ring 0 obeys read-only pages).
2. Map the kernel image using the section symbols from `boot/linker.ld`:
   text `R-X`, rodata `R--`, data and bss `RW-`. Nothing is both writable and
   executable (W^X).
3. Build the direct map at the same offset Limine used, so pointers obtained
   earlier stay valid. It covers usable, boot-reclaimable and ACPI memory,
   read/write and never executable. The kernel image is deliberately *not* in
   it: a writable alias of the kernel's code would defeat W^X.
4. Load `CR3`.

Everything is mapped with 4 KiB pages. Using 2 MiB pages for the direct map
would save page tables and TLB entries; that is left for the performance
phase.

Empty intermediate tables are not freed on unmap; virtual ranges are reused,
so the number of tables is bounded by the peak usage.

## Kernel virtual memory (vmm)

`vmm_init` calls `arch_mmu_init`, then manages the *dynamic area* (4 GiB at
`0xffffc00000000000`) with a second bitmap, one bit per virtual page.

- `vmm_alloc_pages(count, flags)`: find a free virtual run, back each page
  with a zeroed frame. Frames are zeroed through the direct map *before* they
  become visible at the new address.
- `vmm_alloc_kernel_stack(pages)`: reserves one extra page below the stack
  and leaves it unmapped. Overflowing the stack then faults immediately
  instead of silently corrupting whatever lies below.

- `vmm_map_physical(address, size)`: maps existing physical memory (device
  registers, firmware tables) into the dynamic area. No frame changes owner;
  unmapping releases only the virtual range.

`kmain` allocates such a stack and switches to it with `arch_call_on_stack`,
leaving the bootloader's stack behind.

## Heap

A first-fit free-list allocator with boundary tags.

Memory comes from the vmm in *arenas* (16 pages by default, larger for big
requests). An arena is tiled by blocks:

```
[arena header][block][block]...[block][end marker]
```

Each block header stores the block's size and the size of the previous
block. Both neighbours are therefore reachable by pointer arithmetic, and a
freed block is merged with free neighbours in constant time. Free blocks are
also linked in a list whose links live in the unused payload.

- Sizes are rounded to 16 bytes; the low bit of the size is the "used" flag.
- Allocation splits a block if the remainder can form a block of its own.
- The end marker looks like a used block, so merging cannot leave the arena.
- An arena that becomes entirely free is returned to the vmm, except the
  first one (avoids map/unmap churn).
- `kfree` panics on a pointer whose header is not a used block (double free).

## Checks

| Check | What it proves |
|---|---|
| `bitmap`, `list` | the shared data structures |
| `pmm` | distinct frames, counts, contiguous runs, clean failure when too large |
| `vmm` | kernel image permissions, zero-fill, two views of one frame, guard page, no frame leak |
| `heap` | alignment, merging, arena release, 4000-step randomised stress with overlap detection, no leak |
| `write-rodata` | writing a constant gives `#PF` with error code 3 (present + write) |
| `use-after-unmap` | access after free faults, i.e. the TLB entry was invalidated |
| `stack-overflow` | guard page hit escalates to `#DF`, handled on its IST stack |

`use-after-unmap` was validated the other way round as well: with the
`invlpg` removed the check fails, because the stale TLB entry lets the access
through.

## Known limits

- Single CPU. On SMP, unmapping needs a TLB shootdown on the other CPUs.
- The direct map does not include device memory or reserved ranges; drivers
  map what they need with `vmm_map_physical`.
- First-fit over one list is O(number of free blocks). Size classes or a slab
  layer can come later if profiling shows a need.
