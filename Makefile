# Build system for the whole OS.
#
#   make            build the bootable ISO
#   make run        build and boot in QEMU (window + serial on this terminal)
#   make check      build, boot headless and verify the serial log
#   make debug      boot headless, frozen, waiting for GDB on :1234
#   make gdb        attach GDB to a `make debug` session
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

ifeq ($(TOOLCHAIN),llvm)
    CC := clang --target=x86_64-unknown-elf
    LD := ld.lld
else ifeq ($(TOOLCHAIN),gcc)
    CC := x86_64-elf-gcc
    LD := x86_64-elf-ld
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
CFLAGS := -std=c17 -O2 -g \
          -Wall -Wextra -Werror \
          -ffreestanding -fno-stack-protector -fno-pic -fno-pie \
          -fno-omit-frame-pointer -fno-asynchronous-unwind-tables \
          -mno-red-zone -mgeneral-regs-only -mcmodel=kernel \
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

# -no-reboot: a triple fault stops QEMU instead of silently rebooting forever.
QEMU_FLAGS := -M q35 -m 256M $(QEMU_ACCEL) -cdrom $(ISO) -boot d -no-reboot

QEMU_DEBUG_EXIT := -device isa-debug-exit,iobase=0xf4,iosize=0x04
QEMU_HEADLESS   := -display none -serial stdio

CHECK_TIMEOUT := 60
CHECK_EXPECT  := ^hello from the kernel

# ------------------------------------------------------------------ targets
.PHONY: all run check debug gdb clean distclean
all: $(ISO)

run: $(ISO)
	$(QEMU) $(QEMU_FLAGS) -serial stdio

check: $(ISO)
	tools/check-serial.sh $(BUILD)/serial.log $(CHECK_TIMEOUT) '$(CHECK_EXPECT)' -- \
	    $(QEMU) $(QEMU_FLAGS) $(QEMU_HEADLESS) $(QEMU_DEBUG_EXIT)

# -s: GDB server on localhost:1234.  -S: do not start the CPU until GDB says so.
debug: $(ISO)
	$(QEMU) $(QEMU_FLAGS) $(QEMU_HEADLESS) -s -S

gdb: $(KERNEL)
	gdb -q -x tools/gdbinit

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

# The ISO boots both ways: via BIOS (El Torito image limine-bios-cd.bin) and
# via UEFI (the FAT image limine-uefi-cd.bin, which contains BOOTX64.EFI).
$(ISO): $(KERNEL) boot/limine.conf $(LIMINE_TOOL)
	rm -rf $(BUILD)/iso_root
	mkdir -p $(BUILD)/iso_root/boot/limine $(BUILD)/iso_root/EFI/BOOT
	cp $(KERNEL) $(BUILD)/iso_root/boot/kernel.elf
	cp boot/limine.conf $(LIMINE_DIR)/limine-bios.sys $(LIMINE_DIR)/limine-bios-cd.bin \
	    $(LIMINE_DIR)/limine-uefi-cd.bin $(BUILD)/iso_root/boot/limine/
	cp $(LIMINE_DIR)/BOOTX64.EFI $(BUILD)/iso_root/EFI/BOOT/
	xorriso -as mkisofs -quiet -R -r -J \
	    -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
	    -hfsplus -apm-block-size 2048 \
	    --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
	    --protective-msdos-label \
	    $(BUILD)/iso_root -o $@
	$(LIMINE_TOOL) bios-install $@ 2>/dev/null

clean:
	rm -rf $(BUILD)/obj $(BUILD)/iso_root $(KERNEL) $(ISO) $(BUILD)/serial.log

distclean:
	rm -rf $(BUILD)

-include $(DEPS)
