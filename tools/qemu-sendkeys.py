#!/usr/bin/env python3
"""Types keys into a running QEMU guest, for the keyboard check.

usage: qemu-sendkeys.py <qmp-socket> <serial-log> <ready-text> <key>...

Waits until <ready-text> appears in the serial log, then sends each <key>
through QEMU's machine protocol (QMP). A key is a QEMU key name ("a", "ret",
"up"); join names with "-" to press them together ("shift-b").
"""
import json
import socket
import sys
import time

CONNECT_TIMEOUT_S = 30
READY_TIMEOUT_S = 60
KEY_HOLD_MS = 40
# Long enough for the previous key to be released before the next goes down.
PAUSE_BETWEEN_KEYS_S = 0.25


def connect(path):
    deadline = time.monotonic() + CONNECT_TIMEOUT_S
    while True:
        try:
            sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            sock.connect(path)
            return sock.makefile("rw")
        except OSError:
            if time.monotonic() > deadline:
                raise
            time.sleep(0.05)


def command(qmp, name, arguments=None):
    request = {"execute": name}
    if arguments is not None:
        request["arguments"] = arguments
    qmp.write(json.dumps(request) + "\n")
    qmp.flush()
    # QMP interleaves asynchronous events with replies; skip the events.
    while True:
        reply = json.loads(qmp.readline())
        if "return" in reply:
            return reply["return"]
        if "error" in reply:
            raise RuntimeError(f"QMP {name} failed: {reply['error']}")


def wait_for_text(log_path, text):
    deadline = time.monotonic() + READY_TIMEOUT_S
    while time.monotonic() < deadline:
        try:
            with open(log_path, errors="replace") as log:
                if text in log.read():
                    return True
        except FileNotFoundError:
            pass
        time.sleep(0.1)
    return False


def main():
    socket_path, log_path, ready_text, *keys = sys.argv[1:]

    qmp = connect(socket_path)
    qmp.readline()  # greeting
    command(qmp, "qmp_capabilities")

    if not wait_for_text(log_path, ready_text):
        sys.exit(f"qemu-sendkeys: '{ready_text}' never appeared in {log_path}")

    for key in keys:
        chord = [{"type": "qcode", "data": name} for name in key.split("-")]
        command(qmp, "send-key", {"keys": chord, "hold-time": KEY_HOLD_MS})
        time.sleep(PAUSE_BETWEEN_KEYS_S)


if __name__ == "__main__":
    main()
