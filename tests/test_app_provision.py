import fcntl
import hashlib
import os
from pathlib import Path
import socket
import stat
import subprocess
import sys
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build"))
LAUNCHER = BUILD / "launcher-host"
PROBE = BUILD / "ui-probe"
DAEMON = BUILD / "managerd-test"
PACK = ROOT / "tools/package.py"
# Holds a file's whole-file write lock until killed, as another process would.
HOLD_LOCK = ("import fcntl, sys, time\nf = open(sys.argv[1], 'a+b')\nfcntl.lockf(f, fcntl.LOCK_EX)\n"
             "print('locked', flush=True)\ntime.sleep(60)\n")
LAYOUT = ("apps", "data", "state", "state/manager", "trust", "trust/apps", "trust/keys", "manager")


def run(*args, **kwargs):
    return subprocess.run([str(a) for a in args], stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                          check=False, **kwargs)


def elf(content):
    out = bytearray(128)
    out[:7] = b"\x7fELF\x01\x01\x01"
    out[16:20] = b"\x02\x00\x28\x00"
    out[52:52 + len(content)] = content
    return bytes(out)


class AppFixture(unittest.TestCase):
    """A manager layout and signed packages of the application "test"."""

    @classmethod
    def setUpClass(cls):
        cls.keys = tempfile.TemporaryDirectory()
        directory = Path(cls.keys.name)
        cls.key, cls.public = directory / "key.pem", directory / "public.pem"
        cls.other, cls.other_public = directory / "other.pem", directory / "other-public.pem"
        for private, public in ((cls.key, cls.public), (cls.other, cls.other_public)):
            for args in (("openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:3072",
                          "-out", private), ("openssl", "pkey", "-in", private, "-pubout", "-out", public)):
                result = run(*args)
                if result.returncode:
                    raise RuntimeError(result.stderr.decode())
        cls.payload = elf(b"version one")
        source = directory / "source"
        source.write_bytes(cls.payload)
        cls.packages = {}
        for name, app, compat, key in (("good", "test", "demo-1", cls.key), ("other-app", "other", "demo-1", cls.key),
                                       ("other-compat", "test", "demo-2", cls.key),
                                       ("other-key", "test", "demo-1", cls.other)):
            output = directory / name
            result = run("python3", PACK, "--app", app, "--version", "v1", "--sequence", 3, "--compat", compat,
                         "--key", key, "--output", output, "--setting", "background|Background color|blue|blue,red",
                         source)
            if result.returncode:
                raise RuntimeError(result.stderr.decode())
            cls.packages[name] = output.read_bytes()

    @classmethod
    def tearDownClass(cls):
        cls.keys.cleanup()

    def setUp(self):
        self.enterContext(mock.patch.dict(os.environ, {"NBT_TEST_APPS_KEY": str(self.public)}))
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "root"
        self.root.mkdir(mode=0o700)
        for part in LAYOUT:
            (self.root / part).mkdir(mode=0o700)
        self.root.chmod(0o700)
        # Initial manager installation has already created its global lock.
        (self.root / "state/manager/operation.lock").touch(mode=0o600)
        self.env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root))

    def tearDown(self):
        self.tmp.cleanup()

    def provision(self, package=None, app="test", compat="demo-1", key=None, digest=None, data=None):
        package = self.packages["good"] if package is None else package
        key = self.public.read_bytes() if key is None else key
        digest = hashlib.sha256(package).hexdigest() if digest is None else digest
        return run(LAUNCHER, "--provision-app", app, compat, len(key), len(package), digest,
                   input=key + package if data is None else data, env=self.env, timeout=60)

    def tree(self):
        return sorted(str(p.relative_to(self.root)) for p in self.root.rglob("*"))



