import fcntl
import hashlib
import os
from pathlib import Path
import shutil
import select
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from test_app_host import BUILD, DAEMON, TEST_APP, lock_holder  # noqa: E402


SCHEMA = ("NBTSETTINGS1\nsetting=background|Background color|blue|blue,red,green,black\n"
          "setting=size|Text size|small|small,large\n")



# The car's HMI always has a library path; the manager adds the media libraries.
os.environ.setdefault("LD_LIBRARY_PATH", "/nbt-test/lib")


def hmi_runtime():
    """What every application gets of the HMI's environment (hmi_environment.hpp):
    NBT-AAD's PATH and, when set, the library path (with /opt/mm/lib, where the
    car's libasound finds liba2i_client.so), preload, graphics and time zone."""
    entries = [b"PATH=/bin:/usr/bin:/opt/os/bin:/opt/sys/bin:/opt/core/bin:/opt/car/bin:/fs/sda0/opt/mm/bin"]
    for name in ("LD_LIBRARY_PATH", "LD_PRELOAD", "GRAPHICS_ROOT", "TZ"):
        if name in os.environ:
            value = os.environ[name]
            if name == "LD_LIBRARY_PATH" and "/opt/mm/lib" not in value.split(":"):
                value += ":/opt/mm/lib"
            entries.append(f"{name}={value}".encode())
    return entries

