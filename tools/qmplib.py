"""Helpers shared by the check scripts that drive a QEMU guest.

They talk to QEMU's machine protocol (QMP) for screenshots and input, and
read the guest's serial log to learn what the guest is doing.
"""
import json
import re
import socket
import sys
import time

from PIL import Image

DEFAULT_TIMEOUT_S = 60


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

    def screenshot(self, prefix, name):
        """Saves <prefix>-<name>.png and returns the image."""
        ppm = f"{prefix}-{name}.ppm"
        self.command("screendump", filename=ppm)
        time.sleep(0.3)  # the file is written asynchronously
        image = Image.open(ppm).convert("RGB")
        image.save(f"{prefix}-{name}.png")
        return image

    def mouse_move(self, dx, dy):
        self.command("input-send-event", events=[
            {"type": "rel", "data": {"axis": "x", "value": dx}},
            {"type": "rel", "data": {"axis": "y", "value": dy}},
        ])

    def mouse_button(self, down, button="left"):
        self.command("input-send-event", events=[
            {"type": "btn", "data": {"button": button, "down": down}},
        ])

    def key(self, name):
        self.command("send-key", keys=[{"type": "qcode", "data": name}], **{"hold-time": 40})


def read_log(path):
    try:
        with open(path, errors="replace") as log:
            return log.read()
    except FileNotFoundError:
        return ""


def wait_for(path, predicate, what, timeout_s=DEFAULT_TIMEOUT_S):
    """Polls the serial log until predicate(text) returns something truthy."""
    deadline = time.monotonic() + timeout_s
    while time.monotonic() < deadline:
        result = predicate(read_log(path))
        if result:
            return result
        time.sleep(0.1)
    sys.exit(f"check: timed out waiting for {what}")


def expect_pixel(image, position, expected, what):
    actual = image.getpixel(position)
    if actual != expected:
        sys.exit(f"check: {what} at {position}: expected {expected}, found {actual}")


def hex_color(text):
    return tuple(int(text[i:i + 2], 16) for i in (0, 2, 4))


class Pointer:
    """Tracks where the guest's pointer is while moving it in small steps.

    PS/2 mouse packets carry at most +-255 per axis, and a real hand does
    not teleport either; large moves are sent as a series of small ones.
    """

    def __init__(self, qmp, x, y):
        self.qmp, self.x, self.y = qmp, x, y

    def move_to(self, x, y, step=40, pause_s=0.03):
        while (self.x, self.y) != (x, y):
            dx = max(-step, min(step, x - self.x))
            dy = max(-step, min(step, y - self.y))
            self.qmp.mouse_move(dx, dy)
            self.x += dx
            self.y += dy
            time.sleep(pause_s)

    def press(self):
        self.qmp.mouse_button(True)
        time.sleep(0.15)

    def release(self):
        self.qmp.mouse_button(False)
        time.sleep(0.15)

    def click(self):
        self.press()
        self.release()


def find_all(pattern, text):
    return re.findall(pattern, text)