class AppProvisionTests(AppFixture):
    """The launcher's --provision-app: the operator's way to add an application."""

    def test_provisions_a_complete_baseline(self):
        result = self.provision()
        self.assertEqual((result.returncode, result.stdout), (0, b"PROVISIONED\n"), result.stderr)
        active = self.root / "apps/test/bin/app"
        self.assertEqual(active.read_bytes(), self.payload)
        self.assertEqual(stat.S_IMODE(active.stat().st_mode), 0o700)
        record = (self.root / "state/test/installed").read_text()
        self.assertEqual(record, "NBTSTATE1\nsequence=00000000000000000003\nhighwater=00000000000000000003\n"
                                 f"version=v1\nsha256={hashlib.sha256(self.payload).hexdigest()}\n")
        self.assertEqual((self.root / "trust/apps/test.conf").read_text(), "NBTAPP1\ncompat=demo-1\n")
        # The package's declared settings, without values yet.
        self.assertEqual((self.root / "state/test/settings.schema").read_text(),
                         "NBTSETTINGS1\nsetting=background|Background color|blue|blue,red\n")
        self.assertFalse((self.root / "state/test/settings").exists())
        self.assertEqual((self.root / "data/test/settings").read_text(), "NBTVALUES1\nbackground=blue\n")
        self.assertEqual((self.root / "data/test/settings").stat().st_mode & 0o777, 0o600)
        # No key of its own: the apps key bundled with the manager signs its updates.
        self.assertEqual(list((self.root / "trust/keys").iterdir()), [])
        for path in ("apps/test", "apps/test/bin", "state/test", "data/test"):
            self.assertEqual(stat.S_IMODE((self.root / path).stat().st_mode), 0o700)
        for path in ("state/test/installed", "state/test/settings.schema", "trust/apps/test.conf"):
            self.assertEqual(stat.S_IMODE((self.root / path).stat().st_mode), 0o600)
        # Provisioning never replaces anything.
        before = {p: (self.root / p).read_bytes() for p in ("apps/test/bin/app", "state/test/installed")}
        again = self.provision()
        self.assertEqual(again.returncode, 1)
        self.assertIn(b"already provisioned", again.stderr)
        self.assertEqual({p: (self.root / p).read_bytes() for p in before}, before)

    def test_worker_lists_and_opens_the_provisioned_application(self):
        try:
            left, right = socket.socketpair()
            left.close()
            right.close()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        self.assertEqual(self.provision().returncode, 0)
        probe = subprocess.Popen([PROBE, DAEMON, self.root], stdin=subprocess.PIPE, stdout=subprocess.PIPE, text=True)
        try:
            view = probe.stdout.readline().rstrip("\n").split("\t")
            self.assertEqual(view[1], "1", view)  # the main menu
            self.assertEqual(view[7].split("|"), ["test", "Settings"])
            probe.stdin.write("down\n"); probe.stdin.flush()
            probe.stdout.readline()
            probe.stdin.write("select\n"); probe.stdin.flush()
            view = probe.stdout.readline().split("\t")
            self.assertEqual(view[1], "2", view)  # the manager's Settings
            probe.stdin.close()
            self.assertEqual(probe.wait(timeout=10), 0)
        finally:
            if probe.poll() is None:
                probe.kill()
            probe.stdout.close()

    def test_rejected_requests_leave_nothing(self):
        layout = self.tree()
        good = self.packages["good"]
        key = self.public.read_bytes()
        cases = {
            "wrong digest": dict(digest="0" * 64),
            "other application": dict(package=self.packages["other-app"]),
            "other compatibility": dict(package=self.packages["other-compat"]),
            "argument compatibility": dict(compat="demo-2"),
            "unsigned by the key": dict(package=self.packages["other-key"]),
            "truncated stream": dict(data=(key + good)[:-10]),
            "trailing data": dict(data=key + good + b"x"),
            "manager": dict(app="manager"),
            "invalid id": dict(app="../x"),
            "corrupted package": dict(data=key + good[:-1] + bytes([good[-1] ^ 1])),
        }
        for name, arguments in cases.items():
            with self.subTest(name):
                result = self.provision(**arguments)
                self.assertEqual(result.returncode, 1, result.stdout)
                self.assertEqual(self.tree(), layout)
        for args in (["--provision-app"], ["--provision-app", "test", "demo-1", "0", "1", "0" * 64],
                     ["--provision-app", "test", "demo-1", "1", "1", "zz"]):
            self.assertEqual(run(LAUNCHER, *args, env=self.env).returncode, 2)

    def test_failure_after_writing_removes_what_was_created(self):
        if os.geteuid() == 0:
            self.skipTest("root ignores directory permissions")
        layout = self.tree()
        # The profile, written after the settings declaration, fails: both go.
        profiles = self.root / "trust/apps"
        profiles.chmod(0o500)
        try:
            result = self.provision()
        finally:
            profiles.chmod(0o700)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"cannot complete application provisioning", result.stderr)
        self.assertEqual(self.tree(), layout)
        self.assertEqual(self.provision().returncode, 0)

    def test_existing_application_data_is_preserved(self):
        data = self.root / "data/test"
        data.mkdir(mode=0o700)
        (data / "preferences").write_text("operator data")
        before = self.tree()
        result = self.provision()
        self.assertEqual(result.returncode, 1)
        self.assertEqual(self.tree(), before)
        self.assertEqual((data / "preferences").read_text(), "operator data")
        self.assertFalse((self.root / "apps/test").exists())

    def test_existing_or_missing_paths_are_refused(self):
        key = self.root / "trust/keys/test-0.pem"
        key.write_bytes(b"operator key")
        key.chmod(0o600)
        result = self.provision()
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"already provisioned", result.stderr)
        self.assertEqual(key.read_bytes(), b"operator key")
        self.assertFalse((self.root / "apps/test").exists())
        key.unlink()
        (self.root / "trust/apps").rmdir()
        result = self.provision()
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"install the manager first", result.stderr)
        self.assertFalse((self.root / "apps/test").exists())


