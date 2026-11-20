# SPDX-License-Identifier: MPL-2.0
import fcntl
import hashlib
import os
from pathlib import Path
import shutil
import signal
import socket
import struct
import subprocess
import tempfile
import time
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build"))
DAEMON = BUILD / "managerd-test"
TEST_APP = BUILD / "test-app-host"


def lock_holder(path):
    """The pid holding a write-conflicting lock on `path`, or 0."""
    try:
        fd = os.open(path, os.O_RDONLY)
    except FileNotFoundError:
        return 0
    try:
        # struct flock on Linux: short type, short whence, off_t start, off_t len, pid_t pid.
        request = struct.pack("hhqqi", fcntl.F_WRLCK, os.SEEK_SET, 0, 0, 0)
        reply = fcntl.fcntl(fd, fcntl.F_GETLK, request + b"\0" * 4)
        kind, _, _, _, pid = struct.unpack("hhqqi", reply[:struct.calcsize("hhqqi")])
        return 0 if kind == fcntl.F_UNLCK else pid
    finally:
        os.close(fd)


def show_holder(path, byte=1):
    """PID advertising show, or 0; a whole-file write lock is not a marker."""
    try:
        fd = os.open(path, os.O_RDONLY)
    except FileNotFoundError:
        return 0
    try:
        request = struct.pack("hhqqi", fcntl.F_WRLCK, os.SEEK_SET, byte, 1, 0)
        reply = fcntl.fcntl(fd, fcntl.F_GETLK, request + b"\0" * 4)
        kind, _, start, length, pid = struct.unpack("hhqqi", reply[:struct.calcsize("hhqqi")])
        return pid if kind == fcntl.F_RDLCK and start == byte and length == 1 else 0
    finally:
        os.close(fd)


