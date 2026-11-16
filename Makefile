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

OS_NAME := lumen
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
    STRING_CFLAGS := -fno-builtin
else ifeq ($(TOOLCHAIN),gcc)
    CC := x86_64-elf-gcc
    LD := x86_64-elf-ld
    ADDR2LINE := x86_64-elf-addr2line
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
          -Iarch/include -Ikernel \
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
# $(call run-check,<name>,<iso>,<pass|panic>,<quoted regexes...>)
define run-check
tools/check-serial.sh $(1) $(BUILD)/check-$(1).log $(CHECK_TIMEOUT) $(3) $(4) -- \
    $(QEMU) $(call QEMU_FLAGS,$(2)) $(QEMU_HEADLESS) $(QEMU_DEBUG_EXIT)
endef

CHECKS := boot printf cmdline breakpoint divide-by-zero page-fault
.PHONY: check $(CHECKS:%=check-%)
check: $(CHECKS:%=check-%)
	@echo "All checks passed."

check-boot: $(ISO)
	@$(call run-check,boot,$(ISO),pass,'hello from the kernel')

check-printf: $(BUILD)/test-printf.iso
	@$(call run-check,printf,$<,pass,"test: 'printf' passed")

check-cmdline: $(BUILD)/test-cmdline.iso
	@$(call run-check,cmdline,$<,pass,"test: 'cmdline' passed")

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

$(LIMINE_TOOL):
	rm -rf $(LIMINE_DIR)
	git -c advice.detachedHead=false clone --quiet --depth=1 --branch=$(LIMINE_TAG) $(LIMINE_REPO) $(LIMINE_DIR)
	@test "$$(git -C $(LIMINE_DIR) rev-parse HEAD)" = "$(LIMINE_COMMIT)" || \
	    { echo "limine: $(LIMINE_TAG) is not commit $(LIMINE_COMMIT)"; rm -rf $(LIMINE_DIR); exit 1; }
	$(MAKE) -C $(LIMINE_DIR) CC="$(HOST_CC)" limine

ISO_DEPS := $(KERNEL) boot/limine.conf.in tools/mkiso.sh $(LIMINE_TOOL)

$(ISO): $(ISO_DEPS)
	tools/mkiso.sh $@ $(KERNEL) $(LIMINE_DIR) boot/limine.conf.in ""

# Keep the test ISOs: make would otherwise delete them as intermediate files.
.PRECIOUS: $(BUILD)/test-%.iso
$(BUILD)/test-%.iso: $(ISO_DEPS)
	tools/mkiso.sh $@ $(KERNEL) $(LIMINE_DIR) boot/limine.conf.in "test=$*"

clean:
	rm -rf $(BUILD)/obj $(KERNEL) $(BUILD)/*.iso $(BUILD)/*.log*

distclean:
	rm -rf $(BUILD)

-include $(DEPS)
