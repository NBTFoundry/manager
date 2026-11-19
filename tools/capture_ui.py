#!/usr/bin/env python3
# SPDX-License-Identifier: MPL-2.0
"""Captures the manager's GUI for the documentation: the X11 simulator
(`make simulator`) under Xvfb, driven with XTest key presses, saved as stills
(PNG) and short animations (GIF), framed like the head unit's 1440x540 panel.

    make simulator && python3 tools/capture_ui.py [--out docs/assets]
    make simulator && python3 tools/capture_ui.py --languages --out /tmp/languages

Needs Xvfb, python-xlib, ffmpeg and ImageMagick (`magick`); pngquant is used
when present. The simulator's worker is simulated: nothing is installed.
--languages captures the main screens in each language instead, to review
translations on the panel (not for the documentation).
"""

import argparse
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time

from Xlib import X, XK, display as xdisplay
from Xlib.ext import xtest

ROOT = Path(__file__).resolve().parents[1]
SIM = ROOT / "build" / "nbt-simulator"
WIDTH, HEIGHT = 1280, 480          # the GUI's 640x240 frame at scale 2, as on the car
PANEL_W, PANEL_H = 1440, 540       # the head unit's panel; the GUI is centred on black
DIGEST = "3f9a6c1e8b2d47f0a5c3e9d1b7f2a4c6e8d0b3f5a7c9e1d3b5f7a9c1e3d5b7f9"


