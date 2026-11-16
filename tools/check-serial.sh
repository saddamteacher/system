#!/bin/sh
# Boots the OS headless and checks how it ended and what it logged.
#
# usage: check-serial.sh <name> <log-file> <timeout-seconds> <pass|panic> \
#                        <expected-regex>... -- <qemu command...>
#
# pass:  the kernel must report success through the debug-exit device.
# panic: the kernel must report failure, i.e. it must have panicked.
# Every regex must match some line of the serial log.
set -u

NAME="$1"; LOG="$2"; TIMEOUT="$3"; OUTCOME="$4"
shift 4

# QEMU's isa-debug-exit turns a written value v into exit status (v << 1) | 1.
# The kernel writes 0x10 for success and 0x11 for failure.
case "$OUTCOME" in
    pass)  expected_status=33 ;;
    panic) expected_status=35 ;;
    *) echo "check-serial.sh: outcome must be 'pass' or 'panic'"; exit 2 ;;
esac

# Regexes run up to the "--" separator; the QEMU command follows it.
count=0
for argument in "$@"; do
    [ "$argument" = "--" ] && break
    count=$((count + 1))
done
patterns_file="$LOG.patterns"
: > "$patterns_file"
i=0
while [ "$i" -lt "$count" ]; do
    printf '%s\n' "$1" >> "$patterns_file"
    shift
    i=$((i + 1))
done
shift # the "--"

timeout "$TIMEOUT" "$@" > "$LOG" 2>&1
status=$?

fail() {
    echo "--- serial log ($LOG) ---"
    cat "$LOG"
    echo "--- end of serial log ---"
    echo "CHECK FAILED [$NAME]: $1"
    exit 1
}

[ "$status" -eq 124 ] && fail "timed out after ${TIMEOUT}s (kernel never signalled completion)"
[ "$status" -ne "$expected_status" ] && fail "QEMU exited with status $status, expected $expected_status ($OUTCOME)"
while IFS= read -r pattern; do
    grep -Eq -- "$pattern" "$LOG" || fail "serial log does not match: $pattern"
done < "$patterns_file"

echo "CHECK PASSED [$NAME]"
