# SPDX-License-Identifier: MPL-2.0
import os
import fcntl
import re
from pathlib import Path
import select
import signal
import socket
import subprocess
import tempfile
import time
import unittest

from fake_hold import FakeHold
from test_layer_manager import fake_layer_manager


BUILD = Path(os.environ.get("NBT_BUILD_DIR", Path(__file__).resolve().parents[1] / "build"))
GUI = BUILD / "manager-gui-host"
LAUNCHER = BUILD / "launcher-host"


class ManagerGuiTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "root"
        (self.root / "manager").mkdir(parents=True, mode=0o700)
        self.root.chmod(0o700)
        self.log = Path(self.tmp.name) / "screen.log"
        self.worker = Path(self.tmp.name) / "worker"
        self.worker.write_text('''#!/usr/bin/python3
import os, sys
fd = int(sys.argv[2])
root = sys.argv[4]
request = b""
while not request.endswith(b"\\n"):
    data = os.read(fd, 1)
    if not data:
        sys.exit(1)
    request += data
if request != b"HELLO 1\\n":
    sys.exit(1)
os.write(fd, b"HELLO\\t1\\n")
open(root + "/started", "w").close()
pending = b""
while True:
    data = os.read(fd, 1024)
    if not data:
        break
    pending += data
    while b"\\n" in pending:
        line, pending = pending.split(b"\\n", 1)
        if line == b"LIST":
            os.write(fd, b"APPS\\t0\\n")
        elif line == b"RUNNING status":
            os.write(fd, b"RUNNING\\t0\\n")
        elif line == b"SERVICES":
            os.write(fd, b"SERVICES\\t0\\n")
        elif line == b"PHONE STATUS":
            os.write(fd, b"PHONE\\tnone\\n")
        elif line == b"SETUP":
            os.write(fd, b"SETUP\\tcurrent\\n")
        elif line == b"STATUS":
            os.write(fd, b"STATUS\\t0\\t-\\t-\\t-\\t0\\t-\\tIdle\\n")
        else:
            sys.exit(1)
open(root + "/stopped", "w").close()
''')
        self.worker.chmod(0o700)
        # The plugin's hold: each GUI is told to show once it runs.
        self.hold = FakeHold(self.tmp.name).start()
        self.env = dict(os.environ, NBT_FAKE_SCREEN_LOG=str(self.log), **self.hold.env,
                        **fake_layer_manager(self.tmp.name, bound_after=1))
        self.processes = []

    def tearDown(self):
        self.hold.stop()
        for process in self.processes:
            if process.poll() is None:
                process.terminate()
            process.wait(timeout=10)
        self.tmp.cleanup()

    def start(self, worker=None, ignore_sigchld=False):
        process = subprocess.Popen([GUI, "--test-root", self.root, "--test-worker", worker or self.worker], env=self.env,
                                   preexec_fn=(lambda: signal.signal(signal.SIGCHLD, signal.SIG_IGN)) if ignore_sigchld else None)
        self.processes.append(process)
        return process

    def wait_for(self, predicate):
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.02)
        self.fail("timed out waiting for manager state")

    def test_single_instance_worker_and_cleanup(self):
        try:
            left, right = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        left.close()
        right.close()
        first = self.start(ignore_sigchld=True)
        self.wait_for(lambda: (self.root / "started").exists() and
                      self.log.exists() and self.log.read_text().count("post\n") >= 2)
        second = self.start()
        self.assertEqual(second.wait(timeout=4), 0)
        self.assertEqual(self.log.read_text().count("id 1290\n"), 1)
        first.send_signal(signal.SIGTERM)
        self.assertEqual(first.wait(timeout=10), 0)
        self.assertTrue((self.root / "stopped").exists())
        self.assertEqual(self.log.read_text().splitlines()[-2:], ["destroy-window", "destroy-context"])
        third = self.start()
        self.wait_for(lambda: self.log.read_text().count("id 1290\n") == 2)
        third.terminate()
        self.assertEqual(third.wait(timeout=10), 0)

    def test_controller_menu_key_closes_gui(self):
        try:
            left, right = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        left.close()
        right.close()
        import struct
        script = Path(self.tmp.name) / "controller"
        header = struct.pack("<IIIHHBBHB3xII", 0, 2, 0, 0x100, 0x5000, 0xE5, 3, 0xC47, 0xC, 0, 7)
        # Pressing a key nothing uses (COM), then MENU.
        # Keys reach the GUI once it is shown (iDrive held); before, they are iDrive's.
        script.write_text("WAIT 500\n" + "\n".join((header + bytes(k)).hex() for k in
                                                   ([0, 0, 0, 1, 0, 0, 0], [0] * 7, [1, 0, 0, 0, 0, 0, 0])) + "\n")
        env = dict(self.env, NBT_TEST_INPUT_LIBRARY=str(BUILD / "libfake_dsi.so"),
                   NBT_FAKE_DSI_SCRIPT=str(script), NBT_FAKE_DSI_LOG=str(Path(self.tmp.name) / "dsi.log"))
        started = time.monotonic()
        process = subprocess.Popen([GUI, "--test-root", self.root, "--test-worker", self.worker], env=env)
        self.processes.append(process)
        self.assertEqual(process.wait(timeout=10), 0)
        self.assertLess(time.monotonic() - started, 8)
        self.assertTrue((self.root / "stopped").exists())
        self.assertEqual(self.log.read_text().splitlines()[-2:], ["destroy-window", "destroy-context"])

    def test_plugin_signal_steps_aside(self):
        try:
            left, right = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        left.close()
        right.close()
        self.hold.stop()  # this test is the plugin
        debug = self.root / "manager" / "debug.log"
        debug.write_text("")
        debug.chmod(0o600)
        signal_file = self.hold.signal
        # Written before the GUI started: stale, ignored.
        signal_file.write_text("5 aside\n")
        process = subprocess.Popen([GUI, "--test-root", self.root, "--test-worker", self.worker], env=self.env)
        self.processes.append(process)
        self.wait_for(lambda: (self.root / "started").exists() and self.hold.liveness.exists() and
                      self.log.exists() and "post\n" in self.log.read_text())
        time.sleep(0.5)
        self.assertIsNone(process.poll())
        self.assertIn("shown once iDrive holds its focus", debug.read_text())
        self.assertNotIn("iDrive signal", debug.read_text())
        # iDrive is held: the window is shown.
        signal_file.write_text("6 show\n")
        self.wait_for(lambda: "iDrive signal show" in debug.read_text())
        # Malformed or older signals change nothing.
        signal_file.write_text("x aside\n")
        time.sleep(0.3)
        signal_file.write_text("4 aside\n")
        time.sleep(0.3)
        self.assertIsNone(process.poll())
        # iDrive left the manager's focus state: with no installation running,
        # the GUI closes and its window is cleaned up.
        signal_file.write_text("9 aside\n")
        self.assertEqual(process.wait(timeout=5), 0)
        self.assertTrue((self.root / "stopped").exists())
        self.assertEqual(self.log.read_text().splitlines()[-2:], ["destroy-window", "destroy-context"])

    def run_with_controller(self, config, presses):
        import struct
        script = Path(self.tmp.name) / "controller"
        header = struct.pack("<IIIHHBBHB3xII", 0, 2, 0, 0x100, 0x5000, 0xE5, 3, 0xC47, 0xC, 0, 7)
        script.write_text("WAIT 500\n" + "\n".join((header + bytes(k)).hex() for k in presses) + "\n")
        conf = self.root / "manager" / "controller.conf"
        if config is not None:
            conf.write_text(config)
            conf.chmod(0o600)
        dsi_log = Path(self.tmp.name) / "dsi.log"
        env = dict(self.env, NBT_TEST_INPUT_LIBRARY=str(BUILD / "libfake_dsi.so"),
                   NBT_FAKE_DSI_SCRIPT=str(script), NBT_FAKE_DSI_LOG=str(dsi_log))
        process = subprocess.Popen([GUI, "--test-root", self.root, "--test-worker", self.worker], env=env)
        self.processes.append(process)
        return process, dsi_log

    def test_redraws_follow_the_knob_at_up_to_30_frames_per_second(self):
        # 30 fps: the loop comes round at least every 33 ms and draws a changed
        # view at most once per 33 ms. A burst of knob detents is drawn as it
        # comes, capped, and its end is on screen promptly.
        import struct
        try:
            left, right = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        left.close()
        right.close()
        debug = self.root / "manager" / "debug.log"
        debug.write_text("")
        debug.chmod(0o600)

        def rotary(position):
            header = struct.pack("<IIIHHBBHB3xII", 0, 2, 0, 0x100, 0x5000, 0xE5, 3, 0xC46, 0xC, 0, 8)
            return (header + bytes([1, position >> 8, position & 0xFF, 0, 0, 0, 0, 0xFF])).hex()

        script = Path(self.tmp.name) / "controller"
        # Down and up again, 15 detents each way, with no pause between them.
        positions = [100] + list(range(101, 116)) + list(range(114, 99, -1))
        script.write_text("WAIT 800\n" + "\n".join(rotary(p) for p in positions) + "\n")
        posts = Path(self.tmp.name) / "posts.log"
        env = dict(self.env, NBT_TEST_INPUT_LIBRARY=str(BUILD / "libfake_dsi.so"),
                   NBT_FAKE_DSI_SCRIPT=str(script), NBT_FAKE_DSI_LOG=str(Path(self.tmp.name) / "dsi.log"),
                   NBT_FAKE_SCREEN_TIME_LOG=str(posts))
        process = subprocess.Popen([GUI, "--test-root", self.root, "--test-worker", self.worker], env=env)
        self.processes.append(process)
        self.wait_for(lambda: debug.read_text().count("input ") >= 30)
        time.sleep(0.3)
        process.send_signal(signal.SIGTERM)
        self.assertEqual(process.wait(timeout=10), 0)
        inputs = [float(line.split()[0]) for line in debug.read_text().splitlines()
                  if " input " in line and "ignored" not in line]
        self.assertGreaterEqual(len(inputs), 30)
        frames = [float(t) for t in posts.read_text().split() if float(t) >= inputs[0]]
        self.assertGreaterEqual(len(frames), 2, frames)
        gaps = [b - a for a, b in zip(frames, frames[1:])]
        self.assertTrue(all(gap >= 0.030 for gap in gaps), gaps)  # at most 30 redraws a second
        self.assertLessEqual(frames[0] - inputs[0], 0.050)       # the first detent within a frame
        self.assertLessEqual(frames[-1] - inputs[-1], 0.050)     # and the last one
        self.assertLessEqual(len(frames), (frames[-1] - inputs[0]) / 0.033 + 2)

    def registered_instances(self, dsi_log):
        import struct
        for line in dsi_log.read_text().splitlines():
            if line.startswith("REQUEST 1 "):
                payload = bytes.fromhex(line.split()[2])
                count = struct.unpack_from("<I", payload, 4)[0]
                return [key & 0xFF for key in struct.unpack_from("<%dH" % count, payload, 8)]
        return None

    def test_controller_config_sets_instances_and_key_slots(self):
        try:
            left, right = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        left.close()
        right.close()
        # MENU moved to slot 2 (OPTION is then slot 0): slot 1 does nothing.
        process, dsi_log = self.run_with_controller(
            "NBTCTL1\n# bench car\nknob_menu_slot=2\nknob_back_slot=5\ninstances=03,01,12\n",
            ([0, 1, 0, 0, 0, 0, 0], [0] * 7))
        time.sleep(2)
        self.assertIsNone(process.poll())
        self.assertEqual(self.registered_instances(dsi_log), [0x03, 0x01, 0x12])
        process.terminate()
        self.assertEqual(process.wait(timeout=10), 0)
        process, dsi_log = self.run_with_controller(None, ([0, 0, 1, 0, 0, 0, 0],))
        self.assertEqual(process.wait(timeout=10), 0)

    def test_invalid_controller_config_uses_defaults(self):
        try:
            left, right = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        left.close()
        right.close()
        process, dsi_log = self.run_with_controller("NBTCTL1\nknob_menu_slot=9\n", ([1, 0, 0, 0, 0, 0, 0],))
        self.assertEqual(process.wait(timeout=10), 0)  # slot 0 is MENU again
        self.assertEqual(self.registered_instances(dsi_log), [0x01, 0x02, 0x03, 0x05, 0x10, 0x12, 0x14])

    def test_language_from_setting_and_idrive(self):
        # Automatic shows iDrive's language, from the plugin's record; a
        # chosen language wins; a language the manager lacks shows English.
        try:
            left, right = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        left.close()
        right.close()
        debug = self.root / "manager" / "debug.log"
        debug.write_text("")
        debug.chmod(0o600)
        record = Path(self.tmp.name) / "car-language"
        self.env["NBT_TEST_CAR_LANGUAGE"] = str(record)

        def shown(value, expected):
            descriptor = os.open(record, os.O_WRONLY | os.O_CREAT | os.O_TRUNC, 0o600)
            os.write(descriptor, b"NBTLANG1\nvalue=%d\n" % value)
            os.close(descriptor)
            process = self.start()
            self.wait_for(lambda: expected in debug.read_text())
            process.terminate()
            self.assertEqual(process.wait(timeout=10), 0)

        shown(13, "language auto, iDrive 13, shown ja")
        shown(7, "language auto, iDrive 7, shown en")  # Dutch
        language = self.root / "manager" / "language"
        language.write_text("fr\n")
        language.chmod(0o600)
        shown(13, "language fr, iDrive 13, shown fr")
        language.write_text("nl\n")
        shown(1, "language auto, iDrive 1, shown de")

    def requests(self, dsi_log, number):
        return [line.split()[2] for line in dsi_log.read_text().splitlines()
                if line.startswith("REQUEST %x " % number)]

    def test_controller_release_on_exit(self):
        try:
            left, right = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        left.close()
        right.close()
        debug = self.root / "manager" / "debug.log"
        debug.write_text("")
        debug.chmod(0o600)
        # Off by default: closing with MENU sends no request 0x5.
        process, dsi_log = self.run_with_controller(None, ([1, 0, 0, 0, 0, 0, 0],))
        self.assertEqual(process.wait(timeout=10), 0)
        self.assertEqual(len(self.requests(dsi_log, 1)), 1)
        self.assertEqual(self.requests(dsi_log, 5), [])
        self.assertNotIn("controller release", debug.read_text())
        dsi_log.unlink()
        debug.write_text("")
        # With release=1 the same instances are unregistered once, after which
        # no input is delivered.
        process, dsi_log = self.run_with_controller("NBTCTL1\nrelease=1\ninstances=03,01,12\n",
                                                    ([1, 0, 0, 0, 0, 0, 0],))
        self.assertEqual(process.wait(timeout=10), 0)
        registered = self.requests(dsi_log, 1)
        self.assertEqual(len(registered), 1)
        self.assertEqual(self.requests(dsi_log, 5), registered)
        self.assertEqual(self.registered_instances(dsi_log), [0x03, 0x01, 0x12])
        text = debug.read_text()
        self.assertIn("MOST controls released", text)
        self.assertIn("controller release done", text)
        self.assertLess(text.index("controller release done"), text.index("exit (closed by the user)"))
        # SIGTERM releases too.
        dsi_log.unlink()
        process, dsi_log = self.run_with_controller(None, ([0] * 7,))
        self.wait_for(lambda: dsi_log.exists() and "DONE" in dsi_log.read_text())
        process.terminate()
        self.assertEqual(process.wait(timeout=10), 0)
        self.assertEqual(len(self.requests(dsi_log, 5)), 1)

    def test_debug_log_records_startup_controller_and_exit(self):
        try:
            left, right = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        left.close()
        right.close()
        debug = self.root / "manager" / "debug.log"
        debug.write_text("")
        debug.chmod(0o600)
        # An unused key (slot 3), then MENU (slot 0) with an invalid config.
        process, _ = self.run_with_controller("NBTCTL1\nknob_menu_slot=9\n",
                                              ([0, 0, 0, 1, 0, 0, 0], [0] * 7, [1, 0, 0, 0, 0, 0, 0]))
        self.assertEqual(process.wait(timeout=10), 0)
        lines = debug.read_text().splitlines()
        self.assertTrue(all(re.fullmatch(r"\d+\.\d{3} gui\[\d+\] .+", line) for line in lines), lines)
        text = "\n".join(line.split("] ", 1)[1] for line in lines)
        for expected in ("start slot -1", "worker connected", "controller config invalid; defaults",
                         "controller input started", "MOST attached; registering client",
                         "controls registered for 7 instances", "input menu",
                         "exit (closed by the user), status 0"):
            self.assertIn(expected, text)
        self.assertLess(text.index("controller input started"), text.index("input menu"))

    def test_debug_log_is_off_without_the_file(self):
        try:
            left, right = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        left.close()
        right.close()
        process, _ = self.run_with_controller(None, ([1, 0, 0, 0, 0, 0, 0],))
        self.assertEqual(process.wait(timeout=10), 0)
        self.assertEqual(sorted(p.name for p in (self.root / "manager").iterdir()), ["gui.lock"])

    def test_unavailable_worker_ends_the_gui_and_cleans_up_the_window(self):
        process = self.start("/nonexistent/worker")
        self.assertEqual(process.wait(timeout=10), 1)
        self.assertIn("post\n", self.log.read_text())
        self.assertIn("destroy-window\n", self.log.read_text())

    def test_lock_symlink_is_rejected(self):
        victim = Path(self.tmp.name) / "victim"
        victim.write_text("unchanged")
        (self.root / "manager/gui.lock").symlink_to(victim)
        process = self.start()
        self.assertNotEqual(process.wait(timeout=4), 0)
        self.assertEqual(victim.read_text(), "unchanged")
        self.assertFalse(self.log.exists())

    def test_launcher_arguments_and_missing_gui(self):
        self.assertEqual(subprocess.run([LAUNCHER]).returncode, 2)
        self.assertEqual(subprocess.run([LAUNCHER, "--unknown"]).returncode, 2)
        self.assertNotEqual(subprocess.run([LAUNCHER, "--open"]).returncode, 0)

    def test_supervised_gui_has_worker_heartbeats_and_a_lifetime_lease(self):
        try:
            left, right = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        left.close()
        right.close()
        lease = os.open(self.root / "lease", os.O_CREAT | os.O_RDWR, 0o600)
        reader, writer = os.pipe()
        try:
            process = subprocess.Popen([GUI, "--test-root", self.root, "--test-worker", self.worker,
                                        "--ready-fd", str(writer), "--lease-fd", str(lease)],
                                       pass_fds=(writer, lease), env=self.env)
            self.processes.append(process)
            os.close(writer)
            writer = -1
            for _ in range(2):
                self.assertTrue(select.select([reader], [], [], 8)[0])
                self.assertEqual(os.read(reader, 1), b"R")
            with self.assertRaises(BlockingIOError):
                fcntl.lockf(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
            os.close(reader)
            reader = -1
            self.assertNotEqual(process.wait(timeout=10), 0)
            self.assertTrue((self.root / "stopped").exists())
            fcntl.lockf(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
        finally:
            for fd in (reader, writer, lease):
                if fd >= 0:
                    os.close(fd)

    def handover_gui(self, last_signal):
        """A supervised GUI started right after one that ended for a manager
        update (NBT_MANAGER_HANDOVER), the plugin's last signal already there."""
        self.hold.stop()  # this test is the plugin
        debug = self.root / "manager" / "debug.log"
        debug.write_text("")
        debug.chmod(0o600)
        self.hold.signal.write_text(f"5 {last_signal}\n")
        lease = os.open(self.root / "lease", os.O_CREAT | os.O_RDWR, 0o600)
        reader, writer = os.pipe()
        self.addCleanup(os.close, lease)
        self.addCleanup(os.close, reader)
        process = subprocess.Popen([GUI, "--test-root", self.root, "--test-worker", self.worker,
                                    "--ready-fd", str(writer), "--lease-fd", str(lease)],
                                   pass_fds=(writer, lease), env=dict(self.env, NBT_MANAGER_HANDOVER="1"))
        self.processes.append(process)
        os.close(writer)
        self.assertTrue(select.select([reader], [], [], 8)[0])
        self.wait_for(lambda: (self.root / "started").exists() and "post\n" in self.log.read_text())
        time.sleep(0.3)
        return process, debug.read_text()

    def test_gui_after_an_update_shows_at_once_while_idrive_is_held(self):
        try:
            left, right = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        left.close()
        right.close()
        process, log = self.handover_gui("show")
        self.assertIn("started after the manager update; shown at once", log)
        self.assertEqual(int(self.hold.liveness.read_text().split()[0]), process.pid)
        # The GUI holds byte 0 of gui.lock only: the launcher's byte 1 is free.
        lock = os.open(self.root / "manager" / "gui.lock", os.O_RDWR)
        try:
            fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB, 1, 1)
            with self.assertRaises(OSError):
                fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB, 1, 0)
        finally:
            os.close(lock)

    def test_gui_after_an_update_stays_hidden_when_idrive_stepped_aside(self):
        try:
            left, right = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        left.close()
        right.close()
        process, log = self.handover_gui("aside")
        self.assertIsNone(process.poll())
        self.assertNotIn("shown at once", log)
        self.assertIn("shown once iDrive holds its focus", log)

    def test_supervised_gui_rejects_invalid_descriptors(self):
        result = subprocess.run([GUI, "--ready-fd", "3", "--lease-fd", "3"], env=self.env)
        self.assertEqual(result.returncode, 2)

    def test_supervised_gui_does_not_report_ready_when_worker_fails(self):
        try:
            left, right = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        left.close()
        right.close()
        bad_worker = Path(self.tmp.name) / "bad-worker"
        bad_worker.write_text('''#!/usr/bin/python3
import os
fd = 3
while os.read(fd, 1) != b"\\n":
    pass
os.write(fd, b"HELLO\\t1\\n")
while os.read(fd, 1) != b"\\n":
    pass
os.write(fd, b"ERR list\\n")
''')
        bad_worker.chmod(0o700)
        lease = os.open(self.root / "lease", os.O_CREAT | os.O_RDWR, 0o600)
        reader, writer = os.pipe()
        try:
            process = subprocess.Popen([GUI, "--test-root", self.root, "--test-worker", bad_worker,
                                        "--ready-fd", str(writer), "--lease-fd", str(lease)],
                                       pass_fds=(writer, lease), env=self.env)
            self.processes.append(process)
            os.close(writer)
            writer = -1
            self.assertTrue(select.select([reader], [], [], 8)[0])
            self.assertEqual(os.read(reader, 1), b"")
            self.assertNotEqual(process.wait(timeout=10), 0)
        finally:
            for fd in (reader, writer, lease):
                if fd >= 0:
                    os.close(fd)

    def test_worker_death_ends_the_gui_without_a_respawn(self):
        # The launcher owns the worker: the GUI never starts another one. A lost
        # channel ends the GUI (status 1) so the launcher restarts both.
        debug = self.root / "manager" / "debug.log"
        debug.write_text("")
        debug.chmod(0o600)
        worker = Path(self.tmp.name) / "dying-worker"
        worker.write_text('''#!/usr/bin/python3
import os, sys
fd = int(sys.argv[2])
root = sys.argv[4]
with open(root + "/starts", "a") as output:
    output.write(str(os.getpid()) + "\\n")
request = b""
while not request.endswith(b"\\n"):
    request += os.read(fd, 1)
os.write(fd, b"HELLO\\t1\\n")
os._exit(0)
''')
        worker.chmod(0o700)
        process = self.start(worker)
        self.assertEqual(process.wait(timeout=10), 1)
        text = debug.read_text()
        self.assertIn("worker channel lost; closing so the launcher restarts both", text)
        self.assertIn("exit (worker channel lost), status 1", text)
        self.assertEqual(len((self.root / "starts").read_text().splitlines()), 1)

    def test_gui_liveness_heartbeat_and_crash_recovery_notice(self):
        process = self.start()
        liveness = self.hold.liveness
        self.wait_for(liveness.exists)
        first = liveness.read_text()
        self.assertEqual(int(first.split()[0]), process.pid)
        self.wait_for(lambda: liveness.read_text() != first)
        process.kill()
        self.assertEqual(process.wait(timeout=5), -signal.SIGKILL)
        self.assertTrue(liveness.exists())

        debug = self.root / "manager" / "debug.log"
        debug.write_text("")
        debug.chmod(0o600)
        marker = self.root / "manager" / "crash.gui"
        marker.write_text("crash component=gui\n")
        process = self.start()
        self.wait_for(lambda: "recovered from a fault" in debug.read_text())
        self.assertFalse(marker.exists())
        process.terminate()
        self.assertEqual(process.wait(timeout=10), 0)


if __name__ == "__main__":
    unittest.main()