class Session:
    """Xvfb, the simulator and an XTest connection."""

    def __init__(self, **environment):
        r, w = os.pipe()
        self.xvfb = subprocess.Popen(
            ["Xvfb", "-displayfd", str(w), "-screen", "0", f"{WIDTH}x{HEIGHT}x24", "-nolisten", "tcp"],
            pass_fds=(w,), stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
        os.close(w)
        number = b""
        while not number.endswith(b"\n"):
            chunk = os.read(r, 1)
            if not chunk:
                raise SystemExit("Xvfb did not start")
            number += chunk
        os.close(r)
        self.name = ":" + number.decode().strip()
        env = dict(os.environ, DISPLAY=self.name, NBT_SIM_VERSION="v1", NBT_SIM_DIGEST=DIGEST,
                   **environment)
        self.sim = subprocess.Popen([str(SIM), "2"], env=env, stdout=subprocess.DEVNULL,
                                    stderr=subprocess.DEVNULL)
        self.d = xdisplay.Display(self.name)
        time.sleep(1.5)

    def key(self, name, hold=0.08, after=0.35):
        code = self.d.keysym_to_keycode(XK.string_to_keysym(name))
        xtest.fake_input(self.d, X.KeyPress, code)
        self.d.sync()
        time.sleep(hold)
        xtest.fake_input(self.d, X.KeyRelease, code)
        self.d.sync()
        time.sleep(after)

    def still(self, path):
        raw = path.with_suffix(".xwd")
        with open(raw, "wb") as out:
            subprocess.run(["xwd", "-root", "-silent", "-display", self.name], stdout=out, check=True)
        subprocess.run(["magick", f"xwd:{raw}", str(path)], check=True)
        raw.unlink()

    def record(self, path, seconds):
        """Starts recording the screen for `seconds`; returns the ffmpeg process."""
        return subprocess.Popen(
            ["ffmpeg", "-loglevel", "error", "-y", "-f", "x11grab", "-draw_mouse", "0", "-framerate", "24",
             "-video_size", f"{WIDTH}x{HEIGHT}", "-i", self.name, "-t", str(seconds),
             "-c:v", "libx264rgb", "-crf", "0", "-preset", "ultrafast", str(path)])

    def close(self):
        self.sim.terminate()
        self.xvfb.terminate()
        self.sim.wait(timeout=5)
        self.xvfb.wait(timeout=5)


def frame_still(src, dst):
    """The GUI centred on the black panel, in a rounded dark bezel."""
    bezel = 22
    w, h = PANEL_W + 2 * bezel, PANEL_H + 2 * bezel
    subprocess.run([
        "magick", str(src), "-background", "black", "-gravity", "center",
        "-extent", f"{PANEL_W}x{PANEL_H}",
        "-bordercolor", "#171b22", "-border", str(bezel),
        "(", "-size", f"{w}x{h}", "xc:none", "-fill", "white",
        "-draw", f"roundrectangle 0,0 {w - 1},{h - 1} 28,28", ")",
        "-compose", "DstIn", "-composite", "-strip", "-define", "png:exclude-chunks=date,time",
        str(dst)], check=True)
    if shutil.which("pngquant"):
        subprocess.run(["pngquant", "--force", "--skip-if-larger", "--output", str(dst), "--", str(dst)])


def frame_gif(src, dst, width=960, fps=12):
    """The recording, padded to the panel and scaled for a README."""
    pad = f"pad={PANEL_W}:{PANEL_H}:(ow-iw)/2:(oh-ih)/2:black"
    chain = f"fps={fps},{pad},scale={width}:-1:flags=lanczos"
    subprocess.run([
        "ffmpeg", "-loglevel", "error", "-y", "-i", str(src), "-filter_complex",
        f"[0:v]{chain},split[a][b];[a]palettegen=max_colors=96:stats_mode=diff[p];"
        f"[b][p]paletteuse=dither=bayer:bayer_scale=4:diff_mode=rectangle",
        "-loop", "0", str(dst)], check=True)


def capture_languages(out):
    """Each language's menu, settings, Language list, review, pairing code and
    status, as `<code>-<screen>.png`; iDrive in German, so Automatic shows it."""
    for code in ("en", "fr", "de", "it", "es", "zh", "ja"):
        s = Session(NBT_LANGUAGE=code, NBT_SIM_CAR_LANGUAGE="1")
        try:
            s.still(out / f"{code}-menu.png")
            s.key("u", after=1.6)                                      # an update arrives
            s.still(out / f"{code}-review.png")
            s.key("Return", after=0.8)                                 # Reject
            s.key("Down"); s.key("Down"); s.key("Return", after=0.8)   # Settings
            s.still(out / f"{code}-settings.png")
            for _ in range(3):
                s.key("Down")
            s.key("Return", after=0.8)                                 # Language
            s.still(out / f"{code}-language.png")
            s.key("BackSpace", after=0.8)
            s.key("Down"); s.key("Return", after=1.5)                  # Pair a phone
            s.key("u", after=1.5)                                      # a phone asks to pair
            s.still(out / f"{code}-pair-code.png")
            s.key("Return", after=0.8)                                 # No
            s.key("Return", after=0.8)                                 # OK: the main menu
            s.key("Down"); s.key("Down"); s.key("Return", after=0.8)   # Settings
            for _ in range(6):
                s.key("Down")
            s.key("Return", after=0.8)                                 # Status
            s.still(out / f"{code}-status.png")
        finally:
            s.close()
        # media-player's files: its list, a USB stick's files, an import.
        s = Session(NBT_LANGUAGE=code, NBT_SIM_CAR_LANGUAGE="1")
        try:
            s.key("Return", hold=1.9, after=1.2)                       # media-player's settings
            s.key("Down"); s.key("Return", after=0.8)                  # Files
            s.still(out / f"{code}-files.png")
            s.key("Return", after=3.0)                                 # Import from a USB stick
            s.still(out / f"{code}-usb.png")
            s.key("Return", after=1.0)                                 # Import all
            s.still(out / f"{code}-importing.png")
        finally:
            s.close()


def main():
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    parser.add_argument("--out", type=Path, default=ROOT / "docs" / "assets")
    parser.add_argument("--languages", action="store_true", help="each language's screens, for review")
    args = parser.parse_args()
    if not SIM.is_file():
        sys.exit(f"{SIM} is missing: run make simulator")
    for tool in ("Xvfb", "xwd", "ffmpeg", "magick"):
        if not shutil.which(tool):
            sys.exit(f"{tool} is required")
    args.out.mkdir(parents=True, exist_ok=True)
    if args.languages:
        capture_languages(args.out)
        print(f"captures in {args.out}")
        return
    with tempfile.TemporaryDirectory(prefix="nbt-capture-") as temporary:
        tmp = Path(temporary)

        # Stills: the menu, the manager's settings, pairing (Wi-Fi code, then the
        # six digits). MENU would close the manager and end the simulator.
        s = Session()
        try:
            s.still(tmp / "menu.png")
            s.key("Down"); s.key("Down"); s.key("Return", after=0.8)   # Settings
            s.still(tmp / "settings.png")
            for _ in range(4):
                s.key("Down")
            s.key("Return", after=1.5)                                 # Pair a phone
            s.still(tmp / "pair-qr.png")
            s.key("u", after=1.5)                                      # a phone asks to pair
            s.still(tmp / "pair-code.png")
        finally:
            s.close()

        # Still: the test application on the panel.
        s = Session()
        try:
            s.key("Down"); s.key("Return", after=1.5)                  # start "test"
            s.still(tmp / "app.png")
        finally:
            s.close()

        # Animation: a phone sends an update; the driver reviews and installs it.
        s = Session()
        try:
            video = tmp / "install.mkv"
            rec = s.record(video, 11)
            time.sleep(1.0)
            s.key("u", after=1.6)                                      # upload arrives: review
            s.key("Down", after=0.9)                                   # Install
            s.key("Return", after=1.4)                                 # confirm
            s.key("Down", after=0.6)
            s.key("Return", after=0.4)                                 # Yes, install
            rec.wait()
        finally:
            s.close()
        frame_gif(video, args.out / "manager-install.gif")

        # Animation: hold the push on an application for its settings.
        s = Session()
        try:
            video = tmp / "hold.mkv"
            rec = s.record(video, 5)
            time.sleep(0.8)
            s.key("Down", after=0.6)
            s.key("Return", hold=1.9, after=1.2)
            rec.wait()
        finally:
            s.close()
        frame_gif(video, args.out / "manager-settings.gif")

        for name in ("menu", "settings", "pair-qr", "pair-code", "app"):
            frame_still(tmp / f"{name}.png", args.out / f"manager-{name}.png")
    print(f"captures in {args.out}")


if __name__ == "__main__":
    main()
