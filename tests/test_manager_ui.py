# SPDX-License-Identifier: MPL-2.0
from phone_client import SoftwarePhone
import fcntl
import hashlib
import os
import shutil
import signal
import struct
import time
from pathlib import Path
import socket
import subprocess
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build"))
PROBE = BUILD / "ui-probe"
DAEMON = BUILD / "managerd-test"
PACK = ROOT / "tools/package.py"
LOADING, APPS, SETTINGS, PAIRING, REVIEW, CONFIRM, PROGRESS, RESULT, ERROR, CHOICE, UNINSTALL, WAITING, PHONE_CONFIRM = range(13)
STATUS, GAMEPAD_TEST = 16, 17
TEST_APP = BUILD / "test-app-host"


def run(*args):
    return subprocess.run([str(a) for a in args], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=False)


def elf(content):
    out = bytearray(128)
    out[:7] = b"\x7fELF\x01\x01\x01"
    out[16:20] = b"\x02\x00\x28\x00"
    out[52:52 + len(content)] = content
    return bytes(out)


class View:
    def __init__(self, line):
        fields = line.rstrip("\n").split("\t")
        if len(fields) != 13 or fields[0] != "VIEW":
            raise AssertionError("unexpected probe output: " + line)
        self.screen, self.busy, self.focus, self.exit = (int(x) for x in fields[1:5])
        self.title = fields[5]
        self.lines = fields[6].split("|") if fields[6] else []
        self.items = fields[7].split("|") if fields[7] else []
        # The dialog over the view, if any.
        self.modal_title, self.modal_text = fields[8], fields[9]
        self.modal_progress = int(fields[10])
        self.modal_items = fields[11].split("|") if fields[11] else []
        self.modal_focus = int(fields[12])

    def text(self):
        return "\n".join([self.title, *self.lines, *self.items])


