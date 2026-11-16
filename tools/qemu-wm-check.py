#!/usr/bin/env python3
"""Drives and verifies the window server check.

usage: qemu-wm-check.py <qmp-socket> <serial-log> <output-prefix>

Works with user/tests/wm-session.c, which starts the window server and two
demo windows, "Alpha" and "Beta", that overlap. The script checks what is
on screen, clicks Alpha to raise it, drags it by its title bar, checks
again, and looks at the frame rates the server reports. Finally it presses
F12, which makes the server exit successfully. If any check fails the key
is never pressed and the test times out.
"""
import re
import sys
import time

from qmplib import Pointer, Qmp, expect_pixel, find_all, hex_color, read_log, wait_for

TITLE_BAR_HEIGHT = 28
TITLE_FOCUSED = (232, 232, 237)
TITLE_UNFOCUSED = (244, 244, 247)
MARKER_OFFSET = 12  # a point inside each window's corner marker
# Minimum acceptable frame rate while two windows animate and one is being
# dragged. The target is 60; the margin allows for timing jitter when QEMU
# runs without hardware virtualisation.
MIN_FPS = 55.0


def windows_in(log):
    found = {}
    for number, title, x, y, width, height in find_all(
            r"compositor: window (\d+) '(\w+)' at (-?\d+) (-?\d+) size (\d+)x(\d+)", log):
        found[title] = dict(id=int(number), x=int(x), y=int(y), width=int(width), height=int(height))
    return found


def colours_in(log):
    return {title: dict(marker=hex_color(marker), background=hex_color(background))
            for title, marker, background in
            find_all(r"guidemo (\w+): marker ([0-9a-f]{6}) background ([0-9a-f]{6})", log)}


def main():
    socket_path, log_path, prefix = sys.argv[1:]
    qmp = Qmp(socket_path)

    wait_for(log_path, lambda log: log.count(": shown") >= 2, "both demo windows to appear")
    time.sleep(2.5)  # let the frame rate statistics settle
    log = read_log(log_path)
    windows, colours = windows_in(log), colours_in(log)
    if set(windows) != {"Alpha", "Beta"} or set(colours) != {"Alpha", "Beta"}:
        sys.exit(f"check: expected windows Alpha and Beta, found {sorted(windows)}")
    alpha, beta = windows["Alpha"], windows["Beta"]

    # 1. Both windows are drawn; Beta was created last, so it is on top and
    #    focused, and Alpha's title bar is the unfocused colour.
    first = qmp.screenshot(prefix, "1-initial")
    for title, window in windows.items():
        expect_pixel(first, (window["x"] + MARKER_OFFSET, window["y"] + MARKER_OFFSET), colours[title]["marker"],
                     f"{title} corner marker")
    expect_pixel(first, (beta["x"] + beta["width"] - 30, beta["y"] - TITLE_BAR_HEIGHT // 2), TITLE_FOCUSED,
                 "Beta title bar (focused)")
    expect_pixel(first, (alpha["x"] + 150, alpha["y"] - TITLE_BAR_HEIGHT // 2), TITLE_UNFOCUSED,
                 "Alpha title bar (unfocused)")
    # A point that lies inside both windows shows the one on top.
    overlap = (beta["x"] + MARKER_OFFSET, beta["y"] + MARKER_OFFSET)
    if not (alpha["x"] <= overlap[0] < alpha["x"] + alpha["width"]
            and alpha["y"] <= overlap[1] < alpha["y"] + alpha["height"]):
        sys.exit("check: the demo windows do not overlap as the test assumes")

    # 2. Clicking Alpha raises and focuses it: the shared point now shows
    #    Alpha's background instead of Beta's marker.
    pointer = Pointer(qmp, 640, 400)
    grab = (alpha["x"] + 150, alpha["y"] - TITLE_BAR_HEIGHT // 2)
    pointer.move_to(*grab)
    pointer.click()
    wait_for(log_path, lambda text: f"pointer down at {grab[0]} {grab[1]}" in text,
             "the click to arrive where it was aimed")
    time.sleep(0.4)
    second = qmp.screenshot(prefix, "2-raised")
    expect_pixel(second, overlap, colours["Alpha"]["background"], "overlap after raising Alpha")
    # Not at the grab point itself: the pointer is drawn there.
    expect_pixel(second, (grab[0] + 150, grab[1]), TITLE_FOCUSED, "Alpha title bar (now focused)")
    expect_pixel(second, (beta["x"] + beta["width"] - 30, beta["y"] - TITLE_BAR_HEIGHT // 2), TITLE_UNFOCUSED,
                 "Beta title bar (now unfocused)")

    # 3. Drag Alpha by its title bar.
    delta = (170, 130)
    pointer.press()
    pointer.move_to(grab[0] + delta[0], grab[1] + delta[1], step=12, pause_s=0.02)
    pointer.release()
    moved_to = (alpha["x"] + delta[0], alpha["y"] + delta[1])
    wait_for(log_path, lambda text: f"window {alpha['id']} moved to {moved_to[0]} {moved_to[1]}" in text,
             "Alpha to arrive where it was dragged")
    time.sleep(0.4)
    third = qmp.screenshot(prefix, "3-dragged")
    expect_pixel(third, (moved_to[0] + MARKER_OFFSET, moved_to[1] + MARKER_OFFSET), colours["Alpha"]["marker"],
                 "Alpha corner marker after the drag")
    # Where Alpha used to be and nothing else is, the background is back:
    # no trail of old window pixels.
    vacated = (alpha["x"] + 5, alpha["y"] + 5)
    if third.getpixel(vacated) in (colours["Alpha"]["marker"], colours["Alpha"]["background"]):
        sys.exit(f"check: Alpha left pixels behind at {vacated}")

    # 4. Frame rate, as measured by the server itself.
    rates = [float(rate) for rate in find_all(r"compositor: ([\d.]+) fps", read_log(log_path))]
    if len(rates) < 3:
        sys.exit("check: the server reported fewer than three frame rate samples")
    steady = rates[1:]  # the first sample includes start-up
    print(f"wm-check: frame rates {rates}")
    if min(steady) < MIN_FPS:
        sys.exit(f"check: frame rate dropped to {min(steady)} (minimum {MIN_FPS})")

    print(f"wm-check: ok, Alpha raised and dragged to {moved_to}, minimum {min(steady)} fps")
    qmp.key("f12")
    time.sleep(1.0)


if __name__ == "__main__":
    main()
