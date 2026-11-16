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
| `boot/` | `limine.conf` boot menu, `linker.ld` kernel image layout |
| `arch/include/arch/` | arch interface: `boot.h`, `cpu.h`, `early_console.h` |
| `arch/x86_64/boot/` | `_start` (NASM), Limine requests, `arch_entry` |
| `arch/x86_64/` | port I/O, COM1 early console, halt, debug exit |
| `kernel/core/` | `kmain` |
| `tools/` | host setup, serial check runner, GDB script |

## Design notes

- [00 - Toolchain and boot](00-toolchain.md)