class AppSettingsTests(unittest.TestCase):
    """Settings declared by a package, chosen through the worker, given to the application."""

    def setUp(self):
        try:
            left, right = socket.socketpair()
            left.close()
            right.close()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "root"
        for part in ("data/test", "apps/test/bin", "apps/plain/bin", "data/plain", "state/test", "state/plain", "state/manager",
                     "trust/apps", "trust/keys", "manager"):
            (self.root / part).mkdir(parents=True, mode=0o700)
        for path in [self.root, *self.root.rglob("*")]:
            path.chmod(0o700)
        for app in ("test", "plain"):
            active = self.root / f"apps/{app}/bin/app"
            shutil.copyfile(TEST_APP, active)
            active.chmod(0o700)
            digest = hashlib.sha256(active.read_bytes()).hexdigest()
            self.private(f"state/{app}/installed", "NBTSTATE1\nsequence=00000000000000000001\n"
                         f"highwater=00000000000000000001\nversion=v1\nsha256={digest}\n")
            self.private(f"trust/apps/{app}.conf", "NBTAPP1\ncompat=demo-1\n")
        self.private("state/test/settings.schema", SCHEMA)
        self.state = self.root / "state/test"
        self.daemons = []

    def tearDown(self):
        if hasattr(self, "state"):
            pid = lock_holder(self.state / "host.lock")
            if pid:
                os.kill(pid, signal.SIGKILL)
        for daemon, parent in getattr(self, "daemons", []):
            parent.close()
            if daemon.poll() is None:
                daemon.kill()
            daemon.communicate()
        if hasattr(self, "tmp"):
            self.tmp.cleanup()

    def private(self, path, text):
        (self.root / path).write_text(text)
        (self.root / path).chmod(0o600)

    def wait_for(self, predicate, seconds=8):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if predicate():
                return
            time.sleep(0.05)
        self.fail("timed out")

    def worker(self, fault=None):
        parent, child = socket.socketpair()
        parent.settimeout(5)
        daemon = subprocess.Popen([DAEMON, "--control-fd", str(child.fileno()), "--root", self.root],
                                  pass_fds=(child.fileno(),), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  env=dict(os.environ, NBT_TEST_SETTINGS_FAULT=fault or ""))
        child.close()
        self.daemons.append((daemon, parent))
        stream = parent.makefile("rb")

        def command(line):
            parent.sendall(line.encode() + b"\n")
            return stream.readline().decode().rstrip("\n")

        self.assertEqual(command("HELLO 1"), "HELLO\t1")
        return command

    def test_worker_lists_and_sets_declared_settings(self):
        command = self.worker()
        self.assertEqual(command("SETTINGS test"), "SETTINGS\t2\tbackground|Background color|blue|blue,red,green,black"
                                                   "\tsize|Text size|small|small,large")
        self.assertEqual(command("SETTINGS plain"), "SETTINGS\t0")
        self.assertEqual(command("SET test background red"), "OK")
        self.assertEqual((self.state / "settings").read_text(), "NBTVALUES1\nbackground=red\nsize=small\n")
        self.assertEqual((self.state / "settings").stat().st_mode & 0o777, 0o600)
        self.assertFalse((self.state / "settings.next").exists())
        self.assertIn("background|Background color|red|", command("SETTINGS test"))
        # Only declared keys, and only their choices.
        for line in ("SET test background pink", "SET test colour red", "SET plain background red",
                     "SET test background Red"):
            with self.subTest(line=line):
                self.assertEqual(command(line), "ERR settings")
        for line in ("SETTINGS", "SETTINGS missing", "SETTINGS ../test", "SETTINGS test x",
                     "SET test background", "SET test background red x", "SET missing background red"):
            with self.subTest(line=line):
                self.assertEqual(command(line), "ERR invalid-command")
        self.assertEqual((self.state / "settings").read_text(), "NBTVALUES1\nbackground=red\nsize=small\n")
        # A leftover .next from an interrupted write is replaced.
        self.private("state/test/settings.next", "partial")
        self.assertEqual(command("SET test size large"), "OK")
        self.assertEqual((self.state / "settings").read_text(), "NBTVALUES1\nbackground=red\nsize=large\n")
        self.assertFalse((self.state / "settings.next").exists())
        # Values for keys no longer declared, or no longer allowed, are ignored;
        # a malformed values file leaves the defaults.
        self.private("state/test/settings", "NBTVALUES1\nold=1\nbackground=pink\nsize=large\n")
        self.assertIn("\tsize|Text size|large|", command("SETTINGS test"))
        self.assertIn("background|Background color|blue|", command("SETTINGS test"))
        for text in ("garbage", "NBTVALUES1\nsize", "NBTVALUES1\nsize=large"):
            self.private("state/test/settings", text)
            self.assertIn("\tsize|Text size|small|", command("SETTINGS test"))
        # An invalid declaration is reported, not guessed at.
        self.private("state/test/settings.schema", "NBTSETTINGS1\nsetting=x|y|z|a\n")
        self.assertEqual(command("SETTINGS test"), "ERR settings")
        self.assertEqual(command("SET test x a"), "ERR settings")
        self.assertEqual(command("QUIT"), "OK")

    def live_app(self, optional=False):
        path = self.state / "installed"
        text = path.read_text().replace("NBTSTATE1", "NBTSTATE2")
        text += ("bridge=1.0\nbridge-uses=" + ("" if optional else "live-settings") +
                 "\nbridge-optional=" + ("live-settings,future" if optional else "") + "\n")
        self.private("state/test/installed", text)

    def test_twelve_settings_and_live_mode(self):
        choices = "abcdefghijkl,bcdefghijklm,cdefghijklmn,defghijklmno,efghijklmnop,fghijklmnopq"
        lines = [f"setting=key{i:013d}|Label with twenty chars|abcdefghijkl|{choices}\n" for i in range(12)]
        self.private("state/test/settings.schema", "NBTSETTINGS1\n" + "".join(lines))
        self.live_app(optional=True)
        command = self.worker()
        reply = command("SETTINGS test live")
        self.assertGreater(len(reply), 1024)
        self.assertTrue(reply.startswith("SETTINGS\t12\tlive\t"), reply)
        self.assertEqual(command("SET test key0000000000011 fghijklmnopq"), "OK")
        values = (self.root / "data/test/settings").read_text()
        self.assertIn("key0000000000011=fghijklmnopq\n", values)
        self.assertEqual(values, (self.state / "settings").read_text())
        self.assertTrue(command("SETTINGS plain live").startswith("SETTINGS\t0\trestart"))
        self.assertTrue(command("SETTINGS manager live").startswith("SETTINGS\t3\trestart"))
        for line in ("SETTINGS test unknown", "SETTINGS test live x"):
            self.assertEqual(command(line), "ERR invalid-command")
        # Exercise the largest environment: 12 values plus both paths,
        # bridge/version, status descriptor, launch mode and surface (19 entries).
        record = (self.state / "installed").read_text().replace("bridge-uses=\n", "bridge-uses=show,status\n")
        self.private("state/test/installed", record)
        self.assertEqual(command("START test"), "OK screen")
        self.wait_for(lambda: lock_holder(self.state / "lease"))
        env = [e for e in Path(f"/proc/{lock_holder(self.state / 'lease')}/environ").read_bytes().split(b"\0") if e]
        # Plus NBT-AAD's PATH and whichever HMI runtime variables are set.
        self.assertEqual(len(env), 19 + len(hmi_runtime()))
        self.assertIn(b"NBT_SURFACE=1291", env)
        self.assertTrue(set(hmi_runtime()) <= set(env))
        self.assertFalse([e for e in env if e.startswith(b"HOME=")])  # nothing else ambient
        self.assertIn(b"NBT_BRIDGE_CAPS=show,status,live-settings", env)
        self.assertIn(b"NBT_STATUS_FD=5", env)
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: command("RUNNING") == "RUNNING\t0")
        self.assertEqual(command("QUIT"), "OK")

    def test_live_snapshot_keeps_inode_and_is_repaired_before_start(self):
        self.live_app()
        command = self.worker()
        self.assertEqual(command("SET test background green"), "OK")
        snapshot = self.root / "data/test/settings"
        inode = snapshot.stat().st_ino
        # Readers holding the inode see shorter writes without a stale tail.
        with snapshot.open("rb") as reader:
            self.assertEqual(command("SET test background red"), "OK")
            self.assertEqual(snapshot.stat().st_ino, inode)
            self.assertEqual(reader.read(), b"NBTVALUES1\nbackground=red\nsize=small\n")
        snapshot.write_text("interrupted snapshot")
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: lock_holder(self.state / "lease"))
        self.assertEqual(snapshot.read_text(), (self.state / "settings").read_text())
        env = Path(f"/proc/{lock_holder(self.state / 'lease')}/environ").read_bytes().split(b"\0")
        self.assertIn(f"NBT_SETTINGS_FILE={snapshot}".encode(), env)
        self.assertIn(f"NBT_DATA_DIR={snapshot.parent}".encode(), env)
        self.assertIn(b"NBT_BRIDGE_CAPS=live-settings", env)
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: command("RUNNING") == "RUNNING\t0")
        self.assertEqual(command("QUIT"), "OK")

    def test_unsafe_live_destinations_preserve_protected_files_and_choices(self):
        command = self.worker()
        self.assertEqual(command("SET test background green"), "OK")
        snapshot = self.root / "data/test/settings"
        protected = self.root / "oem-file"
        protected.write_bytes(b"OEM bytes")
        protected.chmod(0o600)
        saved = (self.state / "settings").read_bytes()
        for kind in ("symlink", "hardlink", "fifo", "directory", "public"):
            with self.subTest(kind=kind):
                snapshot.unlink()
                if kind == "symlink": snapshot.symlink_to(protected)
                elif kind == "hardlink": os.link(protected, snapshot)
                elif kind == "fifo": os.mkfifo(snapshot, 0o600)
                elif kind == "directory": snapshot.mkdir(mode=0o700)
                else:
                    snapshot.write_bytes(b"untrusted")
                    snapshot.chmod(0o644)
                self.assertEqual(command("SET test background red"), "ERR settings")
                self.assertEqual(protected.read_bytes(), b"OEM bytes")
                self.assertEqual((self.state / "settings").read_bytes(), saved)
                if kind == "directory": snapshot.rmdir()
                else: snapshot.unlink()
                snapshot.write_bytes(b"replaced next SET")
                snapshot.chmod(0o600)
        # A directory alias must never receive manager writes either.
        snapshot.unlink()
        directory = snapshot.parent
        directory.rmdir()
        directory.symlink_to(self.state, target_is_directory=True)
        self.assertEqual(command("SET test background red"), "ERR settings")
        self.assertEqual((self.state / "settings").read_bytes(), saved)
        self.assertEqual(command("START test"), "ERR start")
        self.assertEqual(command("QUIT"), "OK")

    def test_settings_refuse_an_external_update_lock(self):
        command = self.worker()
        # A different process owns the conflicting lock (POSIX locks are per-process).
        with (self.state / "lock").open("a+b") as lock:
            (self.state / "lock").chmod(0o600)
            fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.assertEqual(command("SET test background red"), "ERR settings")
            self.assertFalse((self.state / "settings").exists())
        self.assertEqual(command("SET test background red"), "OK")
        self.assertEqual(command("QUIT"), "OK")

    def test_snapshot_write_truncate_and_sync_failures_can_be_retried(self):
        for fault in ("write", "truncate", "sync"):
            with self.subTest(fault=fault):
                command = self.worker()
                self.assertEqual(command("SET test size large"), "OK")
                self.assertEqual(command("QUIT"), "OK")
                self.daemons[-1][0].wait(timeout=5)
                command = self.worker(fault=fault)
                self.assertEqual(command("SET test size small"), "ERR settings")
                # The authoritative save precedes the snapshot, even when its
                # write or persistence fails. The caller is told to retry.
                self.assertIn("size=small\n", (self.state / "settings").read_text())
                self.assertEqual(command("QUIT"), "OK")
                self.daemons[-1][0].wait(timeout=5)
                command = self.worker()
                self.assertEqual(command("SET test size small"), "OK")
                self.assertEqual((self.root / "data/test/settings").read_bytes(),
                                 (self.state / "settings").read_bytes())
                self.assertEqual(command("QUIT"), "OK")
                self.daemons[-1][0].wait(timeout=5)

    def test_manager_settings(self):
        command = self.worker()
        manager = self.root / "manager"
        log, old, conf = manager / "debug.log", manager / "debug.log.1", manager / "controller.conf"
        self.assertEqual(command("SETTINGS manager"),
                         "SETTINGS\t3\tdebug_log|Debug log|off|off,on\tinvert|Invert knob|off|off,on"
                         "\trelease|Hand back controller|off|off,on")
        # Debug log on: created private, and the worker logs from then on.
        self.assertEqual(command("SET manager debug_log on"), "OK")
        self.assertEqual(log.stat().st_mode & 0o777, 0o600)
        self.assertIn("] debug log switched on\n", log.read_text())
        self.assertIn("\tdebug_log|Debug log|on|", command("SETTINGS manager"))
        self.assertEqual(command("SET manager debug_log on"), "OK")
        # Off keeps the last log, as debug.log.1, and the worker stops writing.
        before = log.read_text()
        self.assertEqual(command("SET manager debug_log off"), "OK")
        self.assertFalse(log.exists())
        # The command itself is the last line logged.
        before = old.read_text()
        self.assertTrue(before.endswith("] command SET\n"))
        self.assertIn("\tdebug_log|Debug log|off|", command("SETTINGS manager"))
        self.assertEqual(old.read_text(), before)
        self.assertEqual(command("SET manager debug_log off"), "OK")
        # On again starts a new file, and the worker follows it.
        self.assertEqual(command("SET manager debug_log on"), "OK")
        self.assertIn("] debug log switched on\n", log.read_text())
        self.assertEqual(old.read_text(), before)
        # A debug log the writers would refuse is reported, not trusted.
        log.chmod(0o666)
        self.assertEqual(command("SETTINGS manager"), "ERR settings")
        self.assertEqual(command("SET manager debug_log on"), "ERR settings")
        log.chmod(0o600)

        # Controller settings rewrite controller.conf with every key.
        self.assertEqual(command("SET manager invert on"), "OK")
        text = conf.read_text()
        self.assertTrue(text.startswith("NBTCTL1\n"))
        for line in ("knob_instance=03", "ccs_instance=01", "instances=01,02,03,05,10,12,14", "invert=1",
                     "knob_menu_slot=0", "knob_back_slot=1", "ccs_menu_slot=0", "ccs_back_slot=1", "release=0"):
            self.assertIn(f"\n{line}\n", text)
        self.assertEqual(conf.stat().st_mode & 0o777, 0o600)
        self.assertFalse((manager / "controller.conf.next").exists())
        self.assertIn("\tinvert|Invert knob|on|", command("SETTINGS manager"))
        # Keys set by the desktop installer keep their values.
        self.private("manager/controller.conf", "NBTCTL1\n# by hand\nknob_back_slot=4\ninstances=01,03\n")
        self.private("manager/controller.conf.next", "partial")
        self.assertIn("\tinvert|Invert knob|off|", command("SETTINGS manager"))
        self.assertEqual(command("SET manager release on"), "OK")
        text = conf.read_text()
        for line in ("knob_back_slot=4", "instances=01,03", "invert=0", "release=1"):
            self.assertIn(f"\n{line}\n", text)
        self.assertFalse((manager / "controller.conf.next").exists())
        # An invalid file is not in effect: the defaults are shown and written.
        self.private("manager/controller.conf", "NBTCTL1\nknob_back_slot=9\n")
        self.assertIn("\trelease|Hand back controller|off|", command("SETTINGS manager"))
        self.assertEqual(command("SET manager invert on"), "OK")
        self.assertIn("\nknob_back_slot=1\n", conf.read_text())
        # Only these keys, and only their choices.
        for line in ("SET manager invert On", "SET manager invert 1", "SET manager debug_log yes",
                     "SET manager knob_back_slot 2", "SET manager background red"):
            with self.subTest(line=line):
                self.assertEqual(command(line), "ERR settings")
        for line in ("SETTINGS manager x", "SET manager invert", "SET manager invert on x"):
            with self.subTest(line=line):
                self.assertEqual(command(line), "ERR invalid-command")
        self.assertIn("\nknob_back_slot=1\n", conf.read_text())

        # The screen's language: set, but not listed (it has more choices than
        # a declaration allows, so phones keep reading the manager's settings).
        language = manager / "language"
        self.assertFalse(language.exists())
        self.assertEqual(command("SET manager language fr"), "OK")
        self.assertEqual(language.read_text(), "fr\n")
        self.assertEqual(language.stat().st_mode & 0o777, 0o600)
        self.assertFalse((manager / "language.next").exists())
        self.assertEqual(command("SET manager language auto"), "OK")
        self.assertEqual(language.read_text(), "auto\n")
        for value in ("nl", "FR", "fr-FR", "automatic", "x" * 12):
            with self.subTest(value=value):
                self.assertEqual(command("SET manager language " + value), "ERR settings")
        self.assertEqual(language.read_text(), "auto\n")
        self.assertEqual(command("SETTINGS manager").count("\t"), 4)
        self.assertNotIn("language", command("SETTINGS manager"))
        self.assertEqual(command("QUIT"), "OK")

    def test_started_application_receives_its_settings(self):
        command = self.worker()
        self.assertEqual(command("SET test background green"), "OK")
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: lock_holder(self.state / "lease"))
        application = lock_holder(self.state / "lease")
        environment = Path(f"/proc/{application}/environ").read_bytes().split(b"\0")
        self.assertEqual(sorted(e for e in environment if e),
                         sorted([f"NBT_DATA_DIR={self.root}/data/test".encode(), b"NBT_LAUNCH=user",
                                 b"NBT_SETTING_BACKGROUND=green", b"NBT_SETTING_SIZE=small"] +
                                hmi_runtime()))
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: command("RUNNING") == "RUNNING\t0")
        # Without a declaration only the launch mode is passed, with the HMI's runtime.
        self.assertEqual(command("START plain"), "OK")
        plain_lease = self.root / "state/plain/lease"
        self.wait_for(lambda: lock_holder(plain_lease))
        environment = Path(f"/proc/{lock_holder(plain_lease)}/environ").read_bytes().split(b"\0")
        self.assertEqual(sorted(e for e in environment if e),
                         sorted([f"NBT_DATA_DIR={self.root}/data/plain".encode(), b"NBT_LAUNCH=user"] +
                                hmi_runtime()))
        self.assertEqual(command("STOP plain"), "OK")
        self.wait_for(lambda: command("RUNNING") == "RUNNING\t0")
        self.assertEqual(command("QUIT"), "OK")


