#!/bin/sh
# Builds the initial RAM file system: a ustar archive of everything user
# space needs at boot.
#
# usage: mkinitramfs.sh <output.tar> <strip-command> <programs-root> <static-root>...
#
#   programs-root  directory of built programs, laid out as in the archive
#                  (bin/init, test/hello, ...); copied without debug info
#   static-root    directories copied as they are (configuration, fonts)
set -eu

OUTPUT="$1"; STRIP="$2"; PROGRAMS_ROOT="$3"
shift 3

ROOT="$OUTPUT.root"
rm -rf "$ROOT"
mkdir -p "$ROOT"
for static_root in "$@"; do
    cp -R "$static_root/." "$ROOT/"
done

# Debug information stays in the build tree, where GDB and addr2line can
# use it; the copy that is loaded into memory at every boot goes without.
(cd "$PROGRAMS_ROOT" && find . -type f) | while read -r program; do
    mkdir -p "$ROOT/$(dirname "$program")"
    "$STRIP" --strip-debug -o "$ROOT/$program" "$PROGRAMS_ROOT/$program"
done

# Fixed order, timestamps and owner: the same inputs give the same archive.
tar --format=ustar --sort=name --mtime=@0 --owner=0 --group=0 --numeric-owner \
    -C "$ROOT" -cf "$OUTPUT" .
rm -rf "$ROOT"
