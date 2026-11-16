# Architecture

Lumen is a from-scratch x86_64 hobby operating system with a desktop
environment that runs entirely in userspace. ("Lumen" is a working name; it is
set in one place, `OS_NAME` in the Makefile, plus the boot menu entry.)

This document describes the layering rules. Each subsystem has its own design
note in this folder, added in the phase that builds it.

## Layers

A layer may depend only on layers below it.

```
user/apps  user/desktop          applications, menu bar, dock
user/libgui                      windows, widgets, drawing, text, animation
user/compositor  user/init       window server, first process
libc                             userland C library
--------------------------------- syscall boundary ---------------------------
kernel/syscall                   syscall table and handlers
kernel/fs  kernel/ipc            VFS, message passing, shared memory
kernel/sched                     tasks, threads, scheduler
kernel/mm  kernel/drivers        memory management, device drivers
kernel/core                      panic, logging, locks, lists, time
arch/x86_64                      everything CPU-specific
boot                             Limine config, linker script
```

## Rules

- **x86 stays in `arch/x86_64`.** No file outside it contains inline assembly
  or x86-specific knowledge. A port to another CPU adds a sibling folder.
- **The arch interface is `arch/include/arch/*.h`.** These headers are
  architecture-neutral declarations (`arch_halt_forever`, ...). The kernel
  includes them as `<arch/...>` and never reaches into `arch/x86_64` directly.
  Headers inside `arch/x86_64` are private to that folder.
- **One upcall.** The arch boot code calls `kmain()`. That contract is declared
  in `<arch/boot.h>` and implemented by `kernel/core`.
- **Small public headers.** Each subsystem exposes one small header; everything
  else is private to its folder.
- **The kernel knows nothing about windows.** It provides framebuffer mapping,
  shared memory, IPC and input events. All GUI logic is in userspace.
- **No unowned global state.** Every mutable global has a documented owner and
  lock.

## What exists today

| Path | Contents |
|---|---|
| `boot/` | `limine.conf.in` boot menu template, `linker.ld` kernel image layout |
| `arch/include/arch/` | arch interface: `boot.h`, `cpu.h`, `debug_uart.h`, `mmu.h`, `trap.h` |
| `arch/x86_64/boot/` | `_start` (NASM), Limine requests |
| `arch/x86_64/` | GDT/TSS, IDT, interrupt entry stubs, trap dispatch, paging, port I/O |
| `kernel/main.c` | `kmain`, the composition root that wires subsystems together |
| `kernel/core/` | string functions, printf, log, spinlock, panic, command line, list, bitmap |
| `kernel/mm/` | frame allocator (`pmm`), kernel virtual memory (`vmm`), heap |
| `kernel/drivers/serial/` | 16550 UART driver |
| `kernel/tests/` | in-kernel self-tests selected with `test=<name>` |
| `tools/` | host setup, ISO builder, serial check runner, GDB script |

Two rules that follow from the layering and are easy to get wrong:

- `kernel/core` is the lowest kernel layer, so it cannot include a driver.
  The log therefore writes to a *sink* function pointer; `kmain` installs the
  serial driver as that sink.
- `arch` is below the kernel, so it cannot call `kprintf` or `panic`. When it
  needs the kernel (boot handoff, a trap it cannot handle) it calls an
  *upcall* declared in an `<arch/...>` header and implemented by the kernel:
  `kmain` and `kernel_handle_trap`.
- When `arch` needs a kernel *service* rather than a notification, the kernel
  passes it in: `arch_mmu_init` receives the frame allocator as a pair of
  function pointers, because page tables are stored in frames.

## Kernel virtual address space (x86_64)

| Range | Use |
|---|---|
| `0x0000000000000000` - `0x00007fffffffffff` | user space (empty until Phase 5) |
| `0xffff800000000000` + physical address | direct map of RAM, read/write, no-execute |
| `0xffffc00000000000` + 4 GiB | dynamic area: heap arenas, kernel stacks |
| `0xffffffff80000000` - top | kernel image: text `R-X`, rodata `R--`, data/bss `RW-` |

## Lock order

Locks are always taken in this order, never the reverse:
heap lock, then vmm lock, then pmm lock. The log lock is a leaf: nothing is
acquired while holding it.

## Design notes

- [00 - Toolchain and boot](00-toolchain.md)
- [01 - Core: logging, panic, descriptor tables, traps](01-core.md)
- [02 - Memory: frames, paging, heap](02-memory.md)
