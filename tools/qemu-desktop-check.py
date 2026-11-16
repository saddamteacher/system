#!/usr/bin/env python3
"""Drives and verifies the desktop check.

usage: qemu-desktop-check.py <qmp-socket> <serial-log> <output-prefix>

Works with user/tests/desktop-session.c, which starts the window server and
the desktop shell. The script looks at the wallpaper, menu bar and dock,
launches applications from the dock, and exercises shadows, rounded
corners, translucency, minimize and restore, zoom and resize. It ends by
pressing F12, which makes the server exit successfully; if any check fails
the key is never pressed and the test times out.
"""
import re
import sys
import time

from qmplib import Pointer, Qmp, find_all, hex_color, read_log, wait_for

TITLE_BAR_HEIGHT = 28
MENU_BAR_HEIGHT = 26
FALLBACK_BACKGROUND = (58, 66, 98)  # what the server paints with no wallpaper
MIN_FPS = 55.0


def luminance(pixel):
    red, green, blue = pixel
    return 0.299 * red + 0.587 * green + 0.114 * blue


def fail(message):
    sys.exit(f"check: {message}")


def window_named(log, title):
    """The most recent geometry the server logged for a window."""
    created = find_all(rf"compositor: window (\d+) '{title}' at (-?\d+) (-?\d+) size (\d+)x(\d+)", log)
    if not created:
        return None
    number, x, y, width, height = map(int, created[-1])
    window = dict(id=number, x=x, y=y, width=width, height=height)
    for kind, a, b, c, d in find_all(
            rf"compositor: window {number} (moved to|resized to) (-?\d+)[ x](-?\d+)(?: at (-?\d+) (-?\d+))?", log):
        if kind == "moved to":
            window.update(x=int(a), y=int(b))
        else:
            window.update(width=int(a), height=int(b), x=int(c), y=int(d))
    return window


