# Architecture

Saddev is a from-scratch x86_64 hobby operating system with a desktop
environment that runs entirely in userspace.

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
kernel/ipc                       channels, named ports, shared memory
kernel/fs                        VFS, file objects, poll, file systems
kernel/drivers                   device drivers (may block, so above sched)
kernel/sched                     threads, scheduler, wait queues, processes
kernel/mm                        memory management
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
| `abi/saddev/` | kernel-user ABI: system call numbers, structures, error codes. The only headers shared by the kernel and user space |
| `arch/include/arch/` | arch interface: `boot.h`, `context.h`, `cpu.h`, `debug_uart.h`, `fpu.h`, `irq.h`, `user.h`, `mmu.h`, `ps2.h`, `timer.h`, `trap.h` |
| `arch/x86_64/boot/` | `_start` (NASM), Limine requests |
| `arch/x86_64/` | GDT/TSS, IDT, interrupt entry stubs, trap dispatch, paging, port I/O, ACPI tables, PIC, PIT, local APIC, I/O APIC, IRQ dispatch, context switch, system call entry, FPU state |
| `kernel/main.c` | `kmain`, the composition root that wires subsystems together |
| `kernel/core/` | string functions, printf, log, spinlock, panic, command line, list, bitmap, time |
| `kernel/mm/` | frame allocator (`pmm`), kernel virtual memory (`vmm`), heap, user address spaces (`aspace`) |
| `kernel/sched/` | threads, round-robin scheduler, wait queues, mutex, processes, ELF loader |
| `kernel/fs/` | VFS, tar-based initial RAM file system, device file system, poll |
| `kernel/ipc/` | message channels, named ports, shared memory |
| `kernel/syscall/` | system call table, handlers, checked access to user memory |
| `kernel/drivers/serial/` | 16550 UART driver |
| `kernel/drivers/ps2/` | PS/2 controller (8042) driver |
| `kernel/drivers/keyboard/` | key events and queue; PS/2 scancode decoder |
| `kernel/drivers/mouse/` | mouse events and queue; PS/2 packet decoder |
| `kernel/drivers/framebuffer/` | describes the display and maps it into one process |
| `kernel/tests/` | in-kernel self-tests selected with `test=<name>` |
| `libc/` | user C library; `libc/arch/x86_64` holds its few assembly files |
| `user/` | `init`, test programs, `rootfs/` (static files), `user.ld` |
| `user/libgui/` | 2D drawing (surfaces, shapes, blending, text, blur, scaling), animation (easing, tweens, springs), the window system protocol and its client library |
| `user/compositor/` | the window server |
| `user/desktop/` | the desktop shell: wallpaper, menu bar, dock, settings |
| `user/apps/` | applications, one directory each: `terminal`, `sh`, `files`, `settings`, `about` |
| `third_party/fonts/` | the Inter and JetBrains Mono typefaces (SIL OFL 1.1), input to `tools/mkfont.py` |
| `tools/` | host setup, ISO and initramfs builders, font atlas generator, serial check runner, QMP key injector and screenshot checker, GDB script |

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
- The same technique lets a lower kernel layer notify a higher one: the
  keyboard driver calls a listener that `devfs` installs (to wake pollers),
  and `core/time` calls the scheduler's tick hook.
- IPC objects are files (`struct file` with their own operations), so they
  live in descriptor tables, can be polled, and can be sent over channels.
  That is why `ipc` sits above `fs`.

## Kernel virtual address space (x86_64)

| Range | Use |
|---|---|
| `0x0000000000100000` - `0x00007ffffffff000` | user space, one per process: program at `0x400000`, `mmap` area from `0x100000000000`, stack at the top |
| `0xffff800000000000` + physical address | direct map of RAM, read/write, no-execute |
| `0xffffc00000000000` + 4 GiB | dynamic area: heap arenas, kernel stacks |
| `0xffffffff80000000` - top | kernel image: text `R-X`, rodata `R--`, data/bss `RW-` |

## Lock order

Locks are always taken in this order, never the reverse:
address space lock, then heap lock, then vmm lock, then pmm lock.

A lock that protects a waited-for condition (a mutex's internal lock, the
keyboard queue lock, the process table lock, a channel's lock, the port
lock, the poll lock) is taken before the scheduler lock.

The poll lock is taken before a file's own lock (poll asks each file for its
state). Code that changes a file's state therefore calls `poll_notify()`
only after releasing that file's lock. The scheduler lock
and the log lock are leaves: nothing is acquired while holding them.

## Interrupt context

Device and timer handlers run with interrupts disabled on the stack of
whatever was interrupted. They must be short, must not block, and may only
take spinlocks (which every other user holds with interrupts disabled).

## Design notes

- [00 - Toolchain and boot](00-toolchain.md)
- [01 - Core: logging, panic, descriptor tables, traps](01-core.md)
- [02 - Memory: frames, paging, heap](02-memory.md)
- [03 - Interrupts, time and the keyboard](03-interrupts.md)
- [04 - Threads and scheduling](04-scheduling.md)
- [05 - User space: processes, system calls, files, libc](05-userspace.md)
- [06 - IPC: channels, ports, shared memory, poll](06-ipc.md)
- [07 - Graphics base: display, mouse, drawing, text](07-graphics.md)
- [08 - The window server](08-compositor.md)
- [09 - The desktop](09-desktop.md)
- [10 - Animations](10-animations.md)
- [11 - Applications](11-apps.md)
- [12 - Polish: measuring, speeding up, leak checks, demo](12-polish.md)
