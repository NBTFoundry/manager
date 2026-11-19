# SPDX-License-Identifier: MPL-2.0
"""The manager GUI on the panel: LayerManager surface 1290 in place of the
iDrive HMI (surface 256), an application's surface 1291 handed the panel by
the GUI, and the HMI put back only when the layer is ours."""
import json
import os
from pathlib import Path
import signal
import socket
import struct
import subprocess
import tempfile
import time
import unittest

from fake_hold import FakeHold


BUILD = Path(os.environ.get("NBT_BUILD_DIR", Path(__file__).resolve().parents[1] / "build"))
GUI = BUILD / "manager-gui-host"
FAKE = Path(__file__).resolve().parent / "fake_layer_manager.py"
WORKER = '''#!/usr/bin/python3
import os, sys
fd = int(sys.argv[2])
pending = b""
while True:
    data = os.read(fd, 1024)
    if not data:
        break
    pending += data
    while b"\\n" in pending:
        line, pending = pending.split(b"\\n", 1)
        reply = {b"HELLO 1": b"HELLO\\t1\\n", b"LIST": b"APPS\\t0\\n", b"PHONE STATUS": b"PHONE\\tnone\\n", b"SETUP": b"SETUP\\tcurrent\\n", b"STATUS": b"STATUS\\t0\\t-\\t-\\t-\\t0\\t-\\tIdle\\n"}.get(line, b"RUNNING\\t0\\n")
        os.write(fd, reply)
'''


# A worker with one application "test" that START hands the panel to.
APP_WORKER = '''#!/usr/bin/python3
import os, sys
fd = int(sys.argv[2])
pending = b""
started = False
stopped_reply = False
while True:
    data = os.read(fd, 1024)
    if not data:
        break
    pending += data
    while b"\\n" in pending:
        line, pending = pending.split(b"\\n", 1)
        if line == b"START test":
            started = True
            stopped_reply = False
            reply = b"OK screen\\n"
        elif line in (b"STOP test", b"STOP test fault"):
            started = False
            stopped_reply = True
            if line == b"STOP test fault":
                open(sys.argv[0] + ".fault", "w").close()
            else:
                open(sys.argv[0] + ".stopped", "w").close()
            reply = b"OK\\n"
        elif line == b"HELLO 1":
            reply = b"HELLO\\t1\\n"
        elif line == b"LIST":
            reply = b"APPS\\t1\\ttest\\n"
        elif line == b"PHONE STATUS":
            reply = b"PHONE\\tnone\\n"
        elif line == b"SETUP":
            reply = b"SETUP\\tcurrent\\n"
        elif line == b"STATUS":
            msg = "Application stopped" if stopped_reply else "Idle"
            stopped_reply = False
            reply = b"STATUS\\t0\\t-\\t-\\t-\\t0\\t-\\t" + msg.encode() + b"\\n"
        elif line.startswith(b"RUNNING"):  # the test creates <worker>.ended once the app ends
            running = started and not os.path.exists(sys.argv[0] + ".ended")
            reply = b"RUNNING\\t1\\ttest\\n" if running else b"RUNNING\\t0\\n"
        else:
            reply = b"ERR invalid-command\\n"
        os.write(fd, reply)
'''


def push_script(path, wait_ms, extra_lines=()):
    """The fake DSI's controller script: a knob push and release after wait_ms."""
    header = struct.pack("<IIIHHBBHB3xII", 0, 2, 0, 0x100, 0x5000, 0xE5, 3, 0xC46, 0xC, 0, 5)
    lines = ["WAIT %d" % wait_ms, (header + bytes([0, 0, 0, 1, 0])).hex(), (header + bytes(5)).hex()]
    lines.extend(extra_lines)
    path.write_text("\n".join(lines) + "\n")
    return path


def fake_layer_manager(directory, bound_after=3):
    """Environment for the fake LayerManagerControl, with iDrive on the panel."""
    state = Path(directory) / "lm-state.json"
    state.write_text(json.dumps({"order": [256], "visible": {"256": 1}, "bound_after": bound_after}))
    return {"NBT_TEST_LAYER_MANAGER": str(FAKE), "NBT_FAKE_LM_STATE": str(state),
            "NBT_FAKE_LM_LOG": str(Path(directory) / "lm.log")}


