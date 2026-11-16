#!/bin/sh
# Builds a bootable ISO (BIOS + UEFI) around a kernel.
#
# usage: mkiso.sh <output.iso> <kernel.elf> <limine-dir> <limine.conf.in> <cmdline>
#
# @CMDLINE@ in the config template is replaced by <cmdline>, which is how the
# test ISOs select a self-test.
set -eu

OUTPUT="$1"; KERNEL="$2"; LIMINE="$3"; CONFIG="$4"; CMDLINE="$5"

ROOT="$OUTPUT.root"
rm -rf "$ROOT"
mkdir -p "$ROOT/boot/limine" "$ROOT/EFI/BOOT"

cp "$KERNEL" "$ROOT/boot/kernel.elf"
sed "s|@CMDLINE@|$CMDLINE|" "$CONFIG" > "$ROOT/boot/limine/limine.conf"
cp "$LIMINE/limine-bios.sys" "$LIMINE/limine-bios-cd.bin" "$LIMINE/limine-uefi-cd.bin" "$ROOT/boot/limine/"
cp "$LIMINE/BOOTX64.EFI" "$ROOT/EFI/BOOT/"

# BIOS boots the El Torito image limine-bios-cd.bin; UEFI boots the FAT image
# limine-uefi-cd.bin, which contains BOOTX64.EFI.
xorriso -as mkisofs -quiet -R -r -J \
    -b boot/limine/limine-bios-cd.bin -no-emul-boot -boot-load-size 4 -boot-info-table \
    -hfsplus -apm-block-size 2048 \
    --efi-boot boot/limine/limine-uefi-cd.bin -efi-boot-part --efi-boot-image \
    --protective-msdos-label \
    "$ROOT" -o "$OUTPUT" 2>/dev/null

"$LIMINE/limine" bios-install "$OUTPUT" 2>/dev/null
rm -rf "$ROOT"
