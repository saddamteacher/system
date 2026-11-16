# 00 - Toolchain and boot

## Commands

| Command | Effect |
|---|---|
| `tools/setup.sh` | install host packages (Debian/Ubuntu) |
| `make run` | build and boot in a QEMU window, serial on the terminal |
| `make check` | build, boot headless, run every self-test, verify the logs |
| `make symbolize ADDR=0x...` | turn a panic stack-trace address into function and line |
| `make debug` + `make gdb` | boot frozen, attach GDB, stop at `kmain` |
| `make clean` / `make distclean` | remove outputs / also the downloaded bootloader |

The first build needs network access once, to fetch Limine.

## Compiler

The default toolchain is `clang --target=x86_64-unknown-elf` with `ld.lld`.
Clang is a cross-compiler by design: the target triple selects a bare-metal
ELF target, so the host's headers and libraries cannot leak into the kernel.
`make TOOLCHAIN=gcc` uses `x86_64-elf-gcc` and `x86_64-elf-ld` instead; this
path is not exercised yet because no such toolchain is installed on the
development machine.

The flags that matter for correctness, not style:

| Flag | Why |
|---|---|
| `-ffreestanding` | there is no C library and no `main`; only compiler headers such as `<stdint.h>` |
| `-mno-red-zone` | the ABI lets leaf functions use 128 bytes below `rsp` without moving it; an interrupt would overwrite them |
| `-mgeneral-regs-only` | interrupt entry will not save SSE/x87 state, so the kernel must not use it |
| `-mcmodel=kernel` | code and data live in the top 2 GiB, addressable with 32-bit sign-extended offsets |
| `-fno-pic -fno-pie` | the kernel is linked at a fixed virtual address |
| `-fno-stack-protector` | the protector needs runtime support we have not written |
| `-ffile-prefix-map` | keeps the checkout path out of the binary |

## Boot flow

1. The firmware (SeaBIOS in QEMU) loads Limine from the ISO.
2. Limine reads `boot/limine.conf`, loads `boot/kernel.elf`, enters 64-bit
   long mode, builds page tables that map the kernel at its link address
   `0xffffffff80000000`, and scans the image for requests.
3. Limine jumps to `_start` (`arch/x86_64/boot/entry.asm`): interrupts off,
   paging on, stack provided by the bootloader.
4. `_start` zeroes `rbp`, aligns the stack and calls `arch_entry`
   (`arch/x86_64/boot/entry.c`).
5. `arch_entry` calls `kmain` (`kernel/main.c`), which brings up logging and
   then asks `arch_boot_problem()` whether Limine accepted our base revision.

### Limine protocol

We do not vendor Limine's header. `arch/x86_64/boot/limine.h` holds our own
definitions, written from the protocol specification, and grows only when the
kernel needs another request.

The image contains a *base revision tag* (three 64-bit words: two magic
numbers and the revision) placed between a start marker and an end marker in
`.data`. A bootloader that supports the revision overwrites the third word
with 0; the kernel panics otherwise. The linker script wraps
the request sections in `KEEP` because only the bootloader ever reads them.

The bootloader itself is a prebuilt binary release, cloned into `build/limine`
and pinned by tag **and** commit hash. Note that the hash to pin is the commit
the tag points at, not the hash of the annotated tag object that
`git ls-remote --tags` prints first.

### Image layout

`boot/linker.ld` produces three loadable segments (text `R-X`, rodata `R--`,
data+bss `RW-`), each starting on its own page, so later phases can map them
with exact permissions. `__kernel_start` and `__kernel_end` bracket the image.

## Automated check

`make check` boots the OS headless once per self-test and inspects the serial
log. Each run uses its own ISO: `tools/mkiso.sh` fills the `@CMDLINE@`
placeholder of `boot/limine.conf.in` with `test=<name>`, and the kernel reads
that through Limine's command line request.

QEMU's `isa-debug-exit` device sits at port `0xF4`. Writing a value `v` to it
makes QEMU exit with status `(v << 1) | 1`. The kernel writes `0x10` when it
finishes normally (status 33) and `0x11` from `panic` (status 35).
`tools/check-serial.sh` takes the expected outcome (`pass` or `panic`) and a
list of regular expressions; a check passes only if the exit status is the
expected one **and** every expression matches the log. A `timeout` catches a
kernel that hangs. Under `make run` the device is absent, the write goes to an
unassigned port and is ignored, and the kernel halts.

## Debugging

`make debug` starts QEMU with `-s -S`: a GDB server on `localhost:1234`, CPU
frozen at reset. `make gdb` loads symbols from `build/kernel.elf`, sets a
*hardware* breakpoint at `kmain` and continues. A software breakpoint would
not work here: it is a patched instruction byte, and the kernel's pages do not
exist yet while the firmware and bootloader run.

## Known limits

- Without `/dev/kvm`, QEMU uses software emulation (TCG) and is several times
  slower. The Makefile enables KVM automatically when it is available.
- The ISO is not bit-for-bit reproducible (xorriso embeds timestamps); the
  kernel ELF is.
