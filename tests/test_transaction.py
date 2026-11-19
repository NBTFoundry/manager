from phone_client import SoftwarePhone
import fcntl
import hashlib
import http.client
import os
import re
from pathlib import Path
import socket
import subprocess
import sys
import time
import tempfile
import unittest
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
PROBE = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build")) / "transaction-probe"
MANAGER = PROBE.parent / "manager-probe"
DAEMON = PROBE.parent / "managerd-test"
CLIENT = PROBE.parent / "client-probe"
PACK = ROOT / "tools/package.py"


def settled(command, waiting=("6",), timeout=10):
    """STATUS once the worker's job ended: a verification keeps state 6 (the
    upload received), a new application's copy state 3."""
    deadline = time.monotonic() + timeout
    while True:
        fields = command("STATUS").split("\t")
        if fields[1] not in waiting or time.monotonic() > deadline:
            return fields
        time.sleep(0.02)


def run(*args, env=None):
    return subprocess.run([str(a) for a in args], stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=False, env=env)


# Holds a file's whole-file write lock until killed, as another process would.
HOLD_LOCK = ("import fcntl, sys, time\nf = open(sys.argv[1], 'a+b')\nfcntl.lockf(f, fcntl.LOCK_EX)\n"
             "print('locked', flush=True)\ntime.sleep(60)\n")


class TransactionTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.keys = tempfile.TemporaryDirectory()
        cls.key = Path(cls.keys.name) / "key.pem"
        result = run("openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt",
                     "rsa_keygen_bits:3072", "-out", cls.key)
        if result.returncode:
            raise RuntimeError(result.stderr.decode())
        cls.public = Path(cls.keys.name) / "public.pem"
        result = run("openssl", "pkey", "-in", cls.key, "-pubout", "-out", cls.public)
        if result.returncode:
            raise RuntimeError(result.stderr.decode())

    @classmethod
    def tearDownClass(cls):
        cls.keys.cleanup()

    def setUp(self):
        # Every package is checked against the apps key bundled with the manager.
        self.enterContext(mock.patch.dict(os.environ, {"NBT_TEST_APPS_KEY": str(self.public)}))
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "managed"
        for part in ("data/test", "apps/test/bin", "state/test", "trust/apps", "trust/keys"):
            (self.root / part).mkdir(parents=True, exist_ok=True, mode=0o700)
        for path in [self.root, *self.root.rglob("*")]:
            if path.is_dir():
                path.chmod(0o700)
        self.active = self.root / "apps/test/bin/app"
        self.state = self.root / "state/test"
        self.old = self.elf(b"old version")
        self.new = self.elf(b"new version")
        self.active.write_bytes(self.old)
        self.active.chmod(0o700)
        record = ("NBTSTATE1\nsequence=00000000000000000001\n"
                  "highwater=00000000000000000001\nversion=v1\n"
                  f"sha256={hashlib.sha256(self.old).hexdigest()}\n")
        self.write_private(self.state / "installed", record.encode())
        self.write_private(self.root / "trust/apps/test.conf", b"NBTAPP1\ncompat=demo-1\n")
        (self.root / "data/test/preferences").write_bytes(b"persistent app data")
        self.make_upload(self.new, 2)

    def tearDown(self):
        self.tmp.cleanup()

    @staticmethod
    def elf(content):
        out = bytearray(128)
        out[:7] = b"\x7fELF\x01\x01\x01"
        out[16:20] = b"\x02\x00\x28\x00"
        out[52:52 + len(content)] = content
        return bytes(out)

    @staticmethod
    def write_private(path, data):
        path.write_bytes(data)
        path.chmod(0o600)

    def make_upload(self, payload, sequence, *bridge_args):
        source = Path(self.tmp.name) / "source"
        source.write_bytes(payload)
        output = self.state / "upload"
        output.unlink(missing_ok=True)
        result = run("python3", PACK, "--app", "test", "--version", f"v{sequence}",
                     "--sequence", sequence, "--compat", "demo-1", "--key", self.key,
                     "--output", output, "--setting", f"background|Background color|blue|blue,red,v{sequence}",
                     *bridge_args, source)
        self.assertEqual(result.returncode, 0, result.stderr)
        output.chmod(0o600)

    def command(self, command, fault=None):
        env = os.environ.copy()
        if fault:
            env["NBT_TEST_FAULT"] = fault
        return run(PROBE, self.root, "test", command, env=env)

    def test_bridge_dependency_rejection_precedes_executable_changes(self):
        for args, reason in ((["--bridge", "1.5"], b"needs bridge 1.5"),
                             (["--bridge", "2.0"], b"does not serve"),
                             (["--bridge", "1.0", "--bridge-uses", "future"], b"not implemented")):
            with self.subTest(args=args):
                self.make_upload(self.new, 2, *args)
                result = self.command("begin")
                self.assertEqual(result.returncode, 1, result.stderr)
                self.assertIn(reason, result.stderr)
                self.assertEqual(self.active.read_bytes(), self.old)
                self.assertFalse((self.state / "journal").exists())
                self.assertFalse(list(self.state.glob("tx-*")))

    def test_optional_gamepad_health_check_has_no_device_descriptor(self):
        self.make_upload(self.new, 2, "--bridge", "1.2", "--bridge-optional", "gamepad")
        self.assertEqual(self.command("begin").returncode, 0)
        environment = self.command("candidate-env")
        self.assertEqual(environment.returncode, 0, environment.stderr)
        self.assertIn(b"NBT_BRIDGE_CAPS=gamepad\n", environment.stdout)
        self.assertNotIn(b"NBT_GAMEPAD_FD=", environment.stdout)

    def test_bridge_grants_and_records_follow_transaction_recovery(self):
        self.make_upload(self.new, 2, "--bridge", "1.0", "--bridge-optional", "show,status")
        self.assertEqual(self.command("begin").returncode, 0)
        env = self.command("candidate-env")
        self.assertEqual(env.returncode, 0, env.stderr)
        self.assertIn(b"NBT_BRIDGE=1.4\nNBT_BRIDGE_CAPS=show,status\n", env.stdout)
        self.assertNotIn(b"NBT_STATUS_FD=", env.stdout)  # candidate has no user status channel
        self.assertNotIn(b"NBT_BRIDGE=", self.command("env").stdout)  # previous executable's record
        self.assertEqual(self.command("confirm", fault="confirmed").returncode, 91)
        self.assertEqual(self.command("recover").returncode, 0)
        expected = (self.state / "installed").read_bytes()
        self.assertTrue(expected.startswith(b"NBTSTATE2\n"))
        self.assertIn(b"bridge-optional=show,status\n", expected)
        self.assertIn(b"NBT_BRIDGE=1.4\n", self.command("env").stdout)
        # A failed candidate without the bridge must restore the declaration
        # with the executable, including after a restart in recovery itself.
        self.make_upload(self.old, 3)
        self.assertEqual(self.command("begin", fault="installed-rename").returncode, 91)
        self.assertEqual(self.command("recover", fault="recovery-restored").returncode, 91)
        self.assertEqual(self.command("recover").returncode, 0)
        self.assertEqual((self.state / "installed").read_bytes(), expected)
        self.assertEqual(self.active.read_bytes(), self.new)
        # A confirmed update may deliberately remove the dependency.
        self.assertEqual(self.command("begin").returncode, 0)
        self.assertNotIn(b"NBT_BRIDGE=", self.command("candidate-env").stdout)
        self.assertEqual(self.command("confirm").returncode, 0)
        self.assertTrue((self.state / "installed").read_bytes().startswith(b"NBTSTATE1\n"))

    def test_most_policy_is_signed_journaled_and_recorded(self):
        policy = "rx:05.00.621,sub:05.00.621,rx:E5.*.*,tx:82.00.*,ctl:E5.03"
        # The packager refuses a policy without `most`, `most` without a policy, and bad rules.
        for args in ((["--bridge", "1.1", "--bridge-most", policy]),
                     (["--bridge", "1.1", "--bridge-uses", "most"]),
                     (["--bridge", "1.1", "--bridge-uses", "most", "--bridge-most", "tx:*.*.*"]),
                     (["--bridge-most", policy])):
            with self.subTest(args=args):
                source = Path(self.tmp.name) / "source"
                source.write_bytes(self.new)
                result = run("python3", PACK, "--app", "test", "--version", "v9", "--sequence", 9, "--compat",
                             "demo-1", "--key", self.key, "--output", Path(self.tmp.name) / "refused", *args, source)
                self.assertNotEqual(result.returncode, 0)
                self.assertFalse((Path(self.tmp.name) / "refused").exists())
        self.make_upload(self.new, 2, "--bridge", "1.2", "--bridge-optional", "most", "--bridge-most", policy)
        self.assertEqual(self.command("begin").returncode, 0)
        self.assertTrue((self.state / "journal").read_bytes().startswith(b"NBTJ3\n"))
        self.assertIn(f"new_bridge-most={policy}\n".encode(), (self.state / "journal").read_bytes())
        env = self.command("candidate-env").stdout
        self.assertIn(b"NBT_BRIDGE_CAPS=most\n", env)
        self.assertNotIn(b"NBT_MOST_FD=", env)  # health checks get no MOST socket
        self.assertEqual(self.command("confirm").returncode, 0)
        record = (self.state / "installed").read_bytes()
        self.assertTrue(record.startswith(b"NBTSTATE3\n"))
        self.assertTrue(record.endswith(f"bridge-optional=most\nbridge-most={policy}\n".encode()))
        # Each version is used exactly when needed: a policy under NBTSTATE2, or
        # NBTSTATE3 without one, is not a record.
        for text in (record.replace(b"NBTSTATE3\n", b"NBTSTATE2\n"),
                     record.split(b"bridge-most=")[0].replace(b"bridge-optional=most", b"bridge-optional=")):
            with self.subTest(text=text[:10]):
                self.write_private(self.state / "installed", text)
                self.assertNotEqual(self.command("env").returncode, 0)
        self.write_private(self.state / "installed", record)
        # Removing the dependency returns to NBTSTATE1 through an NBTJ3 journal.
        self.make_upload(self.old, 3)
        self.assertEqual(self.command("begin").returncode, 0)
        self.assertTrue((self.state / "journal").read_bytes().startswith(b"NBTJ3\n"))
        self.assertEqual(self.command("confirm").returncode, 0)
        self.assertTrue((self.state / "installed").read_bytes().startswith(b"NBTSTATE1\n"))

    def test_begin_then_confirm_and_replay(self):
        result = self.command("begin")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(self.active.read_bytes(), self.new)
        result = self.command("confirm")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"sequence=00000000000000000002", (self.state / "installed").read_bytes())
        self.assertEqual(len(list(self.state.glob("tx-*/old"))), 1)
        self.assertEqual(self.command("begin").returncode, 1)

    def test_failure_after_each_step_restores_old(self):
        for i, step in enumerate(("candidate", "prepared", "backed-up-rename", "backed-up",
                                  "installed-rename", "installed")):
            with self.subTest(step=step):
                if i:
                    self.tearDown()
                    self.setUp()
                result = self.command("begin", fault=step)
                self.assertEqual(result.returncode, 91, result.stderr)
                result = self.command("recover")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.active.read_bytes(), self.old)
                self.assertIn(b"sequence=00000000000000000001", (self.state / "installed").read_bytes())

    def test_confirm_crash_completes_new_version(self):
        for i, step in enumerate(("confirmed", "recorded")):
            with self.subTest(step=step):
                if i:
                    self.tearDown()
                    self.setUp()
                self.assertEqual(self.command("begin").returncode, 0)
                self.assertEqual(self.command("confirm", fault=step).returncode, 91)
                result = self.command("recover")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.active.read_bytes(), self.new)
                self.assertIn(b"sequence=00000000000000000002", (self.state / "installed").read_bytes())

    def test_recovery_refuses_unexpected_active_bytes(self):
        self.assertEqual(self.command("begin").returncode, 0)
        self.active.write_bytes(b"unexpected")
        self.assertEqual(self.command("recover").returncode, 1)
        self.assertEqual(self.active.read_bytes(), b"unexpected")
        self.assertEqual(len(list(self.state.glob("tx-*/old"))), 1)

    def test_repeated_failed_updates_preserve_known_good_backup(self):
        self.assertEqual(self.command("begin").returncode, 0)
        self.assertEqual(self.command("confirm").returncode, 0)
        original_backup = next(self.state.glob("tx-*/old"))
        self.assertEqual(original_backup.read_bytes(), self.old)
        third = self.elf(b"third version")
        for _ in range(3):
            self.make_upload(third, 3)
            self.assertEqual(self.command("begin", fault="installed").returncode, 91)
            self.assertEqual(self.command("recover").returncode, 0)
            self.assertEqual(self.active.read_bytes(), self.new)
            self.assertEqual(original_backup.read_bytes(), self.old)
            self.assertIn(b"sequence=00000000000000000002", (self.state / "installed").read_bytes())

    def test_corrupt_journal_cannot_introduce_destination(self):
        self.assertEqual(self.command("begin").returncode, 0)
        journal = self.state / "journal"
        data = journal.read_bytes()
        tx = next(line[3:] for line in data.splitlines() if line.startswith(b"tx="))
        journal.write_bytes(data.replace(b"tx=" + tx, b"tx=../../outside"))
        sentinel = Path(self.tmp.name) / "outside"
        sentinel.write_bytes(b"untouched")
        self.assertEqual(self.command("recover").returncode, 1)
        self.assertEqual(sentinel.read_bytes(), b"untouched")
        self.assertEqual(self.active.read_bytes(), self.new)

    def test_recovery_can_be_interrupted_and_repeated(self):
        for index, step in enumerate(("recovery-restored", "recovery-cleaned", "recovery-finished")):
            with self.subTest(step=step):
                if index:
                    self.tearDown()
                    self.setUp()
                self.assertEqual(self.command("begin").returncode, 0)
                self.assertEqual(self.command("recover", fault=step).returncode, 91)
                result = self.command("recover")
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.active.read_bytes(), self.old)
                self.assertIn(b"sequence=00000000000000000001", (self.state / "installed").read_bytes())

    def test_downgrade_requires_recovery_and_retains_highwater(self):
        self.assertEqual(self.command("begin").returncode, 0)
        self.assertEqual(self.command("confirm").returncode, 0)
        self.make_upload(self.old, 1)
        self.assertEqual(self.command("begin").returncode, 1)
        self.assertEqual(self.command("downgrade").returncode, 0)
        self.assertEqual(self.command("confirm").returncode, 0)
        self.assertEqual(self.active.read_bytes(), self.old)
        self.assertIn(b"highwater=00000000000000000002", (self.state / "installed").read_bytes())
        self.make_upload(self.new, 2)
        self.assertEqual(self.command("begin").returncode, 1)

    def test_manager_approval_health_and_running_state(self):
        for i, scenario in enumerate(("healthy", "running", "fail", "running-fail", "wrong-digest", "stop",
                                      "host-stuck", "reap-error", "stopping-approve", "watch", "watch-cancel",
                                      "watch-expire", "watch-unopened")):
            with self.subTest(scenario=scenario):
                if i:
                    self.tearDown()
                    self.setUp()
                package = Path(self.tmp.name) / "package"
                (self.state / "upload").rename(package)
                result = run(MANAGER, self.root, package, scenario)
                self.assertEqual(result.returncode, 0, result.stderr)
                expected = self.old if scenario in ("fail", "running-fail", "wrong-digest", "stop", "host-stuck",
                                                    "watch-cancel", "watch-expire", "watch-unopened") else self.new
                self.assertEqual(self.active.read_bytes(), expected)
                self.assertEqual((self.root / "data/test/preferences").read_bytes(), b"persistent app data")
                # A confirmed installation replaces the declared settings; any
                # other outcome keeps the previous declaration (none here).
                schema = self.state / "settings.schema"
                if expected == self.new:
                    self.assertEqual(schema.read_text(),
                                     "NBTSETTINGS1\nsetting=background|Background color|blue|blue,red,v2\n")
                    self.assertEqual(schema.stat().st_mode & 0o777, 0o600)
                else:
                    self.assertFalse(schema.exists())
                self.assertFalse((self.state / "settings.schema.next").exists())

    def test_daemon_browser_session_and_in_car_approval_boundary(self):
        self.daemon_install("Update failed; previous application restored")

    def test_install_under_the_launchers_operation_lock(self):
        # The launcher holds it as long as the manager runs; here the test is
        # the worker's parent. dev35 in the car: "cannot install verified package".
        (self.root / "state/manager").mkdir(mode=0o700)
        lock = self.root / "state/manager/operation.lock"
        with open(lock, "a+b") as held:
            lock.chmod(0o600)
            fcntl.lockf(held, fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.daemon_install("Update failed; previous application restored")

    def test_install_waits_for_another_holder_of_the_operation_lock(self):
        (self.root / "state/manager").mkdir(mode=0o700)
        lock = self.root / "state/manager/operation.lock"
        lock.touch(mode=0o600)
        holder = subprocess.Popen([sys.executable, "-c", HOLD_LOCK, str(lock)], stdout=subprocess.PIPE)
        try:
            self.assertEqual(holder.stdout.readline(), b"locked\n")
            self.daemon_install("cannot install verified package")
        finally:
            holder.kill()
            holder.communicate()

    def daemon_install(self, outcome):
        (self.root / "state/manager").mkdir(mode=0o700, exist_ok=True)
        package = (self.state / "upload").read_bytes()
        parent, child = socket.socketpair()
        try:
            parent.sendall(b"x")
            child.recv(1)
        except PermissionError:
            parent.close()
            child.close()
            self.skipTest("sandbox blocks local socket traffic")
        parent.settimeout(5)
        daemon = subprocess.Popen([DAEMON, "--control-fd", str(child.fileno()), "--root", self.root],
                                  pass_fds=(child.fileno(),), stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        child.close()
        stream = parent.makefile("rb")

        def command(line):
            parent.sendall(line.encode() + b"\n")
            return stream.readline().decode().rstrip("\n")

        try:
            self.assertEqual(command("STATUS"), "ERR protocol-version")
            self.assertEqual(command("HELLO 2"), "ERR protocol-version")
            self.assertEqual(command("HELLO 1"), "HELLO\t1")
            fields = command("SESSION test lo 0").split("\t")
            self.assertEqual(fields[0], "SESSION")
            host = fields[1]
            phone = SoftwarePhone(self.tmp.name)
            phone.pair(command, host)
            self.assertEqual(phone.http(host, "POST", "/upload", package)[0], 400)
            self.assertEqual(phone.request(host, "POST", "/v1/upload", package, bad_signature=True)[0], 403)
            status, body = phone.request(host, "POST", "/v1/upload", package)
            self.assertEqual((status, body), (202, b"STATUS\twaiting\tChecking the package\n"))
            fields = settled(command)
            self.assertEqual(fields[0:5], ["STATUS", "2", "test", "v1", "v2"])
            self.assertEqual(fields[6], hashlib.sha256(package).hexdigest())
            self.assertEqual(phone.http(host, "POST", "/v1/approve", b"")[0], 400)
            self.assertEqual(command("APPROVE " + "0" * 64 + " normal"), "ERR install")
            self.assertEqual(self.active.read_bytes(), self.old)
            # The fixture is an ARM ELF, so a Linux host cannot run it. Approval
            # returns at once; the failed candidate start restores the previous
            # executable and is reported through STATUS.
            self.assertEqual(command("APPROVE " + fields[6] + " normal"), "OK installing")
            self.assertEqual(command("APPROVE " + fields[6] + " normal"), "ERR install")
            fields = settled(command, ("3",))  # the copy runs on the worker's job
            self.assertEqual(fields[1], "5", fields)
            self.assertEqual(fields[7], outcome)
            self.assertEqual(self.active.read_bytes(), self.old)
            if outcome.endswith("previous application restored"):
                # The health check's run is in the application's log, as a host's is.
                log = (self.state / "output.log").read_text()
                self.assertIn(" install check of v2", log)
                self.assertRegex(log, r"install check (failed|of v2: cannot start)")
            else:  # refused before any check
                self.assertFalse((self.state / "output.log").exists())
            self.assertEqual(command("QUIT"), "OK")
            _, stderr = daemon.communicate(timeout=5)
            self.assertEqual(daemon.returncode, 0, stderr)
        finally:
            stream.close()
            parent.close()
            if daemon.poll() is None:
                daemon.kill()
                daemon.communicate()

    def package_for(self, app, sequence=2, key=None):
        source = Path(self.tmp.name) / "source"
        source.write_bytes(self.new)
        output = Path(self.tmp.name) / f"{app}-{sequence}.nbt"
        output.unlink(missing_ok=True)
        result = run("python3", PACK, "--app", app, "--version", f"v{sequence}", "--sequence", sequence,
                     "--compat", "demo-1", "--key", key or self.key, "--output", output, source)
        self.assertEqual(result.returncode, 0, result.stderr)
        return output.read_bytes()

    def test_generic_session_takes_the_application_from_the_package(self):
        (self.root / "state/manager").mkdir(mode=0o700)
        (self.state / "upload").unlink()
        staging = self.root / "state/manager/session.upload"
        parent, child = socket.socketpair()
        try:
            parent.sendall(b"x")
            child.recv(1)
        except PermissionError:
            parent.close()
            child.close()
            self.skipTest("sandbox blocks local socket traffic")
        parent.settimeout(5)
        firewall = Path(self.tmp.name) / "firewall.log"
        daemon = subprocess.Popen([DAEMON, "--control-fd", str(child.fileno()), "--root", self.root],
                                  pass_fds=(child.fileno(),), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  env=dict(os.environ, NBT_TEST_FIREWALL_LOG=str(firewall)))
        child.close()
        stream = parent.makefile("rb")

        def command(line):
            parent.sendall(line.encode() + b"\n")
            return stream.readline().decode().rstrip("\n")

        phone = SoftwarePhone(self.tmp.name)
        paired = False
        def upload(body):
            nonlocal paired
            fields = command("SESSION lo 0").split("\t")
            self.assertEqual(fields[0], "SESSION", fields)
            host = fields[1]
            if not paired:
                phone.pair(command, host)
                paired = True
            return phone.request(host, "POST", "/v1/upload", body)

        try:
            self.assertEqual(command("HELLO 1"), "HELLO\t1")
            # A new application not signed with the apps key: refused, nothing kept.
            forger = Path(self.tmp.name) / "forger.pem"
            self.assertEqual(run("openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:3072",
                                 "-out", forger).returncode, 0)
            # Verified on the worker's job: accepted for checking, then refused.
            self.assertEqual(upload(self.package_for("forged", key=forger))[0], 202)
            fields = settled(command)
            self.assertEqual(fields[1], "5")
            self.assertTrue(fields[7].startswith("Package rejected: "), fields)
            self.assertFalse(staging.exists())
            self.assertFalse((self.root / "state/forged").exists())
            # A new application signed with the apps key: verified, nothing
            # installed until it is approved, then installed from scratch.
            other = self.package_for("other")
            self.assertEqual(upload(other), (202, b"STATUS\twaiting\tChecking the package\n"))
            fields = settled(command)
            self.assertEqual(fields[0:5], ["STATUS", "2", "other", "-", "v2"])
            self.assertTrue(staging.exists())
            self.assertFalse((self.root / "state/other").exists())
            self.assertEqual(command("APPROVE " + fields[6] + " normal"), "OK installing")
            fields = settled(command, ("3",))
            self.assertEqual(fields[1:4] + fields[7:], ["4", "other", "v2", "Application installed"])
            self.assertEqual(command("LIST"), "APPS\t2\tother\ttest")
            self.assertEqual((self.root / "apps/other/bin/app").read_bytes(), self.new)
            self.assertEqual((self.root / "trust/apps/other.conf").read_text(), "NBTAPP1\ncompat=demo-1\n")
            self.assertFalse(staging.exists())
            # Not a package at all.
            self.assertEqual(upload(b"x" * 600)[0], 400)
            self.assertEqual(command("STATUS").split("\t")[7], "Package rejected: not an NBT Manager package")
            self.assertFalse(staging.exists())
            # The manager itself is never a session application.
            self.assertEqual(upload(self.package_for("manager"))[0], 400)
            self.assertFalse(staging.exists())
            # The installed version again: refused before the car's review,
            # naming both versions (dev71 in the car: "verification fails").
            self.assertEqual(upload(self.package_for("test", sequence=1))[0], 202)
            self.assertEqual(settled(command)[7],
                             "Package rejected: version v1 is not newer than the installed v1")
            self.assertFalse((self.state / "upload").exists())
            # A package for the provisioned application selects it and verifies.
            package = self.package_for("test")
            self.assertEqual(upload(package), (202, b"STATUS\twaiting\tChecking the package\n"))
            fields = settled(command)
            self.assertEqual(fields[0:5], ["STATUS", "2", "test", "v1", "v2"])
            self.assertEqual(fields[6], hashlib.sha256(package).hexdigest())
            self.assertFalse(staging.exists())
            self.assertTrue((self.state / "upload").exists())
            # Cancel discards it; an app-bound SESSION still works.
            self.assertEqual(command("CANCEL"), "OK")
            self.assertFalse((self.state / "upload").exists())
            self.assertEqual(command("SESSION test lo 0").split("\t")[0], "SESSION")
            self.assertEqual(command("STATUS").split("\t")[1:3], ["1", "test"])
            self.assertEqual(command("SESSION lo"), "ERR invalid-command")
            self.assertEqual(command("QUIT"), "OK")
            _, stderr = daemon.communicate(timeout=5)
            self.assertEqual(daemon.returncode, 0, stderr)
            # The packet filter rule exists only while a session listens: one
            # cleanup at start, then each open is followed by its removal.
            calls = firewall.read_text().splitlines()
            self.assertEqual(calls[0], "close", calls)
            opens, closes = calls[1::2], calls[2::2]
            self.assertEqual(len(opens), 7, calls)  # seven sessions above
            self.assertTrue(all(re.fullmatch(r"open lo 127\.0\.0\.1 [1-9][0-9]*", line) for line in opens), calls)
            self.assertEqual(closes, ["close"] * len(opens), calls)
        finally:
            stream.close()
            parent.close()
            if daemon.poll() is None:
                daemon.kill()
                daemon.communicate()

    def test_the_worker_answers_while_a_package_is_verified(self):
        # The GUI pings the worker every second with a 2 s timeout: verification
        # and copies run on the worker's job, and the loop answers meanwhile.
        (self.root / "state/manager").mkdir(mode=0o700)
        (self.state / "upload").unlink()
        (self.root / "manager").mkdir(mode=0o700, exist_ok=True)
        (self.root / "manager/debug.log").touch(mode=0o600)  # the debug log is on
        parent, child = socket.socketpair()
        try:
            parent.sendall(b"x")
            child.recv(1)
        except PermissionError:
            parent.close()
            child.close()
            self.skipTest("sandbox blocks local socket traffic")
        parent.settimeout(5)
        daemon = subprocess.Popen([DAEMON, "--control-fd", str(child.fileno()), "--root", self.root],
                                  pass_fds=(child.fileno(),), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                  env=dict(os.environ, NBT_TEST_TASK_DELAY_MS="1500"))
        child.close()
        stream = parent.makefile("rb")

        def command(line):
            parent.sendall(line.encode() + b"\n")
            return stream.readline().decode().rstrip("\n")

        try:
            self.assertEqual(command("HELLO 1"), "HELLO\t1")
            host = command("SESSION lo 0").split("\t")[1]
            phone = SoftwarePhone(self.tmp.name)
            phone.pair(command, host)
            started = time.monotonic()
            self.assertEqual(phone.request(host, "POST", "/v1/upload", self.package_for("test")),
                             (202, b"STATUS\twaiting\tChecking the package\n"))
            fields = command("STATUS").split("\t")
            self.assertEqual(fields[1:3] + fields[7:], ["6", "test", "Checking the package"])
            self.assertEqual(command("LIST"), "APPS\t1\ttest")
            self.assertEqual(command("SESSION lo 0"), "ERR session")
            self.assertLess(time.monotonic() - started, 1.0)
            fields = settled(command)
            self.assertEqual(fields[0:5], ["STATUS", "2", "test", "v1", "v2"])
            self.assertGreaterEqual(time.monotonic() - started, 1.5)
            # A CANCEL during a verification applies once it ends.
            host = command("SESSION lo 0").split("\t")[1]
            self.assertEqual(phone.request(host, "POST", "/v1/upload", self.package_for("test"))[0], 202)
            self.assertEqual(command("CANCEL"), "OK")
            fields = settled(command)
            self.assertEqual([fields[1], fields[7]], ["0", "Session closed"])
            self.assertFalse((self.state / "upload").exists())
            self.assertEqual(command("QUIT"), "OK")
            _, stderr = daemon.communicate(timeout=5)
            self.assertEqual(daemon.returncode, 0, stderr)
            debug = (self.root / "manager/debug.log").read_text()
            self.assertRegex(debug, r"verification test: 1[5-9][0-9]{2} ms")
            # Where the root really is (the car's mount table shows no /var).
            self.assertRegex(debug, r"storage \S+: \S+ on \S+ at \S+, real path /\S+; "
                                    r"\d+ MiB free of \d+ MiB; /var is a ")
        finally:
            stream.close()
            parent.close()
            if daemon.poll() is None:
                daemon.kill()
                daemon.communicate()

    def test_private_client_spawns_and_controls_daemon(self):
        (self.root / "state/manager").mkdir(mode=0o700)
        left, right = socket.socketpair()
        try:
            left.sendall(b"x")
            right.recv(1)
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        finally:
            left.close()
            right.close()
        result = run(CLIENT, DAEMON, self.root)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_gui_keeps_its_heartbeat_while_the_worker_is_slow(self):
        # dev62 in the car: Pair a phone closed the manager. The worker took
        # several seconds to prepare Wi-Fi Direct for SESSION, the GUI waited
        # without beating, and the launcher stops a GUI silent for 3 s. The
        # request now runs on the GUI's worker-channel thread.
        result = run(CLIENT, "keepalive")
        self.assertEqual(result.returncode, 0, result.stderr)


if __name__ == "__main__":
    unittest.main()