class LayerManagerTests(unittest.TestCase):
    def setUp(self):
        try:
            left, right = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        left.close()
        right.close()
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "root"
        (self.root / "manager").mkdir(parents=True, mode=0o700)
        self.root.chmod(0o700)
        self.debug = self.root / "manager" / "debug.log"
        self.debug.write_text("")
        self.debug.chmod(0o600)
        self.worker = Path(self.tmp.name) / "worker"
        self.worker.write_text(WORKER)
        self.worker.chmod(0o700)
        self.screen = Path(self.tmp.name) / "screen.log"
        self.hold = FakeHold(self.tmp.name)
        self.env = dict(os.environ, NBT_FAKE_SCREEN_LOG=str(self.screen),
                        **self.hold.env, **fake_layer_manager(self.tmp.name))
        self.state_path = Path(self.env["NBT_FAKE_LM_STATE"])
        self.calls = Path(self.env["NBT_FAKE_LM_LOG"])
        self.process = None

    def tearDown(self):
        self.hold.stop()
        if self.process and self.process.poll() is None:
            self.process.kill()
            self.process.wait(timeout=10)
        self.tmp.cleanup()

    def state(self):
        return json.loads(self.state_path.read_text())

    def put_state(self, state):  # atomic, as the fake LayerManager reads it any time
        temporary = self.state_path.with_suffix(".tmp")
        temporary.write_text(json.dumps(state))
        os.replace(temporary, self.state_path)

    def start(self, worker=None, env=None):
        self.process = subprocess.Popen([GUI, "--test-root", self.root, "--test-worker", worker or self.worker],
                                        env=env or self.env)

    def wait_for(self, predicate, timeout=8):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.05)
        self.fail("timed out; LayerManager calls: %s" % (self.calls.read_text() if self.calls.exists() else ""))

    def shown(self):
        state = self.state()
        return state["order"] == [1290] and state["visible"] == {"256": 0, "1290": 1}

    def back(self):  # the manager on the panel again after an application
        state = self.state()
        return state["order"] == [1290] and state["visible"].get("1290") == 1

    def calls_made(self):
        return [line.split(" ", 1)[1] for line in self.calls.read_text().splitlines()]

    def test_gui_replaces_and_restores_idrive(self):
        self.start()
        # Posted and panel-sized, but off the panel until iDrive is held.
        self.wait_for(lambda: self.screen.exists() and "post\n" in self.screen.read_text())
        time.sleep(1)
        self.assertEqual(self.calls_made(), ["get screen 0"])
        screen = self.screen.read_text()
        self.assertIn("id 1290\n", screen)
        self.assertIn("size 1440x540\n", screen)
        self.assertNotIn("show\n", screen)  # LayerManager alone decides what it shows
        self.hold.send("show")
        self.wait_for(self.shown)
        calls = self.calls_made()
        # The surface is shown only once LayerManager has made it, and only in
        # place of iDrive.
        first_set = next(i for i, call in enumerate(calls) if call.startswith("set"))
        self.assertEqual(calls[first_set - 3:first_set],
                         ["get surface 1290", "get layer 0x80000000", "get screen 0"])
        self.assertEqual(calls.count("get surface 1290"), 3)
        self.assertEqual(calls[first_set], "set layer 0x80000000 render order 0x50a")
        # Source and destination are the panel, as NBT-AAD sets its surfaces.
        self.assertIn("set surface 1290 source region 0 0 1440 540", calls)
        self.assertIn("set surface 1290 destination region 0 0 1440 540", calls)
        self.process.send_signal(signal.SIGTERM)
        self.assertEqual(self.process.wait(timeout=10), 0)
        state = self.state()
        self.assertEqual(state["order"], [256])
        self.assertEqual(state["visible"], {"256": 1, "1290": 0})
        text = self.debug.read_text()
        self.assertIn("surface 1290: on the panel in place of iDrive", text)
        self.assertIn("surface 1290: iDrive restored", text)

    def test_gui_leaves_android_auto_alone(self):
        self.hold.start()
        self.start()
        self.wait_for(lambda: "on the panel in place of iDrive" in self.debug.read_text())
        # Android Auto took the layer (NBT-AAD's own surface 1282) without
        # iDrive's focus moving: the hidden manager must not keep the knob.
        state = self.state()
        state["order"] = [1282]
        self.put_state(state)
        self.assertEqual(self.process.wait(timeout=5), 0)
        self.assertEqual(self.state()["order"], [1282])
        self.assertNotIn("render order 0x100", self.calls.read_text())
        text = self.debug.read_text()
        self.assertIn("surface 1290: no longer on the panel", text)
        self.assertIn("another surface took the panel", text)
        self.assertIn("surface 1290: iDrive already had the panel", text)

    def test_gui_ends_without_a_restart_when_layer_manager_is_silent(self):
        # dev79, 33 s after a boot: no answer, so no panel size; the launcher
        # restarts a GUI that fails, holding iDrive again each time.
        started = time.monotonic()
        self.start(env=dict(self.env, NBT_FAKE_LM_HANG_S="10"))
        self.assertEqual(self.process.wait(timeout=10), 0)
        self.assertLess(time.monotonic() - started, 6)
        text = self.debug.read_text()
        self.assertIn("screen window: panel size unknown", text)
        self.assertIn("exit (LayerManager did not answer), status 0", text)

    def test_gui_keeps_its_heartbeat_while_layer_manager_is_slow(self):
        # The plugin takes a GUI silent for 1.5 s as gone, the launcher after 3 s.
        self.hold.start()
        self.start(env=dict(self.env, NBT_FAKE_LM_HANG_S="2.5", NBT_FAKE_LM_HANG_ON="get surface"))
        liveness = self.hold.liveness
        self.wait_for(liveness.exists)
        oldest = 0
        deadline = time.monotonic() + 15
        while not self.shown() and time.monotonic() < deadline:
            try:
                beat = int(liveness.read_text().split()[1])
            except (OSError, IndexError, ValueError):
                beat = None
            if beat is not None:
                oldest = max(oldest, time.monotonic_ns() // 1000000 - beat)
            time.sleep(0.05)
        self.assertTrue(self.shown())
        self.assertGreaterEqual(self.calls_made().count("get surface 1290"), 3)
        self.assertLess(oldest, 1200)

    def app_env(self, script_ms, extra_lines=()):
        script = push_script(Path(self.tmp.name) / "controller", script_ms, extra_lines)
        worker = Path(self.tmp.name) / "app-worker"
        worker.write_text(APP_WORKER)
        worker.chmod(0o700)
        env = dict(self.env, NBT_TEST_INPUT_LIBRARY=str(BUILD / "libfake_dsi.so"),
                   NBT_FAKE_DSI_SCRIPT=str(script), NBT_FAKE_DSI_LOG=str(Path(self.tmp.name) / "dsi.log"))
        return worker, env

    def test_gui_hands_the_panel_to_the_application(self):
        state = self.state()
        state["bound_after"] = 1
        self.put_state(state)
        self.hold.start()
        worker, env = self.app_env(2500)
        self.start(worker, env)
        # The push starts "test"; the worker's "OK screen" hands it the panel.
        self.wait_for(lambda: "surface 1291: on the panel" in self.debug.read_text(), timeout=10)
        self.assertEqual(self.state()["order"], [1291])
        self.assertEqual(self.state()["visible"], {"256": 0, "1290": 0, "1291": 1})
        calls = self.calls_made()
        self.assertIn("set surface 1291 source region 0 0 1440 540", calls)
        # The manager stays until the application's surface is up, then the
        # two swap: the held (frozen) iDrive screen never shows in between.
        orders = [call for call in calls if "render order" in call]
        self.assertEqual(orders, ["set layer 0x80000000 render order 0x50a",
                                  "set layer 0x80000000 render order 0x50b"])
        swap = calls.index("set layer 0x80000000 render order 0x50b")
        self.assertIn("set surface 1290 visibility 0", calls[swap:])
        self.assertNotIn("set surface 256 visibility 1", calls)
        text = self.debug.read_text()
        self.assertIn("handing the panel to test", text)
        self.assertIn("surface 1291: on the panel in place of surface 1290", text)
        self.assertNotIn("iDrive restored", text)
        self.assertIsNone(self.process.poll())
        # The application ended: its window, and with it its surface, are gone.
        # The manager comes back, iDrive still held under it.
        state = self.state()
        state["order"] = []
        self.put_state(state)
        self.wait_for(self.back)
        self.assertIsNone(self.process.poll())
        text = self.debug.read_text()
        self.assertIn("surface 1291: no longer on the panel", text)
        self.assertIn("test left the panel; the manager is shown again", text)
        self.process.send_signal(signal.SIGTERM)
        self.assertEqual(self.process.wait(timeout=10), 0)
        self.assertEqual(self.state()["order"], [256])

    def test_gui_comes_back_when_the_application_ends_leaving_its_surface(self):
        # The swap back goes straight from the application to the manager too.
        # dev35 in the car: BACK ended the test app, its surface stayed on the
        # layer and the panel stayed black until MENU.
        state = self.state()
        state["bound_after"] = 1
        self.put_state(state)
        self.hold.start()
        worker, env = self.app_env(2500)
        self.start(worker, env)
        self.wait_for(lambda: "surface 1291: on the panel" in self.debug.read_text(), timeout=10)
        time.sleep(1.5)  # RUNNING has seen it running
        Path(str(worker) + ".ended").touch()
        self.wait_for(self.back, timeout=5)
        self.assertIsNone(self.process.poll())
        # The GUI logs the swap once show() has returned, just after the order changed.
        self.wait_for(lambda: "surface 1290: on the panel in place of surface 1291" in self.debug.read_text())
        text = self.debug.read_text()
        self.assertIn("test ended; the manager is shown again", text)
        self.assertNotIn("iDrive restored", text)
        self.assertNotIn("set surface 256 visibility 1", self.calls_made())
        self.process.send_signal(signal.SIGTERM)
        self.assertEqual(self.process.wait(timeout=10), 0)
        self.assertEqual(self.state()["order"], [256])

    def test_gui_comes_back_when_the_application_window_is_gone(self):
        # dev71 in the car: Doom crashed in its first frame. Its window closed,
        # its id stayed in the render order, nothing told the GUI it ended,
        # and the panel stayed black.
        state = self.state()
        state["bound_after"] = 1
        self.put_state(state)
        self.hold.start()
        worker, env = self.app_env(2500)
        self.start(worker, env)
        self.wait_for(lambda: "surface 1291: on the panel" in self.debug.read_text(), timeout=10)
        state = self.state()
        state["windows"] = [1290]
        self.put_state(state)
        self.wait_for(self.back, timeout=6)
        self.assertIsNone(self.process.poll())
        text = self.debug.read_text()
        self.assertIn("surface 1291: its window is gone", text)
        self.assertIn("test left the panel; the manager is shown again", text)
        self.process.send_signal(signal.SIGTERM)
        self.assertEqual(self.process.wait(timeout=10), 0)
        self.assertEqual(self.state()["order"], [256])

    def test_gui_comes_back_when_the_application_never_appears(self):
        self.state_path.write_text(json.dumps({"order": [256], "visible": {"256": 1}, "bound_after": 1,
                                               "windows": [1290]}))
        self.hold.start()
        worker, env = self.app_env(2500)
        self.start(worker, env)
        self.wait_for(lambda: "handing the panel to test" in self.debug.read_text(), timeout=10)
        self.wait_for(lambda: "test did not appear" in self.debug.read_text(), timeout=15)
        self.wait_for(self.shown)
        self.wait_for(lambda: Path(str(worker) + ".fault").exists(), timeout=5)
        self.assertIsNone(self.process.poll())
        self.assertIn("surface 1291: LayerManager made none within 10 s", self.debug.read_text())
        self.process.send_signal(signal.SIGTERM)
        self.assertEqual(self.process.wait(timeout=10), 0)
        self.assertEqual(self.state()["order"], [256])

    def test_gui_forced_escape_on_held_back_stops_hung_app(self):
        state = self.state()
        state["bound_after"] = 1
        self.put_state(state)
        self.hold.start()
        key_header = struct.pack("<IIIHHBBHB3xII", 0, 2, 0, 0x100, 0x5000, 0xE5, 3, 0xC47, 0xC, 0, 7)
        hold_back = ["WAIT 1000", (key_header + bytes([0, 1, 0, 0, 0, 0, 0])).hex(), "WAIT 2800",
                     "WAIT 2800", (key_header + bytes(7)).hex()]
        worker, env = self.app_env(2000, hold_back)
        self.start(worker, env)
        self.wait_for(lambda: "surface 1291: on the panel" in self.debug.read_text(), timeout=10)
        self.wait_for(lambda: "forced escape from test; stopping" in self.debug.read_text(), timeout=12)
        self.wait_for(lambda: Path(str(worker) + ".fault").exists(), timeout=5)
        self.assertEqual(self.process.wait(timeout=10), 0)
        text = self.debug.read_text()
        self.assertIn("iDrive restored", text)
        self.assertEqual(self.state()["order"], [256])

    def test_gui_tilt_left_does_not_escape_after_5s(self):
        state = self.state()
        state["bound_after"] = 1
        self.put_state(state)
        self.hold.start()
        rot_header = struct.pack("<IIIHHBBHB3xII", 0, 2, 0, 0x100, 0x5000, 0xE5, 3, 0xC46, 0xC, 0, 5)
        tilt_left = ["WAIT 800", (rot_header + bytes([0, 0, 0, 0, 10])).hex(), "WAIT 2800",
                     "WAIT 2800", (rot_header + bytes(5)).hex()]
        worker, env = self.app_env(2000, tilt_left)
        self.start(worker, env)
        self.wait_for(lambda: "surface 1291: on the panel" in self.debug.read_text(), timeout=10)
        time.sleep(6.2)
        self.assertIsNone(self.process.poll())
        self.assertNotIn("forced escape", self.debug.read_text())
        self.assertFalse(Path(str(worker) + ".fault").exists())
        self.process.send_signal(signal.SIGTERM)
        self.assertEqual(self.process.wait(timeout=10), 0)

    def test_gui_preview_keeps_capture_off(self):
        ctrl_path = Path(self.tmp.name) / "root" / "capture-ctrl"
        self.hold.start()
        # No input library -> preview mode: capture stays off even when panel is confirmed.
        self.start()
        self.wait_for(lambda: self.state()["order"] == [1290], timeout=10)
        self.assertTrue(ctrl_path.exists())
        self.assertIn(" st=0 ", ctrl_path.read_text())
        self.process.send_signal(signal.SIGTERM)
        self.assertEqual(self.process.wait(timeout=10), 0)

    def test_gui_capture_control_requires_confirmed_panel_and_preserves_generation(self):
        ctrl_path = Path(self.tmp.name) / "root" / "capture-ctrl"
        state = self.state()
        state["bound_after"] = 1
        self.put_state(state)
        self.hold.start()
        worker, env = self.app_env(1500, ["WAIT 6000"])
        self.start(worker, env)
        self.wait_for(lambda: self.state()["order"] == [1290], timeout=10)
        self.wait_for(lambda: ctrl_path.exists() and " st=1 " in ctrl_path.read_text(), timeout=5)
        gen_mgr = [p for p in ctrl_path.read_text().split() if p.startswith("gen=")][0]
        self.wait_for(lambda: self.state()["order"] == [1291], timeout=10)
        self.wait_for(lambda: ctrl_path.exists() and " st=1 " in ctrl_path.read_text(), timeout=5)
        gen_app = [p for p in ctrl_path.read_text().split() if p.startswith("gen=")][0]
        self.assertEqual(gen_mgr, gen_app)
        self.process.send_signal(signal.SIGTERM)
        self.assertEqual(self.process.wait(timeout=10), 0)

    def test_gui_never_replaces_android_auto(self):
        # Android Auto has the panel when the manager is opened (a favourite key):
        # the manager closes rather than take keys while nobody sees it.
        self.state_path.write_text(json.dumps({"order": [1282], "visible": {"256": 0, "1282": 1},
                                               "bound_after": 1}))
        self.hold.start()
        self.start()
        self.assertEqual(self.process.wait(timeout=8), 0)
        text = self.debug.read_text()
        self.assertIn("another surface has the HMI layer", text)
        self.assertIn("the manager could not take the panel", text)
        self.assertEqual([call for call in self.calls_made() if call.startswith("set")], [])
        self.assertEqual(self.state()["order"], [1282])

    def test_no_surface_leaves_idrive_untouched(self):
        self.put_state({"order": [256], "visible": {"256": 1}, "bound_after": 10 ** 6})
        self.hold.start()
        self.start()
        self.wait_for(lambda: self.calls.exists() and self.calls.read_text().count("get surface") >= 4)
        self.assertIsNone(self.process.poll())
        self.process.send_signal(signal.SIGTERM)
        self.assertEqual(self.process.wait(timeout=10), 0)
        self.assertNotIn(" set ", self.calls.read_text())
        self.assertEqual(self.state()["order"], [256])

    def test_volume_osd_overlays_manager_and_app_and_auto_clears(self):
        state = self.state()
        state["bound_after"] = 1
        state["windows"] = [1290, 1291, 1292]
        self.put_state(state)
        self.hold.start()
        vol_header = struct.pack("<IIIHHBBHB3xII", 0, 2, 0, 0x100, 0x5000, 0x20, 0, 0xC08, 0xC, 0, 5)
        vol_lines = [
            "WAIT 3000",
            (vol_header + bytes([0x01, 0x00, 40, 0x00, 100])).hex(),
            "WAIT 200",
            (vol_header + bytes([0x01, 0x00, 45, 0x00, 100])).hex(),
        ]
        worker, env = self.app_env(1500, vol_lines)
        vol_log = Path(self.tmp.name) / "volume.log"
        env["NBT_FAKE_VOLUME_SCREEN_LOG"] = str(vol_log)
        self.start(worker, env)
        self.wait_for(lambda: self.state()["order"] == [1292, 1290], timeout=10)
        self.wait_for(lambda: self.state()["order"] == [1292, 1291], timeout=10)
        self.assertEqual(self.state()["visible"].get("1292"), 1)
        self.wait_for(lambda: vol_log.exists() and "1292 drawn\n" in vol_log.read_text(), timeout=10)
        self.wait_for(lambda: vol_log.read_text().splitlines()[-1] == "1292 clear", timeout=5)
        entries = vol_log.read_text().splitlines()
        self.assertEqual(entries[0], "1292 clear")
        self.assertGreaterEqual(entries.count("1292 drawn"), 2)
        self.process.send_signal(signal.SIGTERM)
        self.assertEqual(self.process.wait(timeout=10), 0)
        end_state = self.state()
        self.assertEqual(end_state["order"], [256])
        self.assertEqual(end_state["visible"].get("256"), 1)
        self.assertEqual(end_state["visible"].get("1292"), 0)

    def test_layer_manager_control_omits_ld_preload(self):
        self.hold.start()
        worker, env = self.app_env(500)
        env_log = Path(self.tmp.name) / "lm_env.log"
        env["LD_PRELOAD"] = ""
        env["NBT_FAKE_LM_ENV_LOG"] = str(env_log)
        self.start(worker, env)
        self.wait_for(lambda: self.state()["order"] == [1290], timeout=10)
        self.process.send_signal(signal.SIGTERM)
        self.assertEqual(self.process.wait(timeout=10), 0)
        lines = env_log.read_text().splitlines()
        self.assertTrue(lines)
        self.assertTrue(all(line == "LD_PRELOAD=absent" for line in lines))


if __name__ == "__main__":
    unittest.main()