class AppUninstallTests(AppFixture):
    """The worker's REMOVE, behind Uninstall in an application's settings."""

    def setUp(self):
        super().setUp()
        try:
            left, right = socket.socketpair()
            left.close()
            right.close()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")

    def commands(self, *lines):
        parent, child = socket.socketpair()
        daemon = subprocess.Popen([DAEMON, "--control-fd", str(child.fileno()), "--root", self.root],
                                  pass_fds=(child.fileno(),), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        child.close()
        parent.settimeout(10)
        stream = parent.makefile("rb")
        replies = []
        try:
            for line in ("HELLO 1",) + lines:
                parent.sendall(line.encode() + b"\n")
                replies.append(stream.readline().decode().rstrip("\n"))
            parent.sendall(b"QUIT\n")
            stream.readline()
        finally:
            stream.close()
            parent.close()
            daemon.communicate(timeout=10)
        return replies[1:]

    def app_paths(self):
        return [p for p in self.tree() if p.split("/")[1:2] == ["test"] or
                p in ("trust/apps/test.conf",) or p.startswith("trust/keys/test-")]

    def test_uninstall_removes_everything_of_the_application_only(self):
        outside = Path(self.tmp.name) / "outside"
        outside.mkdir()
        (outside / "keep").write_text("not the application's")
        self.assertEqual(self.provision().returncode, 0)
        other = self.provision(package=self.packages["other-app"], app="other")
        self.assertEqual(other.returncode, 0, other.stderr)
        other_paths = [p for p in self.tree() if "other" in p]
        # What an application leaves behind running and updating.
        deep = self.root / "data/test/a/b/c"
        deep.mkdir(parents=True)
        (deep / "file").write_text("data")
        (self.root / "data/test/link-to-file").symlink_to(outside / "keep")
        (self.root / "data/test/link-to-dir").symlink_to(outside)
        (self.root / "state/test/tx-00112233445566778899aabbccddeeff").mkdir(mode=0o700)
        (self.root / "state/test/tx-00112233445566778899aabbccddeeff/old").write_bytes(b"old")
        (self.root / "apps/test/bin/.candidate-00112233445566778899aabbccddeeff").write_bytes(b"new")
        for name in ("test-1.pem", "test-7.pem"):
            (self.root / "trust/keys" / name).write_bytes(b"key")
            (self.root / "trust/keys" / name).chmod(0o600)
        self.assertEqual(self.commands("LIST", "REMOVE test", "LIST", "REMOVE test"),
                         ["APPS\t2\tother\ttest", "OK", "APPS\t1\tother", "ERR missing"])
        self.assertEqual(self.app_paths(), [])
        # Links were removed, not followed; the other application is untouched.
        self.assertEqual((outside / "keep").read_text(), "not the application's")
        self.assertEqual([p for p in self.tree() if "other" in p], other_paths)
        # It can be provisioned again from scratch.
        self.assertEqual(self.provision().returncode, 0)

    def kept(self):  # the application's files, without the lock files taking a lock creates
        return [p for p in self.app_paths() if p.rsplit("/", 1)[-1] not in ("host.lock", "lease", "lock")]

    def test_uninstall_is_refused_while_running_or_busy(self):
        self.assertEqual(self.provision().returncode, 0)
        before = self.kept()
        for path, reply in (("state/test/host.lock", "ERR running"), ("state/test/lease", "ERR running"),
                            ("state/test/lock", "ERR busy")):
            with self.subTest(path), open(self.root / path, "a+b") as held:
                os.chmod(self.root / path, 0o600)
                fcntl.lockf(held, fcntl.LOCK_EX | fcntl.LOCK_NB, 1, 0)
                self.assertEqual(self.commands("REMOVE test"), [reply])
                self.assertEqual(self.kept(), before)
        # Another process with the manager operation lock (a manager update being staged).
        holder = subprocess.Popen([sys.executable, "-c", HOLD_LOCK, str(self.root / "state/manager/operation.lock")],
                                  stdout=subprocess.PIPE)
        try:
            self.assertEqual(holder.stdout.readline(), b"locked\n")
            self.assertEqual(self.commands("REMOVE test"), ["ERR busy"])
            self.assertEqual(self.kept(), before)
        finally:
            holder.kill()
            holder.communicate()
        self.assertEqual(self.commands("REMOVE manager", "REMOVE ../test", "REMOVE", "REMOVE test x"),
                         ["ERR invalid-command"] * 4)
        self.assertEqual(self.kept(), before)

    def test_uninstall_under_the_launchers_operation_lock(self):
        # The launcher holds it as long as the manager runs; here the test is
        # the worker's parent. dev35 in the car: "refused, the manager is busy".
        self.assertEqual(self.provision().returncode, 0)
        with open(self.root / "state/manager/operation.lock", "a+b") as held:
            fcntl.lockf(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.assertEqual(self.commands("REMOVE test"), ["OK"])
        self.assertEqual(self.app_paths(), [])

    def test_an_interrupted_uninstall_completes_when_run_again(self):
        self.assertEqual(self.provision().returncode, 0)
        # Cut short after the profile, the first thing removed.
        (self.root / "trust/apps/test.conf").unlink()
        self.assertEqual(self.commands("LIST", "REMOVE test"), ["APPS\t0", "OK"])
        self.assertEqual(self.app_paths(), [])
        self.assertEqual(self.provision().returncode, 0)


if __name__ == "__main__":
    unittest.main()
