# Build system for the whole OS.
#
#   make            build the bootable ISO
#   make run        build and boot in QEMU (window + serial on this terminal)
#   make check      build, boot headless, run every self-test, verify the logs
#   make debug      boot headless, frozen, waiting for GDB on :1234
#   make gdb        attach GDB to a `make debug` session
#   make symbolize ADDR=0x...   turn a panic stack-trace address into a name
#   make clean      remove build outputs (keeps the downloaded bootloader)
#   make distclean  remove everything under build/

OS_NAME := saddev
ARCH    := x86_64
BUILD   := build

# ---------------------------------------------------------------- toolchain
# llvm: clang is a cross-compiler by design; --target selects a bare-metal
#       x86_64 ELF target, so no host headers or libraries can leak in.
# gcc:  a dedicated x86_64-elf cross toolchain, if you have built one.
TOOLCHAIN ?= llvm

# STRING_CFLAGS stops the optimiser from turning the loop inside memset()
# into a call to memset(), i.e. infinite recursion. Same for memcpy etc.
ifeq ($(TOOLCHAIN),llvm)
    CC := clang --target=x86_64-unknown-elf
    LD := ld.lld
    ADDR2LINE := llvm-addr2line
    AR := llvm-ar
    STRIP := llvm-strip
    STRING_CFLAGS := -fno-builtin
else ifeq ($(TOOLCHAIN),gcc)
    CC := x86_64-elf-gcc
    LD := x86_64-elf-ld
    ADDR2LINE := x86_64-elf-addr2line
    AR := x86_64-elf-ar
    STRIP := x86_64-elf-strip
    STRING_CFLAGS := -fno-builtin -fno-tree-loop-distribute-patterns
else
    $(error TOOLCHAIN must be 'llvm' or 'gcc', got '$(TOOLCHAIN)')
endif

AS      := nasm
HOST_CC := cc
QEMU    := qemu-system-x86_64

# -ffreestanding       no hosted C library; only headers like <stdint.h> exist
# -mno-red-zone        an interrupt may use the stack right below rsp, so the
#                      compiler must not keep data there
# -mgeneral-regs-only  no SSE/x87 in the kernel: interrupt entry does not save
#                      those registers
# -mcmodel=kernel      code lives in the top 2 GiB of the address space
# -fno-pic/-fno-pie    the kernel is linked at a fixed address
# -ffile-prefix-map    keeps the checkout path out of the binary, so the same
#                      sources give the same kernel in any directory
CFLAGS := -std=c17 -O2 -g \
          -Wall -Wextra -Werror \
          -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
          -fno-omit-frame-pointer -fno-asynchronous-unwind-tables \
          -mno-red-zone -mgeneral-regs-only -mcmodel=kernel \
          -ffile-prefix-map=$(CURDIR)=. \
          -Iarch/include -Ikernel -Iabi \
          -MMD -MP

ASFLAGS := -f elf64 -g -F dwarf -Werror

LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T boot/linker.ld

# ------------------------------------------------------------------ sources
C_SRCS   := $(shell find arch/$(ARCH) kernel -name '*.c' | sort)
ASM_SRCS := $(shell find arch/$(ARCH) -name '*.asm' | sort)
OBJS     := $(C_SRCS:%.c=$(BUILD)/obj/%.o) $(ASM_SRCS:%.asm=$(BUILD)/obj/%.asm.o)
DEPS     := $(C_SRCS:%.c=$(BUILD)/obj/%.d)

KERNEL := $(BUILD)/kernel.elf
ISO    := $(BUILD)/$(OS_NAME).iso

# ----------------------------------------------------------------- userland
# User programs are freestanding too, but unlike the kernel they may use the
# red zone and SSE, and they are linked low instead of in the top 2 GiB.
USER_CFLAGS := -std=c17 -O2 -g \
               -Wall -Wextra -Werror \
               -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
               -fno-omit-frame-pointer -fno-asynchronous-unwind-tables \
               -ffile-prefix-map=$(CURDIR)=. \
               -Ilibc/include -Iabi \
               -MMD -MP

USER_LDFLAGS := -nostdlib -static -z max-page-size=0x1000 -T user/user.ld

