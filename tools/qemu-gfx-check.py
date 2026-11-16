#!/usr/bin/env python3
"""Verifies what the graphics test put on the screen.

usage: qemu-gfx-check.py <qmp-socket> <serial-log> <output-prefix>

Works with user/tests/gfx.c: waits for it to report "ready", takes a
screenshot through QEMU, checks the colour patches the program announced
and the cursor, moves the mouse, checks that the cursor followed, and then
presses 'q' so the program exits successfully. If a check fails the key is
never pressed and the program times out, which fails the test.

Screenshots are left as <output-prefix>-before.png and -after.png.
"""
import json
import re
import socket
import sys
import time

from PIL import Image

READY_TIMEOUT_S = 60
# Offset from the cursor's hot spot to a pixel inside its white interior.
CURSOR_INTERIOR = (2, 5)
MOUSE_MOVES = [(40, 25), (35, -10), (60, 45), (-20, 30)]


class Qmp:
    def __init__(self, path):
        deadline = time.monotonic() + 30
        while True:
            try:
                sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
                sock.connect(path)
                break
            except OSError:
                if time.monotonic() > deadline:
                    raise
                time.sleep(0.05)
        self.stream = sock.makefile("rw")
        self.stream.readline()  # greeting
        self.command("qmp_capabilities")

    def command(self, name, **arguments):
        request = {"execute": name}
        if arguments:
            request["arguments"] = arguments
        self.stream.write(json.dumps(request) + "\n")
        self.stream.flush()
        while True:  # skip asynchronous events
            reply = json.loads(self.stream.readline())
            if "return" in reply:
                return reply["return"]
            if "error" in reply:
                raise RuntimeError(f"QMP {name} failed: {reply['error']}")


def read_log(path):
    try:
        with open(path, errors="replace") as log:
            return log.read()
    except FileNotFoundError:
        return ""


def wait_for(path, predicate, what):
    deadline = time.monotonic() + READY_TIMEOUT_S
    while time.monotonic() < deadline:
        result = predicate(read_log(path))
        if result:
            return result
        time.sleep(0.1)
    sys.exit(f"gfx-check: timed out waiting for {what}")


def last_cursor(log):
    positions = re.findall(r"gfx: cursor at (\d+) (\d+)", log)
    return tuple(map(int, positions[-1])) if positions else None


def screenshot(qmp, prefix, name):
    ppm = f"{prefix}-{name}.ppm"
    qmp.command("screendump", filename=ppm)
    time.sleep(0.3)  # the file is written asynchronously
    image = Image.open(ppm).convert("RGB")
    image.save(f"{prefix}-{name}.png")
    return image


def expect_pixel(image, position, expected, what):
    actual = image.getpixel(position)
    if actual != expected:
        sys.exit(f"gfx-check: {what} at {position}: expected {expected}, found {actual}")


def cursor_interior(position):
    return (position[0] + CURSOR_INTERIOR[0], position[1] + CURSOR_INTERIOR[1])


def main():
    socket_path, log_path, prefix = sys.argv[1:]
    qmp = Qmp(socket_path)

    log = wait_for(log_path, lambda text: text if "gfx: ready" in text else None, "the program to be ready")
    start = last_cursor(log)
    before = screenshot(qmp, prefix, "before")

    if before.size != (1280, 800):
        sys.exit(f"gfx-check: screen is {before.size}, expected 1280x800")
    patches = re.findall(r"gfx: patch (\d+) (\d+) ([0-9a-f]{6})", log)
    if len(patches) < 5:
        sys.exit("gfx-check: the program did not announce its colour patches")
    for x, y, color in patches:
        expected = tuple(int(color[i:i + 2], 16) for i in (0, 2, 4))
        expect_pixel(before, (int(x), int(y)), expected, "colour patch")
    expect_pixel(before, cursor_interior(start), (255, 255, 255), "cursor (initial position)")

    for dx, dy in MOUSE_MOVES:
        qmp.command("input-send-event", events=[
            {"type": "rel", "data": {"axis": "x", "value": dx}},
            {"type": "rel", "data": {"axis": "y", "value": dy}},
        ])
        time.sleep(0.15)

    # The guest decides how device units map to pixels; ask it where the
    # cursor ended up rather than assuming.
    time.sleep(0.5)
    end = wait_for(log_path, lambda text: last_cursor(text) if last_cursor(text) != start else None,
                   "the cursor to move")
    time.sleep(0.3)
    end = last_cursor(read_log(log_path))
    after = screenshot(qmp, prefix, "after")

    expect_pixel(after, cursor_interior(end), (255, 255, 255), "cursor (after moving)")
    if after.getpixel(cursor_interior(start)) == (255, 255, 255):
        sys.exit("gfx-check: the cursor left a copy of itself at its old position")
    # Nothing but the two cursor areas may differ between the screenshots.
    def near(point, origin):
        return origin[0] - 1 <= point[0] <= origin[0] + 13 and origin[1] - 1 <= point[1] <= origin[1] + 20
    changed = 0
    for y in range(0, 800):
        for x in range(0, 1280):
            if before.getpixel((x, y)) != after.getpixel((x, y)):
                if not (near((x, y), start) or near((x, y), end)):
                    sys.exit(f"gfx-check: pixel {(x, y)} changed although the cursor was never there")
                changed += 1
    if changed == 0:
        sys.exit("gfx-check: the screen did not change at all")

    print(f"gfx-check: ok, cursor moved from {start} to {end}, {changed} pixels changed")
    qmp.command("send-key", keys=[{"type": "qcode", "data": "q"}], **{"hold-time": 40})
    time.sleep(1.0)


if __name__ == "__main__":
    main()
