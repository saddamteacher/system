#!/bin/sh
# Installs the host packages needed to build and run the OS (Debian/Ubuntu).
# The Limine bootloader is not installed here: the Makefile fetches a pinned
# binary release into build/.
set -eu

PACKAGES="clang lld llvm nasm make gdb git python3 python3-pil xorriso qemu-system-x86"

SUDO=""
[ "$(id -u)" -eq 0 ] || SUDO="sudo"

$SUDO apt-get update
# shellcheck disable=SC2086
DEBIAN_FRONTEND=noninteractive $SUDO apt-get install -y --no-install-recommends $PACKAGES