LIBC_C_SRCS := $(shell find libc/src -name '*.c' | sort)
LIBC_ASM_SRCS := $(filter-out %/crt0.asm,$(sort $(wildcard libc/arch/$(ARCH)/*.asm)))
LIBC_OBJS   := $(LIBC_C_SRCS:%.c=$(BUILD)/obj/%.o) $(LIBC_ASM_SRCS:%.asm=$(BUILD)/obj/%.asm.o)
LIBC        := $(BUILD)/libc.a
# The startup code is not part of the library: it must come first in every
# link, and exactly once.
CRT0        := $(BUILD)/obj/libc/arch/$(ARCH)/crt0.asm.o

# Programs are built into $(BUILD)/user/, laid out as in the file system.
USER_TEST_SRCS := $(sort $(wildcard user/tests/*.c))
USER_PROGRAMS  := $(BUILD)/user/bin/init $(USER_TEST_SRCS:user/tests/%.c=$(BUILD)/user/test/%)
USER_OBJS      := $(BUILD)/obj/user/init/init.o $(USER_TEST_SRCS:%.c=$(BUILD)/obj/%.o)

INITRAMFS := $(BUILD)/initramfs.tar

DEPS += $(LIBC_C_SRCS:%.c=$(BUILD)/obj/%.d) $(USER_OBJS:%.o=%.d)

# ------------------------------------------------------------------- limine
# The bootloader is fetched as a prebuilt binary release. Tag and commit are
# both pinned: the tag says what we want, the commit proves we got it.
LIMINE_REPO   := https://github.com/limine-bootloader/limine.git
LIMINE_TAG    := v9.6.7-binary
LIMINE_COMMIT := ee5d29cd0a8034612dcd1df3f00052480db785c5
LIMINE_DIR    := $(BUILD)/limine
LIMINE_TOOL   := $(LIMINE_DIR)/limine

# --------------------------------------------------------------------- qemu
# Use hardware virtualisation when the host offers it; otherwise QEMU falls
# back to (much slower) software emulation.
QEMU_ACCEL := $(shell test -w /dev/kvm && echo "-enable-kvm -cpu host")

# $(call QEMU_FLAGS,<iso>)
# -no-reboot: a triple fault stops QEMU instead of silently rebooting forever.
QEMU_FLAGS = -M q35 -m 256M $(QEMU_ACCEL) -cdrom $(1) -boot d -no-reboot

QEMU_DEBUG_EXIT := -device isa-debug-exit,iobase=0xf4,iosize=0x04
QEMU_HEADLESS   := -display none -serial stdio

CHECK_TIMEOUT := 60
comma := ,

# ------------------------------------------------------------------ targets
.PHONY: all run debug gdb symbolize clean distclean
all: $(ISO)

run: $(ISO)
	$(QEMU) $(call QEMU_FLAGS,$(ISO)) -serial stdio

# -s: GDB server on localhost:1234.  -S: do not start the CPU until GDB says so.
debug: $(ISO)
	$(QEMU) $(call QEMU_FLAGS,$(ISO)) $(QEMU_HEADLESS) -s -S

gdb: $(KERNEL)
	gdb -q -x tools/gdbinit

symbolize: $(KERNEL)
	$(ADDR2LINE) -f -p -e $(KERNEL) $(ADDR)

# ------------------------------------------------------------------- checks
# Each check boots an ISO whose kernel command line selects a self-test.
# $(call run-check,<name>,<iso>,<pass|panic>,<quoted regexes...>[,<extra qemu flags>])
define run-check
tools/check-serial.sh $(1) $(BUILD)/check-$(1).log $(CHECK_TIMEOUT) $(3) $(4) -- \
    $(QEMU) $(call QEMU_FLAGS,$(2)) $(QEMU_HEADLESS) $(QEMU_DEBUG_EXIT) $(5)
endef

# Tests that only need to report success.
PASS_TESTS  := printf cmdline bitmap list pmm vmm heap threads sleep mutex thread-churn \
               boot user-hello user-syscalls user-files user-malloc user-spawn user-fpu \
               user-sleep user-fault user-churn
# Tests with their own expectations, written out below.
OTHER_TESTS := timer keyboard breakpoint divide-by-zero page-fault write-rodata use-after-unmap stack-overflow

CHECKS := $(PASS_TESTS) $(OTHER_TESTS)
.PHONY: check $(CHECKS:%=check-%)
check: $(CHECKS:%=check-%)
	@echo "All $(words $(CHECKS)) checks passed."

$(PASS_TESTS:%=check-%): check-%: $(BUILD)/test-%.iso
	@$(call run-check,$*,$<,pass,"test: '$*' passed")

# Ticks are compared against an independent hardware clock.
check-timer: $(BUILD)/test-timer.iso
	@$(call run-check,timer,$<,pass,'time: 1000 Hz tick' 'timer test: 1000 ticks took' "test: 'timer' passed")

# Keys are typed into the guest through QEMU's machine protocol (QMP) once
# the kernel says it is ready. The key list must match expected_presses in
# kernel/tests/test_devices.c.
KEYBOARD_QMP := $(BUILD)/check-keyboard.qmp
check-keyboard: $(BUILD)/test-keyboard.iso
	@CHECK_SIDECAR="tools/qemu-sendkeys.py $(KEYBOARD_QMP) $(BUILD)/check-keyboard.log 'keyboard ready for input' a shift-b ret up" \
	    $(call run-check,keyboard,$<,pass,"key: a pressed$(comma) character 'a'" "key: b pressed$(comma) character 'B'" \
	    'key: enter pressed' 'key: up released' "test: 'keyboard' passed",-qmp unix:$(KEYBOARD_QMP)$(comma)server=on$(comma)wait=off)

# The handler must return to the interrupted code, so the test still passes.
check-breakpoint: $(BUILD)/test-breakpoint.iso
	@$(call run-check,breakpoint,$<,pass,'trap: breakpoint at 0xffffffff8' "test: 'breakpoint' passed")

# A deliberate crash: the kernel must report it cleanly and stop.
check-divide-by-zero: $(BUILD)/test-divide-by-zero.iso
	@$(call run-check,divide-by-zero,$<,panic,'PANIC: CPU exception: Divide Error \(#DE\)' \
	    'vector=0 error_code=0x0' ' rip=ffffffff8[0-9a-f]{7}' \
	    'Stack trace:' '\[0\] 0xffffffff8' 'System halted')

# Unlike #DE, a page fault makes the CPU push an error code, so this also
# covers the other kind of entry stub.
check-page-fault: $(BUILD)/test-page-fault.iso
	@$(call run-check,page-fault,$<,panic,'PANIC: CPU exception: Page Fault \(#PF\)' \
	    'vector=14 error_code=0x0' 'fault address=0x00000000dead0000' 'System halted')

# Page fault error code bits: 1 = the page was present (a protection
# violation, not a missing page), 2 = the access was a write.
check-write-rodata: $(BUILD)/test-write-rodata.iso
	@$(call run-check,write-rodata,$<,panic,'PANIC: CPU exception: Page Fault \(#PF\)' \
	    'vector=14 error_code=0x3' 'fault address=0xffffffff8' 'System halted')

# Passes only if unmapping also invalidated the cached translation.
check-use-after-unmap: $(BUILD)/test-use-after-unmap.iso
	@$(call run-check,use-after-unmap,$<,panic,'PANIC: CPU exception: Page Fault \(#PF\)' \
	    'vector=14 error_code=0x0' 'fault address=0xffffc0' 'System halted')

# Running into the guard page must end in a double fault handled on its own
# stack, not in a silent reset.
check-stack-overflow: $(BUILD)/test-stack-overflow.iso
	@$(call run-check,stack-overflow,$<,panic,'PANIC: CPU exception: Double Fault \(#DF\)' \
	    'vector=8 error_code=0x0' 'System halted')

# -------------------------------------------------------------------- build
$(BUILD)/obj/kernel/core/string.o: CFLAGS += $(STRING_CFLAGS)

$(BUILD)/obj/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c $< -o $@

$(BUILD)/obj/%.asm.o: %.asm
	@mkdir -p $(@D)
	$(AS) $(ASFLAGS) $< -o $@

$(KERNEL): $(OBJS) boot/linker.ld
	$(LD) $(LDFLAGS) $(OBJS) -o $@

# Same compile rule as the kernel, different flags.
$(LIBC_OBJS) $(USER_OBJS): CFLAGS := $(USER_CFLAGS)
$(BUILD)/obj/libc/src/string.o: CFLAGS := $(USER_CFLAGS) $(STRING_CFLAGS)

$(LIBC): $(LIBC_OBJS)
	@mkdir -p $(@D)
	$(AR) rcs $@ $^

USER_LINK_DEPS := $(CRT0) $(LIBC) user/user.ld
define link-user
@mkdir -p $(@D)
$(LD) $(USER_LDFLAGS) $(CRT0) $(filter-out $(USER_LINK_DEPS),$^) $(LIBC) -o $@
endef

$(BUILD)/user/bin/init: $(BUILD)/obj/user/init/init.o $(USER_LINK_DEPS)
	$(link-user)

$(BUILD)/user/test/%: $(BUILD)/obj/user/tests/%.o $(USER_LINK_DEPS)
	$(link-user)

$(INITRAMFS): $(USER_PROGRAMS) $(shell find user/rootfs -type f) tools/mkinitramfs.sh
	tools/mkinitramfs.sh $@ user/rootfs $(BUILD)/user $(STRIP)

$(LIMINE_TOOL):
	rm -rf $(LIMINE_DIR)
	git -c advice.detachedHead=false clone --quiet --depth=1 --branch=$(LIMINE_TAG) $(LIMINE_REPO) $(LIMINE_DIR)
	@test "$$(git -C $(LIMINE_DIR) rev-parse HEAD)" = "$(LIMINE_COMMIT)" || \
	    { echo "limine: $(LIMINE_TAG) is not commit $(LIMINE_COMMIT)"; rm -rf $(LIMINE_DIR); exit 1; }
	$(MAKE) -C $(LIMINE_DIR) CC="$(HOST_CC)" limine

ISO_DEPS := $(KERNEL) $(INITRAMFS) boot/limine.conf.in tools/mkiso.sh $(LIMINE_TOOL)

$(ISO): $(ISO_DEPS)
	tools/mkiso.sh $@ $(KERNEL) $(INITRAMFS) $(LIMINE_DIR) boot/limine.conf.in ""

# Keep the test ISOs: make would otherwise delete them as intermediate files.
.PRECIOUS: $(BUILD)/test-%.iso $(BUILD)/obj/user/tests/%.o
$(BUILD)/test-%.iso: $(ISO_DEPS)
	tools/mkiso.sh $@ $(KERNEL) $(INITRAMFS) $(LIMINE_DIR) boot/limine.conf.in "test=$*"

clean:
	rm -rf $(BUILD)/obj $(KERNEL) $(BUILD)/*.iso $(BUILD)/*.log* $(BUILD)/user $(LIBC) $(INITRAMFS)

distclean:
	rm -rf $(BUILD)

-include $(DEPS)
