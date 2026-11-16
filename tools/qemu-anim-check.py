#!/usr/bin/env python3
"""Drives and verifies the animation check.

usage: qemu-anim-check.py <qmp-socket> <serial-log> <output-prefix>

Works with user/tests/desktop-session.c in "slow" mode, where window
animations run six times slower than normal. That leaves time to take a
screenshot in the middle of each one. For every animation the script checks
that the picture half-way through really is in between (smaller, displaced
or semi-transparent) and that the duration the server measured matches the
intended one. Durations are checked against the clock, not frame counts:
that is the point of a time-based animation system.
"""
import re
import sys
import time

from qmplib import Pointer, Qmp, find_all, hex_color, read_log, wait_for

TITLE_BAR_HEIGHT = 28
MENU_BAR_HEIGHT = 26
SCALE = 6
# Nominal durations in ms, times the slow-down, and the accepted deviation.
DURATIONS = {"open": 220 * SCALE, "close": 180 * SCALE, "minimize": 340 * SCALE, "restore": 340 * SCALE}
DURATION_TOLERANCE = 0.10
MIN_FPS = 50.0


def fail(message):
    sys.exit(f"check: {message}")


def changed_box(image, reference, top=MENU_BAR_HEIGHT, step=2, region=None):
    """Bounding box of pixels that differ between two screenshots, ignoring
    the menu bar (its clock may tick over in between). `region` limits the
    comparison to (left, top, right, bottom)."""
    width, height = image.size
    left, right, bottom = 0, width, height
    if region is not None:
        left, top, right, bottom = max(0, region[0]), max(top, region[1]), min(width, region[2]), min(height, region[3])
    box = None
    a, b = image.load(), reference.load()
    for y in range(top, bottom, step):
        for x in range(left, right, step):
            if a[x, y] != b[x, y]:
                box = (x, y, x, y) if box is None else (min(box[0], x), min(box[1], y), max(box[2], x), max(box[3], y))
    return box


def animation_result(log, kind, window_id):
    found = find_all(rf"compositor: animation {kind} window {window_id} took (\d+) ms in (\d+) frames", log)
    return tuple(map(int, found[-1])) if found else None


def expect_duration(log_path, kind, window_id):
    took, frames = wait_for(log_path, lambda text: animation_result(text, kind, window_id),
                            f"the {kind} animation to finish", timeout_s=30)
    wanted = DURATIONS[kind]
    if abs(took - wanted) > wanted * DURATION_TOLERANCE:
        fail(f"{kind} animation took {took} ms, expected about {wanted}")
    # A sanity check only; the test of correctness is the duration above.
    if frames < 20:
        fail(f"{kind} animation was drawn in only {frames} frames")
    print(f"anim-check: {kind}: {took} ms (target {wanted}), {frames} frames")


