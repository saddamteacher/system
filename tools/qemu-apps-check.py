#!/usr/bin/env python3
"""Drives and verifies the applications check.

usage: qemu-apps-check.py <qmp-socket> <serial-log> <output-prefix>

Works with user/tests/desktop-session.c in "apps" mode: the real desktop
with Terminal, Files, Settings and About in the dock. The script starts
each from the dock and uses it the way a person would: types commands into
the terminal, browses to a file, flips settings and checks their effect on
the desktop. It ends by pressing F12; if any check fails the key is never
pressed and the test times out.
"""
import re
import sys
import time

from qmplib import Pointer, Qmp, find_all, read_log, wait_for

TITLE_BAR_HEIGHT = 28
MENU_BAR_HEIGHT = 26
KEY_NAMES = {" ": "spc", "/": "slash", "-": "minus", ".": "dot", "\n": "ret"}


def fail(message):
    sys.exit(f"check: {message}")


def type_text(qmp, text):
    for character in text:
        qmp.key(KEY_NAMES.get(character, character))
        time.sleep(0.07)


def window_named(log, title):
    found = find_all(rf"compositor: window (\d+) '{title}' at (-?\d+) (-?\d+) size (\d+)x(\d+)", log)
    if not found:
        return None
    number, x, y, width, height = map(int, found[-1])
    return dict(id=number, x=x, y=y, width=width, height=height)


def changed_pixels(image, reference, region):
    a, b = image.load(), reference.load()
    return sum(1 for y in range(region[1], region[3]) for x in range(region[0], region[2]) if a[x, y] != b[x, y])


def launch(qmp, pointer, log_path, icon_center, ready_line, title, window_title=None):
    """Starts an application from the dock; returns its window geometry.
    `window_title` is the title its window carries, if not the app's name."""
    window_title = re.escape(window_title or title)
    pointer.move_to(*icon_center)
    time.sleep(0.6)
    pointer.click()
    pointer.move_to(1000, 120)
    wait_for(log_path, lambda text: ready_line in text, f"{title} to start ('{ready_line}')")
    window = wait_for(log_path, lambda text: window_named(text, window_title), f"{title}'s window")
    wait_for(log_path, lambda text: f"animation open window {window['id']}" in text, f"{title} to finish opening")
    time.sleep(0.4)
    return window


