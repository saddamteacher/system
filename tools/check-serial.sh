#!/bin/sh
# Boots the OS headless and checks the serial log.
#
# usage: check-serial.sh <log-file> <timeout-seconds> <expected-regex> -- <qemu command...>
#
# Passes only if the kernel reported success through the debug-exit device
# AND the expected text appeared on the serial port.
set -u

LOG="$1"; TIMEOUT="$2"; EXPECTED="$3"
shift 4

# QEMU's isa-debug-exit turns a written value v into exit status (v << 1) | 1.
# The kernel writes 0x10 for success, so success is (0x10 << 1) | 1 = 33.
QEMU_STATUS_SUCCESS=33

timeout "$TIMEOUT" "$@" > "$LOG" 2>&1
status=$?

echo "--- serial log ($LOG) ---"
cat "$LOG"
echo "--- end of serial log ---"

if [ "$status" -eq 124 ]; then
    echo "CHECK FAILED: timed out after ${TIMEOUT}s (kernel never signalled completion)"
    exit 1
fi
if [ "$status" -ne "$QEMU_STATUS_SUCCESS" ]; then
    echo "CHECK FAILED: QEMU exited with status $status, expected $QEMU_STATUS_SUCCESS"
    exit 1
fi
if ! grep -Eq "$EXPECTED" "$LOG"; then
    echo "CHECK FAILED: serial log does not match: $EXPECTED"
    exit 1
fi
echo "CHECK PASSED"