class AppHostTests(unittest.TestCase):
    """Running applications outlive the worker in detached hosts."""

    def setUp(self):
        try:
            left, right = socket.socketpair()
            left.close()
            right.close()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "root"
        for part in ("data/test", "apps/test/bin", "state/test", "state/manager", "trust/apps", "trust/keys", "manager"):
            (self.root / part).mkdir(parents=True, mode=0o700)
        for path in [self.root, *self.root.rglob("*")]:
            path.chmod(0o700)
        active = self.root / "apps/test/bin/app"
        shutil.copyfile(TEST_APP, active)
        active.chmod(0o700)
        digest = hashlib.sha256(active.read_bytes()).hexdigest()
        for path, text in (("state/test/installed", "NBTSTATE1\nsequence=00000000000000000001\n"
                            f"highwater=00000000000000000001\nversion=v1\nsha256={digest}\n"),
                           ("trust/apps/test.conf", "NBTAPP1\ncompat=demo-1\n")):
            (self.root / path).write_text(text)
            (self.root / path).chmod(0o600)
        self.host_lock = self.root / "state/test/host.lock"
        self.surface_lock = self.root / "state/manager/surface.lock"
        self.lease = self.root / "state/test/lease"
        self.daemons = []

    def tearDown(self):
        # Never leave a detached host (or its application) behind.
        if hasattr(self, "host_lock"):
            pid = lock_holder(self.host_lock)
            if pid:
                os.kill(pid, signal.SIGKILL)
            self.wait_for(lambda: not lock_holder(self.lease), fail=False)
        for daemon, parent in getattr(self, "daemons", []):
            parent.close()
            if daemon.poll() is None:
                daemon.kill()
            daemon.communicate()
        if hasattr(self, "tmp"):
            self.tmp.cleanup()

    def wait_for(self, predicate, seconds=8, fail=True):
        deadline = time.monotonic() + seconds
        while time.monotonic() < deadline:
            if predicate():
                return True
            time.sleep(0.05)
        if fail:
            detail = ""
            if getattr(self, "trace", None) and self.trace.exists():
                detail += "\napp events:\n" + self.trace.read_text()
            if (self.root / "dsi-log").exists():
                detail += "\nDSI log:\n" + (self.root / "dsi-log").read_text()[-3000:]
            if (self.root / "state/test/output.log").exists():
                detail += "\napplication log:\n" + (self.root / "state/test/output.log").read_text()[-3000:]
            if (self.root / "manager/debug.log").exists():
                detail += "\nmanager log:\n" + (self.root / "manager/debug.log").read_text()[-3000:]
            self.fail("timed out" + detail)
        return False

    def worker(self, ignore_sigchld=False):
        parent, child = socket.socketpair()
        parent.settimeout(5)
        daemon = subprocess.Popen([DAEMON, "--control-fd", str(child.fileno()), "--root", self.root],
                                  pass_fds=(child.fileno(),), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  preexec_fn=(lambda: signal.signal(signal.SIGCHLD, signal.SIG_IGN)) if ignore_sigchld else None)
        child.close()
        self.daemons.append((daemon, parent))
        stream = parent.makefile("rb")

        def command(line):
            parent.sendall(line.encode() + b"\n")
            return stream.readline().decode().rstrip("\n")

        self.assertEqual(command("HELLO 1"), "HELLO\t1")
        return daemon, command

    def traced_app(self, uses=None, optional="", ready_delay=0, ignore_stop=False):
        """An app with SIGUSR1's default fatal action unless actually granted show."""
        self.trace = self.root / "app-events"
        self.trace.write_text("")
        program = r"""#!/usr/bin/python3
import fcntl, os, signal, time
path = @LOG@
stopping = showing = False
def event(text):
    with open(path, "a") as output:
        output.write(text + "\n")
def stop(number, frame):
    global stopping
    stopping = True
def show(number, frame):
    global showing
    showing = True
signal.signal(signal.SIGTERM, signal.SIG_IGN if @IGNORE_STOP@ else stop)
if "show" in os.environ.get("NBT_BRIDGE_CAPS", "").split(","):
    signal.signal(signal.SIGUSR1, show)
fcntl.lockf(4, fcntl.LOCK_SH | fcntl.LOCK_NB)
event("start " + str(os.getpid()))
event("bridge=" + os.environ.get("NBT_BRIDGE", "absent"))
event("caps=" + os.environ.get("NBT_BRIDGE_CAPS", "absent"))
event("status=" + os.environ.get("NBT_STATUS_FD", "absent"))
event("surface=" + os.environ.get("NBT_SURFACE", "absent"))
try:
    os.fstat(5)
    event("fd5=open")
except OSError:
    event("fd5=closed")
time.sleep(@DELAY@)
last = 0
while not stopping:
    now = time.monotonic()
    if now - last >= 0.5:
        try:
            os.write(3, b"R")
        except BrokenPipeError:
            break
        last = now
    if showing:
        showing = False
        event("show")
    time.sleep(0.02)
event("exit")
""".replace("@LOG@", repr(str(self.trace))).replace("@DELAY@", repr(ready_delay)).replace("@IGNORE_STOP@", repr(ignore_stop))
        active = self.root / "apps/test/bin/app"
        active.write_text(program)
        active.chmod(0o700)
        record = ("NBTSTATE1\nsequence=00000000000000000001\nhighwater=00000000000000000001\n"
                  f"version=v1\nsha256={hashlib.sha256(active.read_bytes()).hexdigest()}\n")
        if uses is not None:
            record = record.replace("NBTSTATE1\n", "NBTSTATE2\n")
            record += f"bridge=1.5\nbridge-uses={uses}\nbridge-optional={optional}\n"
        (self.root / "state/test/installed").write_text(record)

    def events(self):
        return self.trace.read_text().splitlines()


    def test_status_grant_readiness_worker_restart_and_fresh_instance(self):
        self.traced_app(uses="show,status", ready_delay=0.8)
        path = self.root / "nbt-bridge1-test.status"
        path.write_bytes(b"99 old run\n")
        path.chmod(0o600)
        daemon, command = self.worker(ignore_sigchld=True)
        self.assertEqual(command("START test"), "OK screen")
        self.wait_for(lambda: "status=5" in self.events())
        self.assertIn("fd5=open", self.events())
        self.assertEqual(path.read_bytes(), b"")
        path.write_bytes(b"1 Connecting\n")
        self.assertEqual(command("RUNNING status"), "RUNNING\t1\ttest")  # not ready
        self.wait_for(lambda: show_holder(self.host_lock, 3))
        self.assertEqual(command("RUNNING status"), "RUNNING\t1\ttest|Connecting")
        self.assertEqual(command("RUNNING"), "RUNNING\t1\ttest")
        self.assertEqual(command("START test"), "OK screen")  # both markers coexist
        self.wait_for(lambda: "show" in self.events())
        self.assertEqual(command("QUIT"), "OK")
        self.assertEqual(daemon.wait(timeout=10), 0)
        _, command = self.worker()
        self.assertEqual(command("RUNNING status"), "RUNNING\t1\ttest|Connecting")
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: command("STATUS").split("\t")[7] == "Application stopped")
        self.assertEqual(command("RUNNING status"), "RUNNING\t0")
        self.assertEqual(path.read_bytes(), b"1 Connecting\n")  # harmless leftover
        self.assertEqual(command("START test"), "OK screen")
        self.wait_for(lambda: show_holder(self.host_lock, 3))
        self.assertEqual(path.read_bytes(), b"")
        self.assertEqual(command("RUNNING status"), "RUNNING\t1\ttest")

    def test_status_invalid_and_unsafe_files_fall_back_without_modification(self):
        self.traced_app(uses="status")
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: show_holder(self.host_lock, 3))
        path = self.root / "nbt-bridge1-test.status"
        for content in (b"", b"1 Missing newline", b"01 Bad\n", b"1 x\nextra", b"1 \x00\n",
                        b"1 \xff\n", b"1 Tab\tvalue\n", b"1 " + b"x" * 25 + b"\n", b"x" * 65536):
            with self.subTest(content=content[:50]):
                path.write_bytes(content)
                self.assertEqual(command("RUNNING status"), "RUNNING\t1\ttest")
        path.write_bytes(b"18446744073709551615 abcdefghijklmnopqrstuvwx\n")
        self.assertEqual(command("RUNNING status"), "RUNNING\t1\ttest|abcdefghijklmnopqrstuvwx")
        path.write_bytes(b"0 Connected | ready\n")
        self.assertEqual(command("RUNNING status"), "RUNNING\t1\ttest|Connected | ready")
        path.unlink()
        protected = self.root / "trust/apps/test.conf"
        # A separate OEM stand-in keeps the application's profile itself valid;
        # hard-linking its actual profile correctly makes LIST fail closed.
        protected = self.root / "oem-protected"
        protected.write_bytes(b"1 protected OEM bytes\n")
        protected.chmod(0o600)
        original = protected.read_bytes()
        for kind in ("symlink", "hardlink", "fifo", "directory"):
            with self.subTest(kind=kind):
                if kind == "symlink": path.symlink_to(protected)
                elif kind == "hardlink": os.link(protected, path)
                elif kind == "fifo": os.mkfifo(path, 0o600)
                else: path.mkdir()
                self.assertEqual(command("RUNNING status"), "RUNNING\t1\ttest")
                self.assertEqual(protected.read_bytes(), original)
                if kind == "directory": path.rmdir()
                else: path.unlink()
        self.assertEqual(command("RUNNING status"), "RUNNING\t1\ttest")

    def test_status_only_declared_grants_are_exposed(self):
        for uses, optional, granted in ((None, "", False), ("show", "", False), ("", "status", True)):
            with self.subTest(uses=uses, optional=optional):
                self.traced_app(uses=uses, optional=optional)
                path = self.root / "nbt-bridge1-test.status"
                path.write_bytes(b"1 Untrusted\n")
                path.chmod(0o600)
                daemon, command = self.worker()
                self.assertEqual(command("START test"), "OK screen" if uses == "show" else "OK")
                self.wait_for(lambda: any(e.startswith("fd5=") for e in self.events()))
                self.assertIn("status=5" if granted else "status=absent", self.events())
                self.assertIn("fd5=open" if granted else "fd5=closed", self.events())
                if granted:
                    self.wait_for(lambda: show_holder(self.host_lock, 3))
                    path.write_bytes(b"2 Granted\n")
                else:
                    self.assertFalse(show_holder(self.host_lock, 3))
                self.assertEqual(command("RUNNING status"), "RUNNING\t1\ttest" + ("|Granted" if granted else ""))
                self.assertEqual(command("STOP test"), "OK")
                self.wait_for(lambda: command("STATUS").split("\t")[7] == "Application stopped")
                self.assertEqual(command("QUIT"), "OK")
                self.assertEqual(daemon.wait(timeout=10), 0)

    def test_status_test_app_publishes_and_shorter_writes_truncate(self):
        record = self.root / "state/test/installed"
        record.write_text(record.read_text().replace("NBTSTATE1\n", "NBTSTATE2\n") +
                          "bridge=1.5\nbridge-uses=show,status\nbridge-optional=\n")
        daemon, command = self.worker()
        self.assertEqual(command("START test"), "OK screen")
        self.wait_for(lambda: command("RUNNING status") == "RUNNING\t1\ttest|Hello v1")
        path = self.root / "nbt-bridge1-test.status"
        path.write_bytes(b"1 " + b"x" * 24 + b"\n")
        self.wait_for(lambda: command("RUNNING status") == "RUNNING\t1\ttest|Hello v1")
        self.assertTrue(path.read_bytes().endswith(b" Hello v1\n"))

        self.assertEqual(command("QUIT"), "OK")
        self.assertEqual(daemon.wait(timeout=10), 0)
        gui = subprocess.run([BUILD / "ui-probe", DAEMON, self.root], input=b"", stdout=subprocess.PIPE,
                             stderr=subprocess.PIPE, timeout=10)
        self.assertEqual(gui.returncode, 0, gui.stderr)
        self.assertIn(b"test  (Hello v1)", gui.stdout)

    def test_status_unsafe_leftover_starts_without_status_and_preserves_protected_file(self):
        # A status label is not worth the launch: without a safe channel the
        # application starts with neither NBT_STATUS_FD nor descriptor 5.
        self.traced_app(uses="status")
        path = self.root / "nbt-bridge1-test.status"
        protected = self.root / "oem-protected"  # the app's own profile must stay unlinked
        protected.write_bytes(b"1 protected OEM bytes\n")
        protected.chmod(0o600)
        original = protected.read_bytes()
        _, command = self.worker()
        for kind in ("symlink", "hardlink", "fifo"):
            with self.subTest(kind=kind):
                self.trace.write_text("")
                if kind == "symlink": path.symlink_to(protected)
                elif kind == "hardlink": os.link(protected, path)
                else: os.mkfifo(path, 0o600)
                self.assertEqual(command("START test"), "OK")
                self.wait_for(lambda: any(e.startswith("fd5=") for e in self.events()))
                self.assertIn("status=absent", self.events())
                self.assertIn("fd5=closed", self.events())
                self.assertFalse(show_holder(self.host_lock, 3))
                self.assertEqual(command("RUNNING status"), "RUNNING\t1\ttest")
                self.assertEqual(protected.read_bytes(), original)
                self.assertEqual(command("STOP test"), "OK")
                self.wait_for(lambda: command("RUNNING") == "RUNNING\t0")
                path.unlink()
        self.assertEqual(command("QUIT"), "OK")

    def test_show_reaches_the_same_child_after_worker_restart(self):
        self.traced_app(uses="show")
        daemon, command = self.worker()
        self.assertEqual(command("START test"), "OK screen")
        self.wait_for(lambda: show_holder(self.host_lock))
        host, application = lock_holder(self.host_lock), lock_holder(self.lease)
        self.assertEqual(show_holder(self.host_lock), host)
        self.assertIn("bridge=1.6", self.events())
        self.assertIn("caps=show", self.events())
        # The one application surface, held by this host for its life.
        self.assertIn("surface=1291", self.events())
        self.assertEqual(lock_holder(self.surface_lock), host)
        for _ in range(20):
            self.assertEqual(command("START test"), "OK screen")
        self.wait_for(lambda: "show" in self.events())
        self.assertEqual((lock_holder(self.host_lock), lock_holder(self.lease)), (host, application))
        self.assertEqual(sum(e.startswith("start ") for e in self.events()), 1)
        self.assertEqual(command("QUIT"), "OK")
        self.assertEqual(daemon.wait(timeout=10), 0)
        old_count = self.events().count("show")
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK screen")
        self.wait_for(lambda: self.events().count("show") > old_count)
        self.assertEqual((lock_holder(self.host_lock), lock_holder(self.lease)), (host, application))
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: command("RUNNING") == "RUNNING\t0")
        self.assertFalse(show_holder(self.host_lock))
        self.assertFalse(lock_holder(self.surface_lock))
        self.assertEqual(command("QUIT"), "OK")

    def test_one_application_at_a_time_has_the_surface(self):
        self.traced_app(uses="show")
        # Another application's host holds the surface.
        code = ("import fcntl, os, sys, time; "
                "fd=os.open(sys.argv[1], os.O_CREAT|os.O_RDWR, 0o600); "
                "fcntl.lockf(fd, fcntl.LOCK_EX | fcntl.LOCK_NB, 1, 0); print('ready', flush=True); time.sleep(20)")
        other = subprocess.Popen(["python3", "-c", code, self.surface_lock], stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(other.stdout.readline(), "ready\n")
            daemon, command = self.worker()
            self.assertEqual(command("START test"), "ERR screen")
            self.assertEqual(command("STATUS").split("\t")[7], "Another application has the screen")
            time.sleep(0.3)
            self.assertFalse(lock_holder(self.host_lock))
        finally:
            other.terminate()
            other.wait(timeout=5)
            other.stdout.close()
        self.assertEqual(command("START test"), "OK screen")
        self.wait_for(lambda: show_holder(self.host_lock))
        self.assertIn("surface=1291", self.events())
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: command("RUNNING") == "RUNNING\t0")
        self.assertEqual(command("QUIT"), "OK")
        self.assertEqual(daemon.wait(timeout=10), 0)

    def test_show_is_not_forwarded_without_a_grant(self):
        for uses, optional in ((None, ""), ("", ""), ("", "future")):
            with self.subTest(uses=uses, optional=optional):
                self.traced_app(uses, optional)
                daemon, command = self.worker()
                self.assertEqual(command("START test"), "OK")
                self.wait_for(lambda: lock_holder(self.lease))
                host, application = lock_holder(self.host_lock), lock_holder(self.lease)
                self.assertFalse(show_holder(self.host_lock))
                self.assertIn("surface=absent", self.events())
                self.assertFalse(lock_holder(self.surface_lock))
                self.assertEqual(command("START test"), "ERR running")
                # Even an unsolicited request to the host cannot grant show.
                # This child's default SIGUSR1 action is termination.
                os.kill(host, signal.SIGUSR1)
                time.sleep(0.3)
                self.assertEqual((lock_holder(self.host_lock), lock_holder(self.lease)), (host, application))
                self.assertNotIn("show", self.events())
                self.assertEqual(command("STOP test"), "OK")
                self.wait_for(lambda: command("RUNNING") == "RUNNING\t0")
                self.assertEqual(command("QUIT"), "OK")
                self.assertEqual(daemon.wait(timeout=10), 0)

    def test_show_waits_for_readiness_and_accepts_optional_grants(self):
        self.traced_app(uses="", optional="show", ready_delay=1.5)
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK screen")
        self.wait_for(lambda: lock_holder(self.lease))
        self.assertFalse(show_holder(self.host_lock))
        self.assertEqual(command("START test"), "ERR running")
        self.wait_for(lambda: show_holder(self.host_lock))
        self.assertEqual(command("START test"), "OK screen")
        self.wait_for(lambda: "show" in self.events())
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: command("RUNNING") == "RUNNING\t0")
        self.assertEqual(command("QUIT"), "OK")

    def test_show_is_withdrawn_before_slow_child_shutdown(self):
        self.traced_app(uses="show,status", ignore_stop=True)
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK screen")
        self.wait_for(lambda: show_holder(self.host_lock))
        self.wait_for(lambda: show_holder(self.host_lock, 3))
        path = self.root / "nbt-bridge1-test.status"
        path.write_bytes(b"1 Still alive\n")
        self.assertEqual(command("RUNNING status"), "RUNNING\t1\ttest|Still alive")
        host = lock_holder(self.host_lock)
        os.kill(host, signal.SIGTERM)
        self.wait_for(lambda: not show_holder(self.host_lock))
        self.assertFalse(show_holder(self.host_lock, 3))
        self.assertEqual(command("RUNNING status"), "RUNNING\t1\ttest")
        self.assertEqual(lock_holder(self.host_lock), host)  # child still refuses TERM
        self.assertTrue(lock_holder(self.lease))
        self.assertEqual(command("START test"), "ERR running")
        self.wait_for(lambda: not lock_holder(self.host_lock))
        self.assertFalse(lock_holder(self.lease))
        self.assertNotIn("show", self.events())
        self.assertEqual(command("QUIT"), "OK")

    def test_show_marker_must_belong_to_the_host(self):
        self.traced_app()  # no show handler in the child
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: lock_holder(self.lease))
        host, application = lock_holder(self.host_lock), lock_holder(self.lease)
        code = ("import fcntl, os, sys, time; fd=os.open(sys.argv[1], os.O_RDONLY); "
                "fcntl.lockf(fd, fcntl.LOCK_SH, 1, 1); print('ready', flush=True); time.sleep(20)")
        dummy = subprocess.Popen(["python3", "-c", code, self.host_lock], stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(dummy.stdout.readline(), "ready\n")
            self.assertEqual(show_holder(self.host_lock), dummy.pid)
            self.assertEqual(command("START test"), "ERR running")
            self.assertEqual((lock_holder(self.host_lock), lock_holder(self.lease)), (host, application))
            self.assertNotIn("show", self.events())
        finally:
            dummy.terminate()
            dummy.wait(timeout=5)
            dummy.stdout.close()
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: command("RUNNING") == "RUNNING\t0")
        self.assertEqual(command("QUIT"), "OK")

    def test_whole_file_lock_is_not_a_show_marker(self):
        self.traced_app(uses="show")
        code = ("import fcntl, os, sys, time; "
                "fd=os.open(sys.argv[1], os.O_CREAT|os.O_RDWR, 0o600); "
                "fcntl.lockf(fd, fcntl.LOCK_EX); print('ready', flush=True); time.sleep(20)")
        dummy = subprocess.Popen(["python3", "-c", code, self.host_lock], stdout=subprocess.PIPE, text=True)
        try:
            self.assertEqual(dummy.stdout.readline(), "ready\n")
            _, command = self.worker()
            self.assertEqual(command("RUNNING"), "RUNNING\t1\ttest")
            self.assertFalse(show_holder(self.host_lock))
            self.assertEqual(command("START test"), "ERR running")
            self.assertIsNone(dummy.poll())  # SIGUSR1 would have killed it
            self.assertEqual(command("QUIT"), "OK")
        finally:
            dummy.terminate()
            dummy.wait(timeout=5)
            dummy.stdout.close()
        self.assertFalse(lock_holder(self.host_lock) or show_holder(self.host_lock))

    def test_application_outlives_the_worker_and_is_stopped_by_the_next(self):
        daemon, command = self.worker()
        self.assertEqual(command("RUNNING"), "RUNNING\t0")
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: lock_holder(self.lease) and command("RUNNING") == "RUNNING\t1\ttest")
        self.assertEqual(command("START test"), "ERR running")
        self.assertEqual(command("STATUS").split("\t")[7], "Application already running")
        host = lock_holder(self.host_lock)
        application = lock_holder(self.lease)
        self.assertTrue(host and application and host != application and host != daemon.pid)
        # The worker (and with it the GUI) goes away; the application stays.
        self.assertEqual(command("QUIT"), "OK")
        self.assertEqual(daemon.wait(timeout=10), 0)
        time.sleep(1.5)
        self.assertEqual((lock_holder(self.host_lock), lock_holder(self.lease)), (host, application))
        # The next worker sees it and stops it.
        daemon, command = self.worker()
        self.assertEqual(command("RUNNING"), "RUNNING\t1\ttest")
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: command("STATUS").split("\t")[7] == "Application stopped")
        self.assertEqual(command("RUNNING"), "RUNNING\t0")
        self.assertFalse(lock_holder(self.lease) or lock_holder(self.host_lock))
        with self.assertRaises(ProcessLookupError):
            os.kill(application, 0)
        self.assertEqual(command("QUIT"), "OK")

    def test_an_incompatible_application_is_disabled_until_updated(self):
        record = self.root / "state/test/installed"
        declared = record.read_text().replace("NBTSTATE1\n", "NBTSTATE2\n")
        _, command = self.worker()
        # A capability this manager does not implement; a version older than it runs.
        for bridge in ("bridge=1.5\nbridge-uses=future\nbridge-optional=\n",
                       "bridge=1.4\nbridge-uses=\nbridge-optional=show\n"):
            with self.subTest(bridge=bridge):
                record.write_text(declared + bridge)
                self.assertEqual(command("LIST"), "APPS\t1\ttest|update")
                self.assertEqual(command("START test"), "ERR incompatible")
                self.assertEqual(command("STATUS").split("\t")[7], "Application needs an update")
                # Its page still opens, to uninstall it; it offers no live settings or files.
                self.assertEqual(command("SETTINGS test live"), "SETTINGS\t0\trestart+update")
                self.assertFalse(lock_holder(self.host_lock) or lock_holder(self.lease))
        # An update declaring what this manager runs enables it again.
        record.write_text(declared + "bridge=1.5\nbridge-uses=\nbridge-optional=\n")
        self.assertEqual(command("LIST"), "APPS\t1\ttest")
        self.assertEqual(command("SETTINGS test live"), "SETTINGS\t0\trestart")
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: lock_holder(self.lease))
        self.assertEqual(command("QUIT"), "OK")

    def test_application_ends_with_its_host(self):
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: lock_holder(self.lease))
        host = lock_holder(self.host_lock)
        os.kill(host, signal.SIGKILL)
        # Its heartbeat pipe closes, and by contract the application exits.
        self.wait_for(lambda: not lock_holder(self.lease))
        self.assertEqual(command("RUNNING"), "RUNNING\t0")
        # A second host for the same application is refused while one runs.
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: lock_holder(self.lease))
        result = subprocess.run([DAEMON, "--host", "test", "--root", self.root], timeout=10)
        self.assertEqual(result.returncode, 3)
        self.assertEqual(command("QUIT"), "OK")

    MOST_POLICY = "rx:05.00.621,rx:82.00.*,rx:E5.*.*,tx:82.00.*,tx:A2.00.*,tx:A3.00.*,ctl:E5.03,ctl:15.02"

    @staticmethod
    def most_send(request, fblock, inst, fkt, op, data=b"", dst=0, tag=0):
        return struct.pack("<BBBBHHBBHIIHH", 1, 0x81, 0, op, 0, dst, fblock, inst, fkt, tag, request,
                           len(data), 0) + data

    @staticmethod
    def most_message(fkt, data, fblock=0x05, inst=0, op=12, tx=0, variant=2, value=0):
        """A DSYSMOSTServer MessageType as the server delivers it (information 0x80000001)."""
        return (struct.pack("<IIIHHBBHB3xII", tx, variant, value, 0x1234, 0x0101, fblock, inst, fkt, op, 0, len(data))
                + data).hex()

    def most_app(self, uses, optional, policy, sends):
        """A traced app that logs every MOST record and sends `sends` once the link is up."""
        self.trace = self.root / "app-events"
        self.trace.write_text("")
        program = r"""#!/usr/bin/python3
import fcntl, os, signal, socket, struct, time
path = @LOG@
sends = @SENDS@
stopping = False
def event(text):
    with open(path, "a") as output:
        output.write(text + "\n")
def stop(number, frame):
    global stopping
    stopping = True
signal.signal(signal.SIGTERM, stop)
fcntl.lockf(4, fcntl.LOCK_SH | fcntl.LOCK_NB)
event("caps=" + os.environ.get("NBT_BRIDGE_CAPS", "absent"))
event("most=" + os.environ.get("NBT_MOST_FD", "absent"))
try:
    os.fstat(6)
    event("fd6=open")
except OSError:
    event("fd6=closed")
sock = socket.socket(fileno=6) if os.environ.get("NBT_MOST_FD") == "6" else None
if sock:
    sock.setblocking(False)
up = registry = sent = False
last = 0
while not stopping:
    now = time.monotonic()
    if now - last >= 0.5:
        os.write(3, b"R")
        last = now
    while sock:
        try:
            r = sock.recv(8192)
        except BlockingIOError:
            break
        if r[1] == 1:
            event("state %d %04X %d" % (r[4], r[6] | r[7] << 8, struct.unpack_from("<I", r, 8)[0]))
            up = r[4] == 4
        elif r[1] == 2:
            n = struct.unpack_from("<I", r, 4)[0]
            event("registry %d" % n)
            registry = True
        elif r[1] == 3:
            src, dst, fb, inst, fkt, tag, dropped, length = struct.unpack_from("<HHBBHIIH", r, 4)
            event("message %d %04X %04X %02X.%02X.%03X %d %s" % (r[2], src, dst, fb, inst, fkt, r[3], r[24:].hex()))
        elif r[1] == 4:
            request, code = struct.unpack_from("<IB", r, 4)
            event("result %d %d" % (request, code))
        else:
            event("unknown %s" % r.hex())
    if sock and up and registry and not sent:
        sent = True
        for record in sends:
            sock.send(bytes.fromhex(record))
    time.sleep(0.01)
event("exit")
""".replace("@LOG@", repr(str(self.trace))).replace("@SENDS@", repr([s.hex() for s in sends]))
        active = self.root / "apps/test/bin/app"
        active.write_text(program)
        active.chmod(0o700)
        (self.root / "state/test/installed").write_text(
            "NBTSTATE3\nsequence=00000000000000000001\nhighwater=00000000000000000001\n"
            f"version=v1\nsha256={hashlib.sha256(active.read_bytes()).hexdigest()}\n"
            f"bridge=1.5\nbridge-uses={uses}\nbridge-optional={optional}\nbridge-most={policy}\n")

    def most_environment(self, script_lines, refuse=False):
        os.environ["NBT_TEST_DSI_LIBRARY"] = str(BUILD / "libfake_dsi.so")
        os.environ["NBT_FAKE_DSI_LOG"] = str(self.root / "dsi-log")
        os.environ["NBT_FAKE_DSI_SCRIPT"] = str(self.root / "dsi-script")
        (self.root / "dsi-script").write_text("".join(line + "\n" for line in script_lines))
        if refuse:
            os.environ["NBT_FAKE_DSI_REFUSE"] = "1"
        self.addCleanup(lambda: [os.environ.pop(k, None) for k in (
            "NBT_TEST_DSI_LIBRARY", "NBT_FAKE_DSI_LOG", "NBT_FAKE_DSI_SCRIPT", "NBT_FAKE_DSI_REFUSE")])

    def dsi_log(self):
        path = self.root / "dsi-log"
        return path.read_text().splitlines() if path.exists() else []

    def test_most_messages_are_filtered_joined_and_sends_checked(self):
        registry = struct.pack("<I", 4) + struct.pack("<HHHHHHHH", 0x0101, 0x8200, 0x0102, 0xA100,
                                                      0x0005, 0xA300, 0x0103, 0x0500)
        sentinel = self.most_message(0xFFF, b"", fblock=0xE5)
        self.most_environment([
            "ID c0000001 " + registry.hex(),
            "ID c0000000 01000000",   # comm state 1 before OK: still up
            self.most_message(0x621, b"\x01\x02"),
            self.most_message(0x622, b"\x09"),                                   # not in the policy
            self.most_message(0x400, b"\xaa", fblock=0x82, tx=1, value=19),
            self.most_message(0x400, b"\xbb", fblock=0x82, tx=1, value=20),
            self.most_message(0x400, b"\xcc", fblock=0x82, tx=1, value=21),
            self.most_message(0x401, b"", fblock=0x82, value=5),                 # transmit status
            self.most_message(0x123, b"\x05", fblock=0xE5, inst=3, variant=1),   # no transmission info
            sentinel,
        ])
        sends = [
            self.most_send(1, 0x82, 0, 0x400, 1, b"\x01\x02", tag=0x01020304),
            self.most_send(2, 0x05, 0, 0x621, 1),            # rx only
            self.most_send(3, 0x82, 0, 0x400, 12),           # a reply op
            self.most_send(4, 0x82, 0, 0x400, 0, dst=0x0102),  # node without that FBlock
            self.most_send(5, 0xA2, 0, 0x003, 0),            # FBlock not on the ring
            self.most_send(6, 0xA3, 0, 0x003, 0),            # registered at a non-ring address
            self.most_send(7, 0x82, 1, 0x400, 0),            # other instance
            b"\x01\x81",                                     # malformed
            self.most_send(8, 0x82, 0, 0x400, 0, dst=0x0101),
            *[self.most_send(9 + f, 0x82, 0, f, 0) for f in range(3)],
        ]
        self.most_app("most", "", self.MOST_POLICY, sends)
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: "message 0 0101 1234 E5.00.FFF 12 " in self.events()
                      and sum(e.startswith("result ") for e in self.events()) == len(sends))
        events = self.events()
        self.assertEqual(events[:3], ["caps=most", "most=6", "fd6=open"])
        self.assertEqual(events[3], "state 0 0000 0")
        states = [e for e in events if e.startswith("state ")]
        self.assertEqual(states, ["state 0 0000 0", "state 1 0000 0", "state 4 1234 0"])
        self.assertIn("registry 4", events)
        messages = [e for e in events if e.startswith("message ")]
        self.assertEqual(messages, ["message 0 0101 1234 05.00.621 12 0102",
                                    "message 1 0101 1234 82.00.400 12 aabbcc",
                                    "message 2 0101 1234 E5.03.123 12 05",
                                    "message 0 0101 1234 E5.00.FFF 12 "])
        results = sorted(e for e in events if e.startswith("result "))
        self.assertEqual(results, ["result 0 2", "result 1 0", "result 10 1", "result 11 1", "result 2 1", "result 3 2", "result 4 4",
                                   "result 5 4", "result 6 4", "result 7 1", "result 8 0", "result 9 1"])
        log = self.dsi_log()
        # Registered under the application's name: length 12, "NBTAPP_test" and its terminator.
        self.assertIn("REQUEST f " + (struct.pack("<I", 12) + b"NBTAPP_test\0").hex(), log)
        # Device controls from the policy, in order: client 7, two keys E503 and 1502.
        self.assertIn("REQUEST 1 070000000200000003e50215", log)
        sent = [line for line in log if line.startswith("REQUEST 2 ")]
        self.assertEqual(sent, [
            "REQUEST 2 07000000" "00000000" "01000000" "00000000" "0101" "3412" "8200" "0004" "01000000"
            "04030201" "02000000" "0102",
            "REQUEST 2 07000000" "00000000" "01000000" "00000000" "0101" "3412" "8200" "0004" "00000000"
            "00000000" "00000000"])
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: "exit" in self.events())
        self.assertEqual(command("QUIT"), "OK")

    def test_most_link_follows_comm_state(self):
        sentinel = self.most_message(0xFFF, b"", fblock=0xE5)
        self.most_environment(["ID c0000000 01000000", "ID c0000000 02000000", "ID c0000000 01000000",
                               "ID c0000000 00000000", "ID c0000000 02000000", "DETACH", sentinel])
        self.most_app("most", "", self.MOST_POLICY, [])
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: any(e.endswith("E5.00.FFF 12 ") for e in self.events()))
        # 1 before OK keeps the link up; after OK, anything else is lost until OK
        # again. A detach re-registers (the fake answers only the first time).
        self.assertEqual([e for e in self.events() if e.startswith("state ")],
                         ["state 0 0000 0", "state 1 0000 0", "state 4 1234 0", "state 3 1234 0",
                          "state 4 1234 0", "state 0 0000 0", "state 1 0000 0"])
        self.assertEqual(command("QUIT"), "OK")

    def test_most_subscriptions_follow_registry_link_and_detach(self):
        registry = "ID c0000001 " + (struct.pack("<IHH", 1, 0x101, 0x500)).hex()
        status = self.most_message(0x621, b"\x00\x01")
        self.most_environment([
            "ID c0000001 00000000", "WAIT 200", registry, "WAIT 200", status,
            "ID c0000000 02000000", "ID c0000000 01000000", "WAIT 200",
            "ID c0000000 02000000", "WAIT 200", status,
            "DETACH", "ID 80000000 070000003412", registry, "WAIT 200", status,
            "ID c0000001 00000000", "WAIT 200", registry, "WAIT 200", status,
            registry, "WAIT 1200", self.most_message(0xFFF, b"", fblock=0xE5),
        ])
        self.most_app("most", "", "rx:*.*.*,sub:05.00.621,ctl:E5.03", [])
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: "message 0 0101 1234 E5.00.FFF 12 " in self.events())
        requests = [bytes.fromhex(line.split()[2]) for line in self.dsi_log()
                    if line.startswith("REQUEST 2 ")]
        self.assertEqual(len(requests), 4)
        for request in requests:
            self.assertEqual(struct.unpack_from("<I", request)[0], 7)
            self.assertEqual(struct.unpack_from("<HHBBHB", request, 16), (0x101, 0x1234, 5, 0, 1, 0))
            self.assertEqual(request[36:], b"\x01\x12\x34\x06\x21")
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: len([line for line in self.dsi_log() if line.startswith("REQUEST 2 ")]) == 5)
        clear = bytes.fromhex([line for line in self.dsi_log() if line.startswith("REQUEST 2 ")][-1].split()[2])
        self.assertEqual(clear[36:], b"\x03\x12\x34\x06\x21")
        self.assertEqual(command("QUIT"), "OK")

    def test_most_subscriptions_retry_until_status(self):
        self.most_environment([
            "ID c0000001 " + struct.pack("<IHH", 1, 0x101, 0x500).hex(),
            "WAIT 1400", self.most_message(0x621, b"\0\1"), "WAIT 2200",
            self.most_message(0xFFF, b"", fblock=0xE5),
        ])
        self.most_app("most", "", "rx:*.*.*,sub:05.00.621,ctl:E5.03", [])
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: "message 0 0101 1234 E5.00.FFF 12 " in self.events())
        self.assertEqual(sum(line.startswith("REQUEST 2 ") for line in self.dsi_log()), 2)
        self.assertEqual(command("STOP test"), "OK")
        self.assertEqual(command("QUIT"), "OK")

    def test_most_send_rate_is_limited(self):
        registry = struct.pack("<I", 1) + struct.pack("<HH", 0x0101, 0x8200)
        self.most_environment(["ID c0000001 " + registry.hex()])
        sends = [self.most_send(i, 0x82, 0, 0x400, 0) for i in range(130)]
        self.most_app("", "most", self.MOST_POLICY, sends)
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: sum(e.startswith("result ") for e in self.events()) == len(sends))
        codes = [int(e.split()[2]) for e in self.events() if e.startswith("result ")]
        self.assertGreaterEqual(codes.count(6), 1)       # busy
        self.assertLessEqual(codes.count(0), 110)        # 100 at once, then 100 per second
        self.assertEqual(codes.count(0) + codes.count(6), len(sends))
        self.assertEqual(sum(l.startswith("REQUEST 2 ") for l in self.dsi_log()), codes.count(0))
        self.assertEqual(command("QUIT"), "OK")

    def test_most_refused_registration_and_unavailable_client(self):
        self.most_environment([], refuse=True)
        self.most_app("most", "", self.MOST_POLICY, [self.most_send(1, 0x82, 0, 0x400, 0)])
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: "state 2 0000 0" in self.events())
        # Never up, so nothing is sent; the policy's controls are not registered either.
        time.sleep(0.3)
        self.assertNotIn("result 1 0", self.events())
        self.assertFalse(any(l.startswith(("REQUEST 1 ", "REQUEST 2 ")) for l in self.dsi_log()))
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: "exit" in self.events() and not lock_holder(self.host_lock))
        self.assertEqual(command("QUIT"), "OK")
        # Without a DSI library: optional `most` is not granted and fd 6 stays closed;
        # a required `most` stops the start.
        os.environ["NBT_TEST_DSI_LIBRARY"] = str(self.root / "missing.so")
        for uses, optional, started in (("", "most", True), ("most", "", False)):
            with self.subTest(uses=uses):
                self.most_app(uses, optional, self.MOST_POLICY, [])
                _, command = self.worker()
                self.assertEqual(command("START test"), "OK")
                if started:
                    self.wait_for(lambda: "fd6=closed" in self.events())
                    self.assertEqual(self.events()[:2], ["caps=", "most=absent"])
                    self.assertEqual(command("STOP test"), "OK")
                    self.wait_for(lambda: "exit" in self.events() and not lock_holder(self.host_lock))
                else:
                    time.sleep(0.5)
                    self.assertFalse(lock_holder(self.host_lock) or lock_holder(self.lease))
                    self.assertEqual(self.events(), [])
                self.assertEqual(command("QUIT"), "OK")

    def controller_app(self, uses):
        """A traced app that logs every controller state on fd 8."""
        self.trace = self.root / "app-events"
        self.trace.write_text("")
        program = r"""#!/usr/bin/python3
import fcntl, os, signal, socket, struct, time
path = @LOG@
stopping = False
def event(text):
    with open(path, "a") as output:
        output.write(text + "\n")
def stop(number, frame):
    global stopping
    stopping = True
signal.signal(signal.SIGTERM, stop)
signal.signal(signal.SIGUSR1, lambda n, f: None)
fcntl.lockf(4, fcntl.LOCK_SH | fcntl.LOCK_NB)
event("caps=" + os.environ.get("NBT_BRIDGE_CAPS", "absent"))
event("most=" + os.environ.get("NBT_MOST_FD", "absent"))
event("controller=" + os.environ.get("NBT_CONTROLLER_FD", "absent"))
sock = socket.socket(fileno=8) if os.environ.get("NBT_CONTROLLER_FD") == "8" else None
if sock:
    sock.setblocking(False)
last = 0
while not stopping:
    now = time.monotonic()
    if now - last >= 0.5:
        os.write(3, b"R")
        last = now
    while sock:
        try:
            r = sock.recv(64)
        except BlockingIOError:
            break
        seq, keys, rotation = struct.unpack_from("<IIi", r, 4)
        event("state %d %d %08x %d" % (len(r), r[2], keys, rotation))
    time.sleep(0.01)
event("exit")
""".replace("@LOG@", repr(str(self.trace)))
        active = self.root / "apps/test/bin/app"
        active.write_text(program)
        active.chmod(0o700)
        (self.root / "state/test/installed").write_text(
            "NBTSTATE2\nsequence=00000000000000000001\nhighwater=00000000000000000001\n"
            f"version=v1\nsha256={hashlib.sha256(active.read_bytes()).hexdigest()}\n"
            f"bridge=1.5\nbridge-uses={uses}\nbridge-optional=\n")

    def test_controller_states_reach_a_shown_application(self):
        knob = lambda pos, push=0: self.most_message(
            0xC46, bytes([0x4c, pos >> 8, pos & 0xff, push, 0, 0, 0, 0xff]), fblock=0xE5, inst=3)
        self.most_environment([
            "WAIT 400", knob(0x8001), knob(0x8003),
            self.most_message(0xC47, bytes([0, 0, 1, 0, 0, 0, 0]), fblock=0xE5, inst=3),
            self.most_message(0xC02, bytes([0, 0, 0, 0, 0, 0, 0, 1]), fblock=0x15, inst=0x0C),
            self.most_message(0xC01, bytes([1]), fblock=0x15, inst=0x0C),  # volume button: not ours
            self.most_message(0xC08, bytes([9]), fblock=0x20, inst=1),     # master volume: not ours
        ])
        self.controller_app("show,controller")
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK screen")
        # OPTION (slot 2) and favourite key 8, two detents clockwise, link up.
        self.wait_for(lambda: "state 16 1 00800008 2" in self.events())
        events = self.events()
        self.assertEqual(events[:3], ["caps=show,controller", "most=absent", "controller=8"])
        self.assertEqual(events[-1], "state 16 1 00800008 2")
        # One client: the E5 instances and the favourite keys as device controllers.
        controls = [line for line in self.dsi_log() if line.startswith("REQUEST 1 ")]
        self.assertEqual(controls, ["REQUEST 1 0700000008000000" "01e502e503e505e510e512e514e50c15"])
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: "exit" in self.events())
        self.assertEqual(command("QUIT"), "OK")

    def test_host_refuses_invalid_applications(self):
        for app in ("manager", "../x", "missing"):
            result = subprocess.run([DAEMON, "--host", app, "--root", self.root], timeout=10,
                                    stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
            self.assertNotEqual(result.returncode, 0, app)
        self.assertFalse((self.root / "state/missing").exists())

    def script_app(self, body):
        """Installs a Python application whose main part is `body`."""
        active = self.root / "apps/test/bin/app"
        active.write_text("#!/usr/bin/python3\nimport fcntl, os, signal, sys, time\n"
                          "fcntl.lockf(4, fcntl.LOCK_SH | fcntl.LOCK_NB)\n" + body)
        active.chmod(0o700)
        (self.root / "state/test/installed").write_text(
            "NBTSTATE1\nsequence=00000000000000000001\nhighwater=00000000000000000001\n"
            f"version=v1\nsha256={hashlib.sha256(active.read_bytes()).hexdigest()}\n")

    def app_log(self):
        path = self.root / "state/test/output.log"
        return path.read_text() if path.exists() else ""

    def files_app(self, required):
        """An application of bridge 1.5 declaring `files`, which logs what it gets."""
        self.script_app("print('files=' + os.environ.get('NBT_FILES_DIR', 'absent'), flush=True)\n"
                        "print('caps=' + os.environ.get('NBT_BRIDGE_CAPS', 'absent'), flush=True)\n"
                        "os.write(3, b'R')\n"
                        "while True:\n    time.sleep(0.5)\n    os.write(3, b'R')\n")
        record = (self.root / "state/test/installed").read_text().replace("NBTSTATE1\n", "NBTSTATE4\n")
        record += (f"bridge=1.5\nbridge-uses={'files' if required else ''}\n"
                   f"bridge-optional={'' if required else 'files'}\nbridge-files=sfc|1|2\n")
        (self.root / "state/test/installed").write_text(record)

    def test_files_reach_the_application_only_with_a_usable_disk(self):
        disk = Path(self.tmp.name) / "disk"
        disk.mkdir()
        previous = os.environ.get("NBT_TEST_FILES_ROOT")
        os.environ["NBT_TEST_FILES_ROOT"] = str(disk / "nbt-manager")
        self.addCleanup(lambda: os.environ.__setitem__("NBT_TEST_FILES_ROOT", previous)
                        if previous is not None else os.environ.pop("NBT_TEST_FILES_ROOT", None))
        self.files_app(required=True)
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK")
        folder = disk / "nbt-manager/files/test"
        self.wait_for(lambda: f"files={folder}" in self.app_log())
        self.assertIn("caps=files", self.app_log())
        self.assertEqual(oct(folder.stat().st_mode & 0o777), "0o700")
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: command("STATUS").split("\t")[7] == "Application stopped")
        # An unsafe root: a required `files` refuses the start, an optional one is not granted.
        (disk / "nbt-manager").chmod(0o755)
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: "needs files; not available on this car" in self.app_log())
        self.wait_for(lambda: not lock_holder(self.host_lock))
        self.files_app(required=False)
        self.wait_for(lambda: command("START test") == "OK")
        self.wait_for(lambda: "files=absent" in self.app_log())
        self.assertIn("caps=\n", self.app_log())  # granted nothing
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: command("STATUS").split("\t")[7] == "Application stopped")
        self.assertEqual(command("QUIT"), "OK")

    def test_bridge_16_hold_car_data_and_network(self):
        """Bridge 1.6: the hold request is relayed to the plugin and released at
        the end, the car-data names and network rules are published while the
        application runs, and its packet-filter rules are opened and closed."""
        firewall = self.root / "firewall-log"
        previous = os.environ.get("NBT_TEST_FIREWALL_LOG")
        os.environ["NBT_TEST_FIREWALL_LOG"] = str(firewall)
        self.addCleanup(lambda: os.environ.__setitem__("NBT_TEST_FIREWALL_LOG", previous)
                        if previous is not None else os.environ.pop("NBT_TEST_FIREWALL_LOG", None))
        self.script_app("for key in ('NBT_HOLD_FD', 'NBT_HOLD_STATE', 'NBT_CDS_DIR', 'NBT_NET'):\n"
                        "    print(key + '=' + os.environ.get(key, 'absent'), flush=True)\n"
                        "os.write(3, b'R')\n"
                        "os.pwrite(9, b'1 hold\\n', 0)\n"
                        "while True:\n    time.sleep(0.5)\n    os.write(3, b'R')\n")
        record = (self.root / "state/test/installed").read_text().replace("NBTSTATE1\n", "NBTSTATE5\n")
        record += ("bridge=1.6\nbridge-uses=\nbridge-optional=hold,car-data,net\n"
                   "bridge-cds=driving.gear,sensors.PDCStatus\nbridge-net=tcp:5277@uap0+wfd0\n")
        (self.root / "state/test/installed").write_text(record)
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK")
        relay = self.root / "nbt-bridge1-hmi"
        self.wait_for(lambda: relay.exists() and relay.read_text().endswith(" 1 test hold\n"))
        log = self.app_log()
        self.assertIn("NBT_HOLD_FD=9", log)
        self.assertIn(f"NBT_HOLD_STATE={self.root}/nbt-bridge1-test.hmi", log)
        self.assertIn(f"NBT_CDS_DIR={self.root}", log)
        self.assertIn("NBT_NET=tcp:5277@uap0+wfd0", log)
        want, net = self.root / "nbt-bridge1-test.want", self.root / "nbt-bridge1-test.net"
        self.wait_for(lambda: want.exists() and net.exists())
        self.assertRegex(want.read_text(), r"^\d+ driving.gear,sensors.PDCStatus\n$")
        self.assertRegex(net.read_text(), r"^\d+ tcp:5277@uap0\+wfd0\n$")
        app_rules = lambda: [l for l in firewall.read_text().splitlines() if l.startswith("app ")]
        self.assertEqual(app_rules()[:3],
                         ["app close test", "app open test uap0 5277", "app open test wfd0 5277"])
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: command("STATUS").split("\t")[7] == "Application stopped")
        self.wait_for(lambda: relay.read_text().endswith(" 1 test release\n"))
        self.assertFalse(want.exists() or net.exists())
        self.assertEqual(app_rules()[-1], "app close test")
        self.assertEqual(command("QUIT"), "OK")

    def test_output_and_a_crash_reach_the_application_log(self):
        self.script_app("print('hello from stdout', flush=True)\n"
                        "print('hello from stderr', file=sys.stderr)\n"
                        "os.write(3, b'R')\n"
                        "time.sleep(0.3)\n"
                        "os.kill(os.getpid(), signal.SIGSEGV)\n")
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: "ended by signal" in self.app_log())
        lines = self.app_log().splitlines()
        self.assertRegex(lines[0], r"^--- \d{4}-\d\d-\d\d \d\d:\d\d:\d\d started v1 \(pid \d+\)$")
        self.assertIn("hello from stdout", lines)
        self.assertIn("hello from stderr", lines)
        self.assertRegex(lines[-1], r"ended by signal 11 \(SIGSEGV, invalid memory access\)$")
        self.assertEqual((self.root / "state/test/output.log").stat().st_mode & 0o777, 0o600)
        # Exercise the producer and signed HTTP reader together, not a seeded log file.
        from phone_client import SoftwarePhone
        host = command("SESSION lo 0").split("\t")[1]
        phone = SoftwarePhone(self.tmp.name)
        phone.pair(command, host)
        self.assertEqual(phone.request(host, "POST", "/v1/logs/app", b"test\n"),
                         (200, self.app_log().encode()))
        self.assertEqual(command("QUIT"), "OK")

    def debug_log(self):
        debug = self.root / "manager/debug.log"
        (self.root / "manager").mkdir(mode=0o700, exist_ok=True)
        if not debug.exists():
            debug.touch(mode=0o600)
        return debug

    def test_an_unwritable_application_log_is_explained_in_the_debug_log(self):
        # dev74 in the car: "application log open failed (errno 89)" and
        # nothing else. A log that cannot be a file is said so, with how the
        # application ended, since that note was in the log that failed.
        debug = self.debug_log()
        log = self.root / "state/test/output.log"
        log.unlink(missing_ok=True)
        log.mkdir(mode=0o700)
        self.script_app("os.write(3, b'R')\nsys.exit(3)\n")
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: "application exited" in debug.read_text())
        text = debug.read_text()
        self.assertRegex(text, r"host test: application log \S+/state/test/output.log: checked open "
                               r"failed \(errno \d+\); not a regular file \(mode 40700\)")
        self.assertIn("host test: application log open failed", text)
        self.assertIn("host test: application exited (status 3, signal 0, fault 0)", text)
        self.assertEqual(command("QUIT"), "OK")

    def test_the_application_log_does_not_depend_on_the_checked_open(self):
        # dev74/dev75 in the car: the checked open failed on every start, for a
        # cause not established. A plain append open takes over.
        debug = self.debug_log()
        self.script_app("print('hello from the app', flush=True)\nos.write(3, b'R')\nsys.exit(0)\n")
        with mock.patch.dict(os.environ, {"NBT_TEST_APP_LOG_CHECKED_FAILS": "1"}):
            _, command = self.worker()
            self.assertEqual(command("START test"), "OK")
            self.wait_for(lambda: "exited with status 0" in self.app_log())
        self.assertIn("hello from the app", self.app_log())
        self.assertRegex(debug.read_text(), r"host test: application log \S+/output.log: checked open "
                                            r"failed \(errno \d+\); opened with a plain append open")
        self.assertEqual((self.root / "state/test/output.log").stat().st_mode & 0o777, 0o600)
        self.assertEqual(command("QUIT"), "OK")

    def test_the_application_log_without_append_mode_writes_at_the_end(self):
        # If O_APPEND is refused too, each write seeks to the end: a log
        # emptied meanwhile (the phone's Clear) gets no hole of zero bytes.
        debug = self.debug_log()
        self.script_app("print('before', flush=True)\nos.write(3, b'R')\ntime.sleep(1.5)\n"
                        "print('after', flush=True)\nsys.exit(0)\n")
        with mock.patch.dict(os.environ, {"NBT_TEST_APP_LOG_CHECKED_FAILS": "1",
                                          "NBT_TEST_APP_LOG_NO_APPEND": "1"}):
            _, command = self.worker()
            self.assertEqual(command("START test"), "OK")
            self.wait_for(lambda: "before" in self.app_log())
            os.truncate(self.root / "state/test/output.log", 0)
            self.wait_for(lambda: "exited with status 0" in self.app_log())
        data = (self.root / "state/test/output.log").read_bytes()
        self.assertNotIn(b"\0", data)
        self.assertTrue(data.startswith(b"after\n"), data)
        self.assertIn("opened with a plain write open", debug.read_text())
        self.assertEqual(command("QUIT"), "OK")

    def test_a_broken_contract_and_a_stop_are_explained(self):
        self.script_app("print('starting, but never ready', flush=True)\ntime.sleep(30)\n")
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: "stopped:" in self.app_log(), seconds=12)
        self.assertIn("starting, but never ready", self.app_log())
        self.assertIn("it did not signal ready (write 'R' to fd 3) within 5 s", self.app_log())
        self.wait_for(lambda: command("RUNNING") == "RUNNING\t0")
        # A healthy application the manager stops, then one that exits.
        self.script_app("while True:\n    os.write(3, b'R')\n    time.sleep(0.5)\n")
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: lock_holder(self.lease))
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: "stopped by the manager" in self.app_log())
        self.wait_for(lambda: command("RUNNING") == "RUNNING\t0")
        self.script_app("os.write(3, b'R')\nsys.exit(4)\n")
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: "exited with status 4" in self.app_log())
        self.assertEqual(command("QUIT"), "OK")

    def test_the_application_log_is_bounded(self):
        # 200 KiB of output: the log keeps the newest, in two files of at most 64 KiB.
        self.script_app("os.write(3, b'R')\n"
                        "for i in range(2000):\n"
                        "    sys.stdout.write('line %05d %s\\n' % (i, 'x' * 90))\n"
                        "sys.stdout.flush()\n")
        _, command = self.worker()
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: "exited with status 0" in self.app_log())
        current = self.root / "state/test/output.log"
        old = self.root / "state/test/output.log.1"
        self.assertLessEqual(current.stat().st_size, 64 * 1024)
        self.assertLessEqual(old.stat().st_size, 64 * 1024)
        self.assertIn("line 01999", current.read_text())
        self.assertNotIn("line 00000", old.read_text() + current.read_text())
        self.assertEqual(command("QUIT"), "OK")

    def test_fault_quarantine_blocks_repeat_failures_and_unblocks(self):
        # Three consecutive early crashes quarantine the application; UNBLOCK clears it.
        self.script_app("os.write(3, b'R')\n"
                        "os.kill(os.getpid(), signal.SIGSEGV)\n")
        _, command = self.worker()
        guard = self.root / "state/test/fault-guard"
        for expected in (1, 2, 3):
            self.assertEqual(command("START test"), "OK")
            self.wait_for(lambda: f"faults={expected}\n" in (guard.read_text() if guard.exists() else ""))
            self.wait_for(lambda: command("RUNNING") == "RUNNING\t0")
        self.assertEqual(command("START test"), "ERR quarantined")
        self.assertEqual(command("UNBLOCK test"), "OK")
        self.assertIn("faults=0\n", guard.read_text())
        # Forced fault-stop also increments the fault counter, while healthy running clears it.
        self.script_app("while True:\n    os.write(3, b'R')\n    time.sleep(0.5)\n")
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: lock_holder(self.lease))
        self.assertEqual(command("STOP test fault"), "OK")
        self.wait_for(lambda: command("RUNNING") == "RUNNING\t0")
        self.assertIn("faults=1\n", guard.read_text())
        self.assertEqual(command("QUIT"), "OK")


if __name__ == "__main__":
    unittest.main()
