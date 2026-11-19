# SPDX-License-Identifier: MPL-2.0
"""The plugin's side of the focus hold in host tests: once a manager GUI
reports itself alive, iDrive is "held" on the blank state and the GUI is told
to show (include/nbt/gui/hmi_hold.hpp). The GUI stays hidden until then."""
import threading
import time
from pathlib import Path


class FakeHold:
    def __init__(self, directory):
        self.signal = Path(directory) / "hmi-signal"
        self.liveness = Path(directory) / "gui-live"
        self.env = {"NBT_TEST_HMI_SIGNAL": str(self.signal), "NBT_TEST_GUI_LIVENESS": str(self.liveness)}
        self._stop = threading.Event()
        self._thread = None

    def send(self, word):
        self.signal.write_text("%d %s\n" % (time.monotonic_ns(), word))

    def start(self):
        """Show every new GUI as soon as it runs, as the plugin does once iDrive is held."""
        def run():
            shown = None
            while not self._stop.wait(0.02):
                try:
                    pid = self.liveness.read_text().split(" ", 1)[0]
                except OSError:
                    continue
                if pid and pid != shown:
                    shown = pid
                    self.send("show")
        self._thread = threading.Thread(target=run, daemon=True)
        self._thread.start()
        return self

    def stop(self):
        self._stop.set()
        if self._thread:
            self._thread.join(timeout=2)
