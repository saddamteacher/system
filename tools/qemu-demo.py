#!/usr/bin/env python3
"""A guided tour of the desktop, recorded.

usage: qemu-demo.py <qmp-socket> <serial-log> <output-directory>

Works with user/tests/desktop-session.c in "apps" mode (the real desktop
with its four applications). The script uses the desktop the way a person
would while a recorder photographs the screen a few times per second. It
leaves numbered screenshots of the key moments and an animation of the
whole tour (saddev-demo.gif) in the output directory, then presses F12 to
end the session.
"""
import os
import re
import sys
import threading
import time

from PIL import Image

from qmplib import (TITLE_BAR_HEIGHT, Pointer, Qmp, launch, read_log, type_text, wait_for)

FRAME_INTERVAL_S = 0.2
GIF_SIZE = (640, 400)


class Recorder(threading.Thread):
    """Photographs the screen at a steady pace until stopped."""

    def __init__(self, qmp, directory):
        super().__init__(daemon=True)
        self.qmp, self.path = qmp, os.path.join(directory, "frame.ppm")
        self.frames, self.running = [], True

    def run(self):
        while self.running:
            started = time.monotonic()
            self.qmp.command("screendump", filename=self.path)
            time.sleep(0.1)  # the file is written asynchronously
            try:
                self.frames.append(Image.open(self.path).convert("RGB").resize(GIF_SIZE, Image.LANCZOS))
            except (OSError, SyntaxError):
                pass  # caught mid-write: this frame is skipped
            time.sleep(max(0.0, FRAME_INTERVAL_S - (time.monotonic() - started)))

    def finish(self, output):
        self.running = False
        self.join()
        os.remove(self.path)
        # One palette for all frames keeps colours steady and the file small.
        palette = self.frames[len(self.frames) // 2].quantize(colors=128, dither=Image.Dither.NONE)
        frames = [frame.quantize(palette=palette, dither=Image.Dither.NONE) for frame in self.frames]
        frames[0].save(output, save_all=True, append_images=frames[1:], duration=int(FRAME_INTERVAL_S * 1000),
                       loop=0, optimize=True)
        return len(frames)


def main():
    socket_path, log_path, directory = sys.argv[1:]
    os.makedirs(directory, exist_ok=True)
    prefix = os.path.join(directory, "saddev")
    qmp = Qmp(socket_path)
    log = wait_for(log_path, lambda text: text if "desktop: ready" in text else None, "the desktop to be ready")
    time.sleep(1.0)

    shelf = re.search(r"dock shelf at (\d+) (\d+) size (\d+)x(\d+), (\d+) items, first icon at (\d+) (\d+)", log)
    icon_x, icon_y = int(shelf.group(6)), int(shelf.group(7))
    icons = {name: (icon_x + 24 + i * 62, icon_y + 24)
             for i, name in enumerate(["Terminal", "Files", "Settings", "About"])}
    pointer = Pointer(qmp, 640, 400)
    pointer.move_to(1000, 120)
    time.sleep(0.5)
    qmp.screenshot(prefix, "1-desktop")
    recorder = Recorder(qmp, directory)
    recorder.start()

    # The dock grows under the pointer.
    pointer.move_to(icons["Terminal"][0] - 120, icon_y + 24)
    pointer.move_to(icons["About"][0] + 120, icon_y + 24, step=6)
    pointer.move_to(icons["Settings"][0], icon_y + 24, step=6)
    time.sleep(0.6)
    qmp.screenshot(prefix, "2-dock-magnified")

    terminal = launch(qmp, pointer, log_path, icons["Terminal"], "terminal: started", "Terminal")
    type_text(qmp, "cat /etc/motd\n")
    type_text(qmp, "ls /\n")
    type_text(qmp, "mem\n")
    time.sleep(0.8)
    qmp.screenshot(prefix, "3-terminal")

    files = launch(qmp, pointer, log_path, icons["Files"], "files: showing /", "Files", window_title="/")
    pointer.move_to(files["x"] + 120, files["y"] + 40 + 13 + 26)  # the second row: etc
    pointer.click()
    wait_for(log_path, lambda text: "files: entered /etc" in text, "Files to enter /etc")

    # Drag it aside by its title bar.
    grip = (files["x"] + files["width"] // 2, files["y"] - TITLE_BAR_HEIGHT // 2)
    pointer.move_to(*grip)
    pointer.press()
    pointer.move_to(grip[0] + 260, grip[1] + 90, step=12)
    pointer.release()
    wait_for(log_path, lambda text: f"window {files['id']} moved to" in text, "the Files window to be moved")
    qmp.screenshot(prefix, "4-files-moved")

    # Minimize the terminal into the dock and bring it back.
    pointer.move_to(terminal["x"] + 38, terminal["y"] - TITLE_BAR_HEIGHT // 2)  # the yellow button
    pointer.click()
    pointer.move_to(1000, 120)
    wait_for(log_path, lambda text: f"animation minimize window {terminal['id']}" in text, "the terminal to minimize")
    time.sleep(0.5)
    pointer.move_to(*icons["Terminal"])
    time.sleep(0.4)
    pointer.click()
    pointer.move_to(1000, 120)
    wait_for(log_path, lambda text: f"animation restore window {terminal['id']}" in text, "the terminal to return")

    settings = launch(qmp, pointer, log_path, icons["Settings"], "settings: shown, connected to the desktop",
                      "Settings")
    control_x = settings["x"] + settings["width"] - 24 - 230
    pointer.move_to(control_x + 230 // 2, settings["y"] + 20 + 3 * 48 + 24)  # wallpaper: Ocean
    pointer.click()
    wait_for(log_path, lambda text: "wallpaper ocean" in text, "the wallpaper to change")
    pointer.move_to(1000, 120)
    time.sleep(1.0)
    qmp.screenshot(prefix, "5-settings-ocean")

    launch(qmp, pointer, log_path, icons["About"], "about: shown", "About", window_title="About Saddev")
    time.sleep(1.0)
    qmp.screenshot(prefix, "6-about")

    frame_count = recorder.finish(prefix + "-demo.gif")
    rates = [float(rate) for rate in re.findall(r"compositor: ([\d.]+) fps", read_log(log_path))]
    print(f"demo: {frame_count} frames recorded; the window server reported "
          f"{min(rates):.0f}-{max(rates):.0f} frames per second" if rates else f"demo: {frame_count} frames recorded")
    for name in sorted(os.listdir(directory)):
        if name.endswith(".ppm"):
            os.remove(os.path.join(directory, name))
    qmp.key("f12")


if __name__ == "__main__":
    main()
