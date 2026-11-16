# 05 - User space: processes, system calls, files, libc

Programs now run in ring 3, each in its own address space, and reach the
kernel only through system calls.

## Privilege and the two ways in

User code runs with CPL 3 (the low two bits of `CS`). It cannot execute
privileged instructions or touch pages without the *user* bit. It enters the
kernel in exactly two ways:

- **Interrupts and exceptions.** The CPU loads the kernel stack from
  `TSS.rsp0`, pushes the user state and jumps through the IDT, as in Phase 1.
- **System calls**, via the `syscall` instruction.

`arch_set_kernel_stack` keeps both in step with the running thread: the
scheduler calls it on every switch.

### syscall / sysret

`syscall` is deliberately minimal: it jumps to the address in the `LSTAR`
MSR, puts the return address in `rcx` and the flags in `r11`, masks flags
(`SFMASK`: we clear the interrupt flag) and loads fixed segment selectors
from `STAR`. It does **not** switch stacks.

`syscall_entry` (`arch/x86_64/syscall_entry.asm`) therefore begins on the
user's stack with interrupts off: it parks the user `rsp` in a variable,
loads the kernel stack, copies the user `rsp` onto it, saves registers, and
only then enables interrupts and calls C. Parking the stack pointer in a
plain variable is a single-CPU simplification; with several CPUs it becomes
per-CPU data reached through `swapgs`.

Convention: number in `rax`, arguments in `rdi, rsi, rdx, r10, r8, r9`,
result in `rax`; a negative result is `-errno`.

`sysret` computes its selectors by fixed offsets from one base, which is why
the GDT has user data immediately before user code (decided in Phase 1).

### First entry to user mode

There is no "jump to ring 3" instruction. `arch_enter_user` builds the stack
frame an interrupt from user mode would have left (`ss`, `rsp`, `rflags`,
`cs`, `rip`), clears the registers and executes `iretq`.

## Address spaces

`arch_mmu_space_init` creates a top-level page table whose upper half is a
copy of the kernel's 256 top-level entries. Those entries point at the same
lower-level tables, so the kernel half is shared, and later kernel mappings
appear everywhere. This only holds if the top-level entry existed before the
copy, so `arch_mmu_init` pre-creates the entry of the dynamic area.

`kernel/mm/aspace` adds what the page tables alone do not record: the list
of mapped regions, needed to free everything on exit.

- `aspace_map` / `aspace_map_anywhere`: zero-filled private memory.
- `aspace_write`: copies into a space that is not active by translating each
  page and writing through the direct map. This is how programs are loaded
  without switching address spaces.
- `aspace_user_range_valid`: the check behind every user pointer.

The scheduler loads a thread's address space (and saves/restores its FPU
state) on each switch; kernel threads get the kernel's own space.

## Processes

A process is an address space, one thread, and a table of 16 open files.

**Starting** (`process_spawn`): read the file, load the ELF image, build the
stack with `argc`/`argv`, inherit or open the standard files, then create
the thread. The thread starts in kernel mode and immediately leaves through
`arch_enter_user`.

**ELF loading** (`kernel/sched/elf.c`): statically linked 64-bit
executables. For each `LOAD` program header, map the covered pages with the
header's permissions and copy the file bytes; fresh pages are zero, which is
`.bss`. Every field is validated: sizes against the file, addresses against
user space, no wrap-around, no overlap.

**Ending** (`process_exit`): close files, switch to the kernel address space
(a process cannot free the page tables it is running on), destroy the space,
then publish the exit code and wake waiters. `process_wait` collects the
code and frees the record.

**Faults.** A CPU exception from user mode does not panic. The process layer
registers a handler with `core/panic`; it logs the event and ends the
process with exit code `128 + vector`.

## Checked access to user memory

A pointer from a program is just a number the program chose. Before the
kernel reads or writes through it, `kernel/syscall/user_memory.c` verifies
that the whole range lies in user space, is mapped with the user bit, and is
writable when the kernel is going to write. Strings are checked byte by
byte, since their length is unknown until the terminator is found.

After the check the memory is accessed directly: a system call runs in the
caller's address space, and with one thread per process nothing can unmap
the range in between.

## Files

`kernel/fs/vfs` dispatches by path prefix (longest match) to a file system.

| Mount | File system | Contents |
|---|---|---|
| `/` | `tarfs` | the initial RAM file system: a ustar archive loaded by the bootloader as a module; read-only |
| `/dev` | `devfs` | `console`: writes go to the kernel log, reads return typed characters |

An open `struct file` is reference counted so that a child can share its
parent's standard files.

The archive is built by `tools/mkinitramfs.sh` from `user/rootfs/` plus the
built programs (stripped of debug info), with fixed order, timestamps and
owners so that identical inputs give an identical archive.

## System calls

`exit, write, read, open, close, seek, stat, readdir, mmap, munmap, spawn,
wait, getpid, sleep_ms, yield, uptime_ms`. Numbers and structures are in
`abi/saddev/syscall.h`, shared by kernel and libc.

## Floating point state

User programs use SSE; the kernel is built without it. `fpu_init` enables
the unit, and the scheduler saves and restores the 512-byte `fxsave` area
for every thread that runs user code. Kernel threads have none.

## libc

`libc/` is a small freestanding C library: `crt0` (calls `main`, then
`exit`), a system call stub, wrappers that turn `-errno` into `-1` plus
`errno`, `string.h`, `printf` (with precision and `%f`), `malloc` (the same
boundary-tag design as the kernel heap, on top of `mmap`), `strtol`, and
`fenv.h` rounding control. Its three assembly files live in
`libc/arch/x86_64`.

Programs are linked at `0x400000` by `user/user.ld`, with code, constants
and data in separate page-aligned segments.

## Checks

| Check | What it proves |
|---|---|
| `boot` | `/bin/init` starts, prints through `write`, reads a file |
| `user-hello` | output, arguments, `printf` with floating point |
| `user-syscalls` | 20 bad calls (closed or huge descriptors, NULL, kernel addresses, ranges that run off or wrap, writing into read-only memory, unknown call numbers) all return the right error |
| `user-files` | `stat`, `read`, `seek`, end of file, directory listing across both mounts |
| `user-malloc` | 3000-step randomised workload with overlap detection, `calloc`, `realloc` |
| `user-spawn` | arguments, exit codes, waiting in any order, and every way starting a program can fail |
| `user-fpu` | four processes with different rounding modes keep them across 400 yields each; new processes start with default state |
| `user-sleep` | time from user space |
| `user-fault` | a NULL dereference and a read of kernel memory each end the process, and the system keeps working |
| `user-churn` | 20 processes, half exiting and half crashing, leave frames, heap and the process table unchanged |

### A test that proved nothing

The first FPU test summed a series in several processes and passed. It also
passed with FPU restore disabled. Under emulation interrupts arrive only
between translated code blocks, and around a call the compiler keeps no live
value in a vector register (all of them are caller-saved), so no state was
ever at risk. The rewritten test uses the rounding mode, which persists
across calls; with restore disabled all four workers now fail.

## Known limits

- One thread per process.
- No `fork`/`exec`: `spawn` starts a program directly.
- Files are read-only; there is no writable file system yet.
- `mmap` addresses are never reused within a process.
- Any process may wait for any other; there is no parent/child ownership.