class ManagerUiTests(unittest.TestCase):
    """Drives the GUI controller against the real worker over its private channel."""

    @classmethod
    def setUpClass(cls):
        cls.keys = tempfile.TemporaryDirectory()
        cls.key = Path(cls.keys.name) / "key.pem"
        cls.public = Path(cls.keys.name) / "public.pem"
        for args in (("openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt",
                      "rsa_keygen_bits:3072", "-out", cls.key),
                     ("openssl", "pkey", "-in", cls.key, "-pubout", "-out", cls.public)):
            result = run(*args)
            if result.returncode:
                raise RuntimeError(result.stderr.decode())

    @classmethod
    def tearDownClass(cls):
        cls.keys.cleanup()

    def setUp(self):
        self.enterContext(mock.patch.dict(os.environ, {"NBT_TEST_APPS_KEY": str(self.public)}))
        try:
            left, right = socket.socketpair()
            left.sendall(b"x")
            right.recv(1)
            left.close()
            right.close()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "managed"
        for part in ("data/test", "apps/test/bin", "state/test", "state/manager", "trust/apps", "trust/keys", "manager"):
            (self.root / part).mkdir(parents=True, exist_ok=True, mode=0o700)
        for path in [self.root, *self.root.rglob("*")]:
            if path.is_dir():
                path.chmod(0o700)
        self.active = self.root / "apps/test/bin/app"
        self.old = elf(b"old version")
        self.active.write_bytes(self.old)
        self.active.chmod(0o700)
        record = ("NBTSTATE1\nsequence=00000000000000000001\n"
                  "highwater=00000000000000000001\nversion=v1\n"
                  f"sha256={hashlib.sha256(self.old).hexdigest()}\n")
        self.private(self.root / "state/test/installed", record.encode())
        self.private(self.root / "trust/apps/test.conf", b"NBTAPP1\ncompat=demo-1\n")
        source = Path(self.tmp.name) / "source"
        source.write_bytes(elf(b"new version"))
        package = Path(self.tmp.name) / "package"
        result = run("python3", PACK, "--app", "test", "--version", "v2", "--sequence", 2,
                     "--compat", "demo-1", "--key", self.key, "--output", package, source)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.package = package.read_bytes()
        self.phone = SoftwarePhone(self.tmp.name)
        conn = Path(self.tmp.name) / "car"
        (conn / "var/opt/conn/carplay").mkdir(parents=True)
        secret = b"development-only"
        mask = b"NBTEvo0715"
        (conn / "var/opt/conn/WFDPass.conf").write_text(bytes(c ^ mask[i % len(mask)] for i,c in enumerate(secret)).hex())
        (conn / "var/opt/conn/carplay/iap2_Wifi.cfg").write_text("SSID=BMW Test CarPlay\n")
        self.enterContext(mock.patch.dict(os.environ, {"NBT_TEST_CONN_ROOT": str(conn)}))
        self.probe = subprocess.Popen([PROBE, DAEMON, self.root], stdin=subprocess.PIPE,
                                      stdout=subprocess.PIPE, text=True)
        self.initial = self.view()

    def tearDown(self):
        if hasattr(self, "probe"):
            if self.probe.poll() is None:
                self.probe.kill()
            self.probe.communicate()
        if hasattr(self, "tmp"):
            self.tmp.cleanup()

    @staticmethod
    def private(path, data):
        path.write_bytes(data)
        path.chmod(0o600)

    def view(self):
        return View(self.probe.stdout.readline())

    def send(self, *commands):
        for command in commands:
            self.probe.stdin.write(command + "\n")
            self.probe.stdin.flush()
            if command in ("link", "address"):
                line = self.probe.stdout.readline()
                if command == "link":
                    self.assertTrue(line.startswith("LINK\t"), line)
                    self.link = line.rstrip("\n")[5:]
                else:
                    self.assertTrue(line.startswith("ADDRESS\t"), line)
                    self.host = line.rstrip("\n")[8:]
            view = self.view()
        return view

    def finish(self):
        self.probe.stdin.close()
        self.assertEqual(self.probe.wait(timeout=10), 0)

    def until(self, screen):
        for _ in range(8):
            view = self.send("poll")
            if view.screen == screen:
                return view
        self.fail(f"Expected screen {screen}: {view.text()}")

    def pair_phone(self):
        # Settings is the main menu's last item; Pair a phone follows its three
        # settings and Language.
        view = self.send("down", "down", "down", "select")
        self.assertEqual(view.screen, SETTINGS, view.text())
        self.assertEqual(view.items[3:], ["Language: Automatic (English)", "Pair a phone",
                                          "Paired phones", "Status"])
        view = self.send("down", "down", "down", "down", "select", "address", "link")
        self.assertEqual(view.screen, PAIRING, view.text())
        self.assertTrue(self.link.startswith("WIFI:T:WPA;"))
        self.assertNotIn("http", self.link)
        self.assertEqual(self.phone.pair_request(self.host)[0], 202)
        view = self.until(PHONE_CONFIRM)
        self.assertEqual(view.focus, 0)
        self.assertEqual(view.items, ["No", "Yes, pair"])
        view = self.send("down", "select")
        self.assertEqual(view.screen, RESULT, view.text())
        self.assertIn("Phone paired", view.lines)
        status, data = self.phone.request(self.host, "GET", "/v1/status")
        self.assertEqual(status, 200)
        self.assertIn(b"STATUS\tpaired\tPhone paired\nCAR\t02:11:22:33:44:55\t", data)
        self.send("select")

    def upload_review(self, package=None):
        self.assertEqual(self.phone.request(self.host, "POST", "/v1/upload", package or self.package)[0], 202)
        view = self.until(REVIEW)
        self.assertEqual(view.focus, 0)
        self.assertEqual(view.items[0], "Reject")
        return view

    def test_signed_upload_approval_failure_and_rollback_are_visible(self):
        self.pair_phone()
        view = self.upload_review()
        self.assertIn("Installed: v1, new: v2", view.lines)
        view = self.send("down", "select")
        self.assertEqual((view.screen, view.focus), (CONFIRM, 0))
        view = self.send("down", "select")
        if view.screen != RESULT:
            view = self.until(RESULT)
        self.assertIn("Update failed", view.lines)
        self.assertEqual(self.active.read_bytes(), self.old)
        status, data = self.phone.request(self.host, "GET", "/v1/status")
        self.assertEqual(status, 200)
        self.assertIn(b"\tfailed\tUpdate failed; previous application restored", data)
        self.finish()

    def test_rejection_and_remote_uninstall_need_the_car(self):
        self.pair_phone()
        self.upload_review()
        view = self.send("select")
        self.assertEqual(view.screen, APPS)
        self.assertIn(b"\tcancelled\t", self.phone.request(self.host, "GET", "/v1/status")[1])
        self.assertEqual(self.phone.request(self.host, "POST", "/v1/remove", b"test\n")[0], 202)
        view = self.until(UNINSTALL)
        self.assertEqual(view.focus, 0)
        self.assertIn("Requested by Test phone", view.lines)
        self.assertTrue(self.active.exists())
        self.send("select")
        self.assertIn(b"\tcancelled\t", self.phone.request(self.host, "GET", "/v1/status")[1])
        self.send("select")
        self.assertEqual(self.phone.request(self.host, "POST", "/v1/remove", b"test\n")[0], 202)
        self.until(UNINSTALL)
        view = self.send("down", "select")
        self.assertIn("Application uninstalled", view.lines)
        self.assertFalse(self.active.exists())
        self.assertIn(b"\tremoved\t", self.phone.request(self.host, "GET", "/v1/status")[1])
        self.finish()

    def test_first_install_and_settings_apply_without_another_car_prompt(self):
        self.pair_phone()
        source = Path(self.tmp.name) / "other.elf"
        source.write_bytes(elf(b"new application"))
        package = Path(self.tmp.name) / "other.nbt"
        result = run("python3", PACK, "--app", "other", "--version", "v1", "--sequence", 1,
                     "--compat", "demo-1", "--key", self.key, "--output", package,
                     "--setting", "background|Background|blue|blue,red", source)
        self.assertEqual(result.returncode, 0, result.stderr)
        view = self.upload_review(package.read_bytes())
        self.assertEqual(view.items, ["Reject", "Install"])
        view = self.send("down", "select", "down", "select")
        if view.screen != RESULT:
            view = self.until(RESULT)
        self.assertIn("Update installed", view.lines)
        self.assertEqual(self.phone.request(self.host, "POST", "/v1/settings/read", b"other\n"),
                         (200, b"SETTINGS\t1\nbackground|Background|blue|blue,red\n"))
        self.assertEqual(self.phone.request(self.host, "POST", "/v1/settings/set", b"other\tbackground\tred\n"),
                         (200, b"OK\n"))
        self.assertIn(b"background|Background|red|", self.phone.request(self.host, "POST", "/v1/settings/read", b"other\n")[1])
        self.assertEqual(self.phone.request(self.host, "POST", "/v1/settings/set", b"other\tbackground\tinvalid\n")[0], 409)
        self.assertEqual(self.send("poll").screen, RESULT)
        self.finish()

    def test_pairing_qr_contains_only_wifi(self):
        try:
            import zxingcpp
            from PIL import Image
        except ImportError:
            self.skipTest("zxing-cpp or Pillow not installed")
        self.send("down", "select", "select", "link")
        snapshot = Path(self.tmp.name) / "screen.pgm"
        self.send("snapshot " + str(snapshot))
        results = zxingcpp.read_barcodes(Image.open(snapshot))
        self.assertEqual([r.text for r in results], [self.link])
        self.assertTrue(self.link.startswith("WIFI:"))
        self.send("back")
        self.finish()

    def test_status_page_and_gamepad_test_render(self):
        # Settings > Status (its last item), then the gamepad test, each frame
        # drawn by the renderer; BACK returns to Status, then to Settings.
        view = self.send("down", "down", "down", "select")
        view = self.send("down", "down", "down", "down", "down", "down", "select")
        self.assertEqual((view.screen, view.title), (STATUS, "Status"), view.text())
        self.assertEqual(view.items, ["Test the gamepad"])
        self.assertIn("Last error: none since the manager opened", view.lines)
        view = self.send("select")
        self.assertEqual((view.screen, view.title), (GAMEPAD_TEST, "Gamepad test"), view.text())
        self.assertEqual(view.items, [])
        self.assertIn("BACK on the iDrive controller leaves", view.lines)
        view = self.send("back")
        self.assertEqual(view.screen, STATUS, view.text())
        view = self.send("back")
        self.assertEqual((view.screen, view.focus), (SETTINGS, 6), view.text())
        self.send("back")
        self.finish()

    def lock_holder(self, path):
        try:
            fd = os.open(path, os.O_RDONLY)
        except FileNotFoundError:
            return 0
        try:
            request = struct.pack("hhqqi", fcntl.F_WRLCK, os.SEEK_SET, 0, 0, 0) + b"\0" * 4
            kind, _, _, _, pid = struct.unpack("hhqqi", fcntl.fcntl(fd, fcntl.F_GETLK, request)[:28])
            return 0 if kind == fcntl.F_UNLCK else pid
        finally:
            os.close(fd)

    def test_running_worker_leaves_the_manager_lock_to_the_launcher(self):
        # The launcher confirms a manager trial while its worker runs; the
        # worker holding the manager transaction lock made that fail.
        self.assertFalse(self.lock_holder(self.root / "state/manager/lock"))

    def test_push_starts_and_settings_stop_an_application(self):
        # A host-runnable application this time.
        shutil.copyfile(TEST_APP, self.active)
        self.active.chmod(0o700)
        record = ("NBTSTATE1\nsequence=00000000000000000001\nhighwater=00000000000000000001\nversion=v1\n"
                  f"sha256={hashlib.sha256(self.active.read_bytes()).hexdigest()}\n")
        self.private(self.root / "state/test/installed", record.encode())
        host_lock = self.root / "state/test/host.lock"
        try:
            view = self.send("select")
            # The application starts and the manager closes.
            self.assertEqual(view.exit, 1, view.text())
            deadline = time.monotonic() + 8
            while not self.lock_holder(self.root / "state/test/lease") and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertTrue(self.lock_holder(host_lock))
            self.finish()
            # Opening the manager again: the application is still running.
            self.probe = subprocess.Popen([PROBE, DAEMON, self.root], stdin=subprocess.PIPE,
                                          stdout=subprocess.PIPE, text=True)
            view = self.view()
            self.assertEqual(view.items[0], "test  (running)")
            view = self.send("select")
            self.assertEqual(view.exit, 0, view.text())
            self.assertIn("test is already running", view.lines)
            # Holding the push opens its settings: Start with the car, then stopping it.
            view = self.send("hold")
            self.assertEqual((view.screen, view.title, view.items),
                             (SETTINGS, "test settings",
                              ["Start with the car: off", "Stop test", "Uninstall test"]))
            self.assertFalse(view.modal_title)
            view = self.send("release", "select")
            for _ in range(10):
                if view.items[0] == "Start with the car: on":
                    break
                view = self.send("poll")
            self.assertEqual(view.items[0], "Start with the car: on")
            self.assertEqual((self.root / "state/test/autostart").read_text(),
                             "NBTAUTO1\nenabled=1\n")
            view = self.send("down", "select")
            self.assertEqual(view.screen, SETTINGS)
            for _ in range(10):
                if "test stopped" in view.lines:
                    break
                view = self.send("poll")
            self.assertIn("test stopped", view.lines)
            self.assertEqual(view.items, ["Start with the car: on", "Uninstall test"])
            self.assertFalse(self.lock_holder(host_lock))
            # Stopped, it can be uninstalled: asked first, then gone with its data.
            view = self.send("select")
            self.assertEqual((view.title, view.items), ("Uninstall test?", ["No", "Yes, uninstall"]))
            view = self.send("down", "select")
            for _ in range(10):
                if view.screen == APPS and "test uninstalled" in view.lines:
                    break
                view = self.send("poll")
            self.assertIn("test uninstalled", view.lines)
            self.assertEqual(view.items[0], "Settings")
            for path in ("apps/test", "state/test", "data/test", "trust/apps/test.conf"):
                self.assertFalse((self.root / path).exists(), path)
            self.finish()
        finally:
            pid = self.lock_holder(host_lock)
            if pid:
                os.kill(pid, signal.SIGKILL)


if __name__ == "__main__":
    unittest.main()