def close_window(pointer, log_path, window, title):
    pointer.move_to(window["x"] + 18, window["y"] - TITLE_BAR_HEIGHT // 2)  # the red button
    pointer.click()
    pointer.move_to(1000, 120)
    wait_for(log_path, lambda text: f"animation close window {window['id']}" in text, f"{title} to close")


def main():
    socket_path, log_path, prefix = sys.argv[1:]
    qmp = Qmp(socket_path)
    log = wait_for(log_path, lambda text: text if "desktop: ready" in text else None, "the desktop to be ready")
    time.sleep(1.0)

    shelf = re.search(r"dock shelf at (\d+) (\d+) size (\d+)x(\d+), (\d+) items, first icon at (\d+) (\d+)", log)
    shelf_x, shelf_y, shelf_width, shelf_height, items, icon_x, icon_y = map(int, shelf.groups())
    if items != 4:
        fail(f"the dock has {items} items, expected the four applications")
    icons = {name: (icon_x + 24 + i * 62, icon_y + 24)
             for i, name in enumerate(["Terminal", "Files", "Settings", "About"])}
    pointer = Pointer(qmp, 640, 400)
    pointer.move_to(1000, 120)
    time.sleep(0.5)
    resting = qmp.screenshot(prefix, "1-desktop")

    # ---- Terminal --------------------------------------------------------
    terminal = launch(qmp, pointer, log_path, icons["Terminal"], "terminal: started", "Terminal")
    wait_for(log_path, lambda text: "desktop: active application is Terminal" in text,
             "the menu bar to name the terminal")
    text_area = (terminal["x"] + 6, terminal["y"] + 6, terminal["x"] + terminal["width"] - 6,
                 terminal["y"] + terminal["height"] - 6)
    time.sleep(0.5)
    fresh = qmp.screenshot(prefix, "2-terminal")
    if changed_pixels(fresh, resting, (text_area[0], text_area[1], text_area[0] + 400, text_area[1] + 40)) < 200:
        fail("the terminal shows no greeting or prompt")

    # A command whose effect can be seen from outside: it writes to the log.
    type_text(qmp, "log hello-from-the-terminal\n")
    wait_for(log_path, lambda text: "sh: hello-from-the-terminal" in text, "the shell to run the typed command")
    # Commands that print: the text area fills up further down.
    type_text(qmp, "cat /etc/motd\n")
    type_text(qmp, "ls /\n")
    type_text(qmp, "mem\n")
    time.sleep(0.8)
    used = qmp.screenshot(prefix, "3-terminal-used")
    lower_lines = (text_area[0], text_area[1] + 60, text_area[0] + 500, text_area[1] + 260)
    if changed_pixels(used, fresh, lower_lines) < 1500:
        fail("typed commands produced no visible output")
    # Leaving the shell closes the terminal.
    type_text(qmp, "exit\n")
    wait_for(log_path, lambda text: f"animation close window {terminal['id']}" in text,
             "the terminal to close when its shell exits")

    # ---- Files -----------------------------------------------------------
    files = launch(qmp, pointer, log_path, icons["Files"], "files: showing /", "Files", window_title="/")
    first_row_y = files["y"] + 40 + 13  # toolbar, then half a row
    listing = qmp.screenshot(prefix, "4-files")
    # Directories come first, in order: bin, etc, fonts, test.
    pointer.move_to(files["x"] + 120, first_row_y + 26)
    pointer.click()
    wait_for(log_path, lambda text: "files: entered /etc (2 entries)" in text, "Files to enter /etc")
    pointer.move_to(files["x"] + 120, first_row_y)  # motd, the first of two files
    pointer.click()
    wait_for(log_path, lambda text: "files: opened /etc/motd (67 bytes)" in text, "Files to open /etc/motd")
    pointer.move_to(1000, 120)
    time.sleep(0.4)
    viewing = qmp.screenshot(prefix, "5-file-open")
    body = (files["x"] + 8, files["y"] + 44, files["x"] + 420, files["y"] + 100)
    if changed_pixels(viewing, listing, body) < 300:
        fail("the opened file's text is not shown")
    # Back twice: out of the file, then out of /etc.
    for _ in range(2):
        pointer.move_to(files["x"] + 40, files["y"] + 20)
        pointer.click()
        time.sleep(0.3)
    pointer.move_to(1000, 120)
    time.sleep(0.4)
    back = qmp.screenshot(prefix, "6-files-back")
    if changed_pixels(back, listing, (files["x"], files["y"] + 40, files["x"] + files["width"], files["y"] + 150)) > 0:
        fail("going back twice did not return to the first listing")
    close_window(pointer, log_path, files, "Files")

    # ---- Settings --------------------------------------------------------
    settings = launch(qmp, pointer, log_path, icons["Settings"], "settings: shown, connected to the desktop",
                      "Settings")
    switch_x = settings["x"] + settings["width"] - 24 - 20
    row = lambda index: settings["y"] + 20 + index * 48 + 24
    before = qmp.screenshot(prefix, "7-settings")

    pointer.move_to(switch_x, row(0))  # magnification
    pointer.click()
    wait_for(log_path, lambda text: "desktop: settings: magnification off" in text, "magnification to be switched off")
    above_icon = (icons["Files"][0], icon_y - 12)
    pointer.move_to(*icons["Files"])
    time.sleep(1.0)
    hovering = qmp.screenshot(prefix, "8-no-magnification")
    if hovering.getpixel(above_icon) != before.getpixel(above_icon):
        fail("dock icons still grow although magnification is off")

    clock_region = (1080, 2, 1270, MENU_BAR_HEIGHT - 2)
    pointer.move_to(switch_x, row(1))  # 24-hour clock
    pointer.click()
    wait_for(log_path, lambda text: "12-hour clock" in text, "the clock format to change")
    pointer.move_to(1000, 120)
    time.sleep(0.6)
    twelve = qmp.screenshot(prefix, "9-12-hour")
    if changed_pixels(twelve, before, clock_region) < 20:
        fail("the menu bar clock did not change format")

    pointer.move_to(settings["x"] + settings["width"] - 24 - 14, row(2))  # time zone, plus
    pointer.click()
    wait_for(log_path, lambda text: "UTC+330 min" in text, "the time zone to move half an hour")

    control_x = settings["x"] + settings["width"] - 24 - 230
    pointer.move_to(control_x + 230 // 2, row(3))  # wallpaper: the middle choice, Ocean
    pointer.click()
    wait_for(log_path, lambda text: "wallpaper ocean" in text, "the wallpaper to change")
    pointer.move_to(1000, 120)
    time.sleep(1.0)
    ocean = qmp.screenshot(prefix, "10-ocean")
    red, green, blue = ocean.getpixel((1150, 620))
    old_red, _, _ = before.getpixel((1150, 620))
    if not (red < old_red - 40 and blue > red):
        fail(f"the wallpaper did not turn from dusk to ocean: {(red, green, blue)} was red {old_red}")

    # ---- About -----------------------------------------------------------
    launch(qmp, pointer, log_path, icons["About"], "about: shown", "About", window_title="About Saddev")
    wait_for(log_path, lambda text: "desktop: active application is About" in text, "the menu bar to name About")

    # ---- A full desktop for the record -----------------------------------
    files = launch(qmp, pointer, log_path, icons["Files"], "files: showing /", "Files", window_title="/")
    launch(qmp, pointer, log_path, icons["Terminal"], "terminal: started", "Terminal")
    type_text(qmp, "ls /bin\n")
    time.sleep(0.8)
    qmp.screenshot(prefix, "11-final")

    rates = [float(rate) for rate in find_all(r"compositor: ([\d.]+) fps", read_log(log_path))]
    print(f"apps-check: frame rates {rates}")
    if rates and min(rates) < 50.0:
        fail(f"frame rate dropped to {min(rates)}")
    print("apps-check: ok")
    qmp.key("f12")
    time.sleep(1.0)


if __name__ == "__main__":
    main()