class TestApplicationTests(unittest.TestCase):
    """The test application leaves on BACK when started from the menu."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.path = Path(self.tmp.name)
        self.lease = self.path / "lease"
        self.lease.write_bytes(b"")

    def tearDown(self):
        self.tmp.cleanup()

    def start(self, launch, script_keys=None, caps=None, settings_file=None, controller=None):
        env = {"NBT_LAUNCH": launch, "NBT_SETTING_BACKGROUND": "red",
               "NBT_FAKE_SCREEN_LOG": str(self.path / "screen.log")}
        if controller is not None:
            env["NBT_SETTING_CONTROLLER"] = controller
        if settings_file is not None:
            env.update(NBT_SETTINGS_FILE=str(settings_file),
                       NBT_FAKE_SCREEN_COLOR_LOG=str(self.path / "colors.log"))
        if caps is not None:
            env.update(NBT_BRIDGE="1.0", NBT_BRIDGE_CAPS=caps)
        if script_keys is not None:
            script = self.path / "controller"
            header = struct.pack("<IIIHHBBHB3xII", 0, 2, 0, 0x100, 0x5000, 0xE5, 3, 0xC47, 0xC, 0, 7)
            script.write_text("\n".join((header + bytes(k)).hex() for k in script_keys) + "\n")
            env.update(NBT_TEST_INPUT_LIBRARY=str(BUILD / "libfake_dsi.so"), NBT_FAKE_DSI_SCRIPT=str(script),
                       NBT_FAKE_DSI_LOG=str(self.path / "dsi.log"), NBT_TEST_ROOT=str(self.path / "missing"))
        ready_read, ready_write = os.pipe()
        lease = os.open(self.lease, os.O_RDWR)
        # Out of the way of 3 and 4 first, then onto them in the child.
        high = [fcntl.fcntl(fd, fcntl.F_DUPFD_CLOEXEC, 20) for fd in (ready_write, lease)]
        os.close(ready_write)
        os.close(lease)

        def descriptors():
            os.dup2(high[0], 3)
            os.dup2(high[1], 4)

        try:
            # close_fds would close 3 and 4 after preexec_fn; Python's own
            # descriptors are not inheritable anyway.
            process = subprocess.Popen([TEST_APP, "--ready-fd", "3", "--lease-fd", "4"], env=env, close_fds=False,
                                       preexec_fn=descriptors, stdout=subprocess.DEVNULL, stderr=subprocess.PIPE)
        finally:
            for fd in high:
                os.close(fd)
        self.addCleanup(os.close, ready_read)
        return process, ready_read

    def test_live_background_and_invalid_snapshot_fallback(self):
        snapshot = self.path / "settings"
        snapshot.write_text("NBTVALUES1\nbackground=green\n")
        snapshot.chmod(0o600)
        process, ready = self.start("user", script_keys=([0] * 7,), caps="live-settings", settings_file=snapshot)
        colors = self.path / "colors.log"
        def wait_color(color):
            deadline = time.monotonic() + 3
            while time.monotonic() < deadline:
                if colors.exists() and colors.read_text().splitlines()[-1:] == [color]: return
                time.sleep(0.03)
            self.fail("background did not change: " + (colors.read_text() if colors.exists() else "no posts"))
        try:
            self.assertEqual(os.read(ready, 1), b"R")
            wait_color("ff1f7a3e")
            snapshot.write_text("NBTVALUES1\nbackground=blue\n")
            wait_color("ff1d4f91")
            before = colors.read_text()
            for text in ("truncated", "NBTVALUES1\nbackground=red", "x" * 500,
                         "NBTVALUES1\nbackground=red\nmalformed\n"):
                snapshot.write_text(text)
                time.sleep(1.1)
                self.assertEqual(colors.read_text(), before)
            snapshot.write_text("NBTVALUES1\nbackground=red\n")
            wait_color("ff9a2323")
            self.assertIsNone(process.poll())
        finally:
            process.send_signal(signal.SIGTERM)
            _, errors = process.communicate(timeout=10)
        self.assertEqual(process.returncode, 0, errors)

    def test_show_restores_the_existing_window_and_heartbeats_continue(self):
        process, ready = self.start("user", caps="show")
        log = self.path / "screen.log"
        try:
            self.assertEqual(os.read(ready, 1), b"R")
            # Without controller input the test window steps aside after five
            # seconds. Showing it must reset that timer, not its health timer.
            deadline = time.monotonic() + 8
            while "hide" not in log.read_text().splitlines() and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertIn("hide", log.read_text().splitlines())
            process.send_signal(signal.SIGUSR1)
            deadline = time.monotonic() + 2
            while log.read_text().splitlines().count("show") < 2 and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertEqual(log.read_text().splitlines()[-2:], ["post", "show"])
            time.sleep(0.3)
            self.assertEqual(log.read_text().splitlines()[-1], "show")
            self.assertIsNone(process.poll())
            self.assertTrue(select.select([ready], [], [], 2)[0], "heartbeat stopped after show")
            self.assertIn(b"R", os.read(ready, 32))
        finally:
            process.send_signal(signal.SIGTERM)
            _, errors = process.communicate(timeout=10)
        self.assertEqual(process.returncode, 0, errors)
        self.assertEqual(log.read_text().splitlines()[-3:], ["hide", "destroy-window", "destroy-context"])

    def test_show_is_ignored_during_health_checks_or_without_a_grant(self):
        for launch, caps in (("check", "show"), ("user", None), ("user", "status")):
            with self.subTest(launch=launch, caps=caps):
                log = self.path / "screen.log"
                log.unlink(missing_ok=True)
                process, ready = self.start(launch, caps=caps)
                try:
                    self.assertEqual(os.read(ready, 1), b"R")
                    before = log.read_text()
                    process.send_signal(signal.SIGUSR1)
                    time.sleep(0.25)
                    self.assertIsNone(process.poll())
                    self.assertEqual(log.read_text(), before)
                finally:
                    process.send_signal(signal.SIGTERM)
                    process.communicate(timeout=10)
                self.assertEqual(process.returncode, 0)

    def test_back_ends_a_user_launch(self):
        # BACK (key slot 1) pressed and released.
        process, ready = self.start("user", ([0, 1, 0, 0, 0, 0, 0], [0] * 7))
        _, errors = process.communicate(timeout=10)
        self.assertEqual(process.returncode, 0, errors)
        self.assertEqual(os.read(ready, 1), b"R")
        log = (self.path / "screen.log").read_text().splitlines()
        self.assertIn("post", log)
        self.assertEqual(log[-3:], ["hide", "destroy-window", "destroy-context"])

    def test_health_check_leaves_the_controller_alone(self):
        # The same BACK press is not read during a health check.
        process, ready = self.start("check", ([0, 1, 0, 0, 0, 0, 0], [0] * 7))
        try:
            with self.assertRaises(subprocess.TimeoutExpired):
                process.wait(timeout=2)
            self.assertEqual(os.read(ready, 1), b"R")
            self.assertFalse((self.path / "dsi.log").exists())
        finally:
            process.send_signal(signal.SIGTERM)
        process.communicate(timeout=10)
        self.assertEqual(process.returncode, 0)

    def test_controller_setting_off_leaves_the_controller_alone(self):
        # Started from the menu with "Use the controller" off, the same BACK
        # press is not read; the app stays running hidden so show can be tried.
        process, ready = self.start("user", ([0, 1, 0, 0, 0, 0, 0], [0] * 7), caps="show", controller="off")
        log = self.path / "screen.log"
        try:
            with self.assertRaises(subprocess.TimeoutExpired):
                process.wait(timeout=2)
            self.assertEqual(os.read(ready, 1), b"R")
            self.assertFalse((self.path / "dsi.log").exists())
            deadline = time.monotonic() + 8
            while "hide" not in log.read_text().splitlines() and time.monotonic() < deadline:
                time.sleep(0.05)
            self.assertIn("hide", log.read_text().splitlines())
            self.assertIsNone(process.poll())
        finally:
            process.send_signal(signal.SIGTERM)
        _, errors = process.communicate(timeout=10)
        self.assertEqual(process.returncode, 0, errors)

    def test_invalid_controller_setting_keeps_the_default(self):
        process, ready = self.start("user", ([0, 1, 0, 0, 0, 0, 0], [0] * 7), controller="maybe")
        _, errors = process.communicate(timeout=10)
        self.assertEqual(process.returncode, 0, errors)
        self.assertTrue((self.path / "dsi.log").exists())


if __name__ == "__main__":
    unittest.main()