def main():
    socket_path, log_path, prefix = sys.argv[1:]
    qmp = Qmp(socket_path)
    log = wait_for(log_path, lambda text: text if "desktop: ready" in text else None, "the desktop to be ready")
    time.sleep(1.0)

    shelf = re.search(r"dock shelf at (\d+) (\d+) size (\d+)x(\d+), (\d+) items, first icon at (\d+) (\d+)", log)
    shelf_x, shelf_y, shelf_width, shelf_height, _, icon_x, icon_y = map(int, shelf.groups())
    icon_centers = [(icon_x + 24 + i * 62, icon_y + 24) for i in range(3)]
    pointer = Pointer(qmp, 640, 400)
    resting = qmp.screenshot(prefix, "1-resting")

    # ---- Dock magnification ---------------------------------------------
    # Above the middle icon's resting top edge there is only shelf; once
    # the pointer is over the icon it has grown to cover that spot.
    above_icon = (icon_centers[1][0], icon_y - 12)
    red, green, blue = resting.getpixel(above_icon)
    if red > 200 and green > 120 and blue < 110:
        fail("the middle dock icon is already large before the pointer comes near")
    pointer.move_to(*icon_centers[1])
    wait_for(log_path, lambda text: "desktop: dock magnified" in text, "the dock icons to finish growing")
    magnified = qmp.screenshot(prefix, "2-magnified")
    red, green, blue = magnified.getpixel(above_icon)
    if not (red > 200 and 90 < green < 220 and blue < 110):
        fail(f"the icon under the pointer did not grow: {(red, green, blue)} above it")
    percent = int(find_all(r"largest icon (\d+) percent", read_log(log_path))[-1])
    if not 150 <= percent <= 160:
        fail(f"largest icon is {percent} percent of normal, expected about 155")
    # Neighbours grow less and are pushed aside: the row got wider overall.
    box = changed_box(magnified, resting)
    if box is None or box[2] - box[0] < shelf_width - 10:
        fail(f"magnification changed too small an area: {box}")
    pointer.move_to(900, 300)
    time.sleep(1.2)
    settled = qmp.screenshot(prefix, "3-settled")
    leftover = changed_box(settled, resting, step=1)
    # Only the pointer itself differs (it was at 640,400 and is at 900,300).
    if leftover is not None and not (leftover[0] >= 630 and leftover[3] <= 430):
        fail(f"the dock did not return to its resting state: differences in {leftover}")
    print(f"anim-check: magnification: {percent} percent, back to rest afterwards")

    # ---- Launch: bounce, then the window opens --------------------------
    pointer.move_to(*icon_centers[0])
    time.sleep(0.6)
    pointer.click()
    wait_for(log_path, lambda text: "desktop: bounce started for Alpha" in text, "the launch bounce to start")
    pointer.move_to(900, 300)
    log = wait_for(log_path, lambda text: text if re.search(r"compositor: window \d+ 'Alpha'", text) else None,
                   "Alpha's window to be created")
    number, x, y, width, height = map(int, re.search(
        r"compositor: window (\d+) 'Alpha' at (-?\d+) (-?\d+) size (\d+)x(\d+)", log).groups())
    # The animation starts with the window's first drawing, which this
    # line follows immediately; photograph it early, while it is small.
    wait_for(log_path, lambda text: "guidemo Alpha: marker" in text, "Alpha to draw")
    opening = qmp.screenshot(prefix, "4-opening")
    expect_duration(log_path, "open", number)
    bounce = wait_for(log_path, lambda text: find_all(r"bounce finished for Alpha after (\d+) ms", text),
                      "the launch bounce to end")
    if int(bounce[-1]) % 420 > 60:
        fail(f"the bounce ended {int(bounce[-1]) % 420} ms into a hop instead of on the ground")
    time.sleep(0.5)
    opened = qmp.screenshot(prefix, "5-opened")

    colours = find_all(r"guidemo Alpha: marker ([0-9a-f]{6}) background ([0-9a-f]{6})", read_log(log_path))[-1]
    marker, background = hex_color(colours[0]), hex_color(colours[1])
    # A point well inside the window, on plain background.
    probe = (x + width - 60, y + 140)
    if opened.getpixel(probe) != background:
        fail(f"the opened window does not show its background at {probe}: {opened.getpixel(probe)}")
    during = opening.getpixel(probe)
    if during == background or during == resting.getpixel(probe):
        fail(f"half-way through opening, {probe} is {during}: not a blend of window and wallpaper")
    # While opening the window is smaller than its final frame: looking
    # only at the frame's area, the changed pixels stop short of its edges.
    frame = (x, y - TITLE_BAR_HEIGHT, x + width, y + height)
    box = changed_box(opening, resting, step=1, region=frame)
    if box is None or not (box[0] >= frame[0] + 2 and box[2] <= frame[2] - 3):
        fail(f"the opening window is not scaled down: changed area {box} within frame {frame}")
    print(f"anim-check: open: mid-way pixel {during} between wallpaper and {background}")

    # ---- Minimize into the dock -----------------------------------------
    frame_center = (x + width // 2, y - TITLE_BAR_HEIGHT + (height + TITLE_BAR_HEIGHT) // 2)
    pointer.move_to(x + 38, y - TITLE_BAR_HEIGHT // 2)  # the yellow button
    pointer.click()
    pointer.move_to(900, 300)
    time.sleep(0.75)
    minimizing = qmp.screenshot(prefix, "6-minimizing")
    expect_duration(log_path, "minimize", number)
    time.sleep(0.5)
    minimized = qmp.screenshot(prefix, "7-minimized")
    box = changed_box(minimizing, minimized)
    if box is None:
        fail("nothing was on screen half-way through minimizing")
    box_width, box_height = box[2] - box[0], box[3] - box[1]
    box_center = ((box[0] + box[2]) // 2, (box[1] + box[3]) // 2)
    if not (20 < box_width < width - 30 and 20 < box_height < height):
        fail(f"the minimizing window is not shrinking: it covers {box_width}x{box_height}")
    # It travels from the window's place towards its dock icon.
    if not (frame_center[1] + 30 < box_center[1] < icon_centers[0][1] - 10):
        fail(f"the minimizing window is not between its place and the dock: centre {box_center}")
    if not (min(frame_center[0], icon_centers[0][0]) - 10 < box_center[0] < max(frame_center[0], icon_centers[0][0]) + 10):
        fail(f"the minimizing window is not heading for its dock icon: centre {box_center}")
    if minimized.getpixel((x + 12, y + 12)) == marker:
        fail("the window is still on screen after minimizing")
    print(f"anim-check: minimize: mid-way it covered {box_width}x{box_height} around {box_center}")

    # ---- Restore from the dock ------------------------------------------
    pointer.move_to(*icon_centers[0])
    time.sleep(0.6)
    pointer.click()
    pointer.move_to(900, 300)
    time.sleep(0.6)
    restoring = qmp.screenshot(prefix, "8-restoring")
    expect_duration(log_path, "restore", number)
    time.sleep(0.5)
    restored = qmp.screenshot(prefix, "9-restored")
    box = changed_box(restoring, minimized)
    if box is None or not (20 < box[2] - box[0] < width - 30):
        fail(f"the restoring window is not growing back: {box}")
    if restored.getpixel((x + 12, y + 12)) != marker:
        fail("the restored window is not back in its place")

    # ---- Close ----------------------------------------------------------
    pointer.move_to(x + 18, y - TITLE_BAR_HEIGHT // 2)  # the red button
    pointer.click()
    pointer.move_to(900, 300)
    time.sleep(0.4)
    closing = qmp.screenshot(prefix, "10-closing")
    expect_duration(log_path, "close", number)
    time.sleep(0.5)
    closed = qmp.screenshot(prefix, "11-closed")
    during = closing.getpixel(probe)
    if during == background or during == closed.getpixel(probe):
        fail(f"half-way through closing, {probe} is {during}: not a blend of window and wallpaper")
    frame = (x - 30, y - TITLE_BAR_HEIGHT - 30, x + width + 30, y + height + 40)
    if changed_box(closed, resting, step=1, region=frame) is not None:
        fail("the closed window left something behind")
    print(f"anim-check: close: mid-way pixel {during}")

    # ---- Frame rate during all of the above -----------------------------
    rates = [float(rate) for rate in find_all(r"compositor: ([\d.]+) fps", read_log(log_path))]
    print(f"anim-check: frame rates {rates}")
    if rates and min(rates) < MIN_FPS:
        fail(f"frame rate dropped to {min(rates)} during animations")

    print("anim-check: ok")
    qmp.key("f12")
    time.sleep(1.0)


if __name__ == "__main__":
    main()