def main():
    socket_path, log_path, prefix = sys.argv[1:]
    qmp = Qmp(socket_path)
    log = wait_for(log_path, lambda text: text if "desktop: ready" in text else None, "the desktop to be ready")
    time.sleep(1.0)

    shelf = re.search(r"dock shelf at (\d+) (\d+) size (\d+)x(\d+), (\d+) items, first icon at (\d+) (\d+)", log)
    if not shelf:
        fail("the desktop did not report where the dock is")
    shelf_x, shelf_y, shelf_width, shelf_height, _, icon_x, icon_y = map(int, shelf.groups())
    icon_centers = [(icon_x + 24 + i * 62, icon_y + 24) for i in range(3)]
    # A point on the shelf itself: left of the first icon.
    shelf_probe = (shelf_x + 5, shelf_y + shelf_height // 2)

    # ---- 1. The empty desktop -------------------------------------------
    empty = qmp.screenshot(prefix, "1-desktop")
    corner, far = empty.getpixel((100, 400)), empty.getpixel((1100, 700))
    if corner == FALLBACK_BACKGROUND or corner == far:
        fail(f"no wallpaper: pixels {corner} and {far}")
    # The menu bar is light glass over a dark sky: much brighter than the
    # wallpaper just below it, yet not solid white.
    bar, below_bar = empty.getpixel((640, MENU_BAR_HEIGHT // 2)), empty.getpixel((640, MENU_BAR_HEIGHT + 20))
    if not (luminance(bar) > luminance(below_bar) + 60 and luminance(bar) < 250):
        fail(f"menu bar does not look translucent: bar {bar}, wallpaper below {below_bar}")
    beside_shelf = empty.getpixel((shelf_x - 12, shelf_probe[1]))
    if not luminance(empty.getpixel(shelf_probe)) > luminance(beside_shelf) + 15:
        fail(f"dock shelf not visible: shelf {empty.getpixel(shelf_probe)}, beside it {beside_shelf}")
    red, green, blue = empty.getpixel((icon_centers[0][0], icon_centers[0][1] - 18))
    if not (blue > 180 and blue > red + 60):
        fail(f"first dock icon is not the blue one: {(red, green, blue)}")
    if "desktop: active application is Saddev" not in log:
        fail("the menu bar did not start with the default application name")

    # ---- 2. Launch from the dock: shadow and rounded corners -------------
    pointer = Pointer(qmp, 640, 400)
    pointer.move_to(*icon_centers[0])
    pointer.click()
    log = wait_for(log_path, lambda text: text if "guidemo Alpha: shown" in text else None,
                   "Alpha to start from the dock")
    wait_for(log_path, lambda text: "desktop: active application is Alpha" in text,
             "the menu bar to show the new application")
    # Off the dock, so its icons are back at their resting size and place.
    pointer.move_to(900, 300)
    wait_for(log_path, lambda text: "bounce finished for Alpha" in text, "the launch bounce to end")
    time.sleep(0.8)
    alpha = window_named(read_log(log_path), "Alpha")
    colours = {title: dict(marker=hex_color(marker), background=hex_color(background))
               for title, marker, background in
               find_all(r"guidemo (\w+): marker ([0-9a-f]{6}) background ([0-9a-f]{6})", read_log(log_path))}
    shot = qmp.screenshot(prefix, "2-window")

    bottom = alpha["y"] + alpha["height"]
    middle = alpha["x"] + alpha["width"] // 2
    under = (middle, bottom + 8)
    if not luminance(shot.getpixel(under)) < luminance(empty.getpixel(under)) - 8:
        fail(f"no shadow under the window: {shot.getpixel(under)} vs wallpaper {empty.getpixel(under)}")
    far_under = (middle, bottom + 60)
    if abs(luminance(shot.getpixel(far_under)) - luminance(empty.getpixel(far_under))) > 3:
        fail("the shadow reaches implausibly far")
    # Bottom corners are rounded: the extreme corner pixel is not window.
    background = colours["Alpha"]["background"]
    if shot.getpixel((alpha["x"], bottom - 1)) == background:
        fail("the window's bottom-left corner is square")
    if shot.getpixel((alpha["x"] + 14, bottom - 3)) != background:
        fail("the window's content is missing near its bottom-left corner")
    if shot.getpixel((alpha["x"], alpha["y"] - TITLE_BAR_HEIGHT)) == shot.getpixel((middle + 80, alpha["y"] - 8)):
        fail("the title bar's top-left corner is square")
    # The dock now marks Alpha as running: a dark dot under its icon.
    dot = (icon_centers[0][0], shelf_y + shelf_height - 5)
    if not luminance(shot.getpixel(dot)) < luminance(empty.getpixel(dot)) - 40:
        fail("no running indicator under the dock icon")

    # ---- 3. Translucency: a window behind the dock shows through ---------
    grab = (alpha["x"] + 150, alpha["y"] - TITLE_BAR_HEIGHT // 2)
    target = (shelf_x - 120, shelf_y - 100)  # new window origin
    pointer.move_to(*grab)
    pointer.press()
    pointer.move_to(grab[0] + target[0] - alpha["x"], grab[1] + target[1] - alpha["y"], step=14, pause_s=0.02)
    pointer.release()
    wait_for(log_path, lambda text: f"window {alpha['id']} moved to {target[0]} {target[1]}" in text,
             "Alpha to be dragged behind the dock")
    pointer.move_to(900, 300)  # out of the way
    time.sleep(0.5)
    behind = qmp.screenshot(prefix, "3-behind-dock")
    through = behind.getpixel(shelf_probe)
    if not luminance(through) > luminance(empty.getpixel(shelf_probe)) + 15:
        fail(f"the dock does not show the window behind it: {through} vs {empty.getpixel(shelf_probe)}")
    if through == background:
        fail("the window is in front of the dock, not behind it")
    red, green, blue = behind.getpixel((icon_centers[0][0], icon_centers[0][1] - 18))
    if not (blue > 180 and blue > red + 60):
        fail("the dock icon is hidden by the window")
    alpha = window_named(read_log(log_path), "Alpha")

    # ---- 4. Minimize and restore ----------------------------------------
    marker_at = (alpha["x"] + 12, alpha["y"] + 12)
    pointer.move_to(alpha["x"] + 38, alpha["y"] - TITLE_BAR_HEIGHT // 2)  # the yellow button
    pointer.click()
    wait_for(log_path, lambda text: f"window {alpha['id']} minimized" in text, "Alpha to be minimized")
    wait_for(log_path, lambda text: text.rstrip().endswith("active application is Saddev")
             or "active application is Saddev" in text.split("minimized")[-1],
             "the menu bar to fall back to the default name")
    pointer.move_to(900, 300)
    time.sleep(1.0)  # the minimize animation takes a third of a second
    hidden = qmp.screenshot(prefix, "4-minimized")
    if hidden.getpixel(marker_at) == colours["Alpha"]["marker"]:
        fail("the minimized window is still on screen")
    if not luminance(hidden.getpixel(dot)) < luminance(empty.getpixel(dot)) - 40:
        fail("the dock lost the indicator of a minimized application")
    pointer.move_to(*icon_centers[0])
    time.sleep(0.6)  # the icons shift as they grow; let them settle
    pointer.click()
    wait_for(log_path, lambda text: f"window {alpha['id']} restored" in text, "Alpha to be restored from the dock")
    pointer.move_to(900, 300)
    time.sleep(1.0)
    restored = qmp.screenshot(prefix, "5-restored")
    if restored.getpixel(marker_at) != colours["Alpha"]["marker"]:
        fail("the restored window is not back where it was")

    # ---- 5. Zoom to the work area and back ------------------------------
    pointer.move_to(alpha["x"] + 58, alpha["y"] - TITLE_BAR_HEIGHT // 2)  # the green button
    pointer.click()
    work_height = shelf_y - MENU_BAR_HEIGHT - TITLE_BAR_HEIGHT
    zoomed_line = f"window {alpha['id']} resized to 1280x{work_height} at 0 {MENU_BAR_HEIGHT + TITLE_BAR_HEIGHT}"
    wait_for(log_path, lambda text: zoomed_line in text, f"Alpha to fill the work area ({zoomed_line})")
    time.sleep(0.5)
    zoomed = qmp.screenshot(prefix, "6-zoomed")
    if zoomed.getpixel((12, MENU_BAR_HEIGHT + TITLE_BAR_HEIGHT + 12)) != colours["Alpha"]["marker"]:
        fail("the zoomed window is not at the top-left of the work area")
    if zoomed.getpixel((1270, shelf_y - 30)) != background:
        fail("the zoomed window does not reach the right edge")
    pointer.move_to(58, MENU_BAR_HEIGHT + TITLE_BAR_HEIGHT // 2)
    pointer.click()
    restored_line = f"window {alpha['id']} resized to {alpha['width']}x{alpha['height']} at {alpha['x']} {alpha['y']}"
    wait_for(log_path, lambda text: restored_line in text, "Alpha to return to its previous size and place")

    # ---- 6. Resize by dragging the corner -------------------------------
    # Bring the window fully on screen first: its bottom edge is still
    # behind the dock and partly below the screen.
    grab = (alpha["x"] + 150, alpha["y"] - TITLE_BAR_HEIGHT // 2)
    pointer.move_to(*grab)
    pointer.press()
    pointer.move_to(grab[0] - 120, grab[1] - 380, step=14, pause_s=0.02)
    pointer.release()
    alpha.update(x=alpha["x"] - 120, y=alpha["y"] - 380)
    wait_for(log_path, lambda text: f"window {alpha['id']} moved to {alpha['x']} {alpha['y']}" in text,
             "Alpha to be dragged back up")
    corner_grab = (alpha["x"] + alpha["width"] - 4, alpha["y"] + alpha["height"] - 4)
    pointer.move_to(*corner_grab)
    pointer.press()
    pointer.move_to(corner_grab[0] + 90, corner_grab[1] - 60, step=10, pause_s=0.02)
    pointer.release()
    resized_line = f"window {alpha['id']} resized to {alpha['width'] + 90}x{alpha['height'] - 60}"
    wait_for(log_path, lambda text: resized_line in text, f"Alpha to reach its dragged size ({resized_line})")
    pointer.move_to(900, 300)
    time.sleep(0.5)
    resized = qmp.screenshot(prefix, "7-resized")
    if resized.getpixel((alpha["x"] + alpha["width"] + 60, alpha["y"] + 40)) != background:
        fail("the resized window was not redrawn at its new width")

    # ---- 7. A second application, for the final picture -----------------
    pointer.move_to(*icon_centers[1])
    pointer.click()
    wait_for(log_path, lambda text: "guidemo Beta: shown" in text, "Beta to start from the dock")
    pointer.move_to(900, 420)
    time.sleep(1.5)
    qmp.screenshot(prefix, "8-final")

    rates = [float(rate) for rate in find_all(r"compositor: ([\d.]+) fps", read_log(log_path))]
    steady = rates[1:]
    print(f"desktop-check: frame rates {rates}")
    if len(steady) < 3 or min(steady) < MIN_FPS:
        fail(f"frame rate too low or too few samples: {rates}")

    print(f"desktop-check: ok, minimum {min(steady)} fps")
    qmp.key("f12")
    time.sleep(1.0)


if __name__ == "__main__":
    main()
