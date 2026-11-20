# SPDX-License-Identifier: MPL-2.0
import hashlib
import os
from pathlib import Path
import signal
import subprocess
import time
import unittest

import test_app_host as hosts


class AutostartTests(unittest.TestCase):
    setUp = hosts.AppHostTests.setUp
    tearDown = hosts.AppHostTests.tearDown
    wait_for = hosts.AppHostTests.wait_for
    worker = hosts.AppHostTests.worker

    def private(self, name, text):
        path = self.root / "state/test" / name
        path.write_text(text)
        path.chmod(0o600)
        return path

    def automatic(self, fault=None):
        env = dict(os.environ)
        if fault:
            env["NBT_TEST_AUTOSTART_FAULT"] = fault
        return subprocess.run([hosts.DAEMON, "--host-auto", "test", "--root", self.root], env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=8)

    def dispatch(self):
        return subprocess.run([hosts.DAEMON, "--autostart", "--root", self.root],
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=8)

    def failures(self):
        path = self.root / "state/test/autostart-guard"
        return int(path.read_text().split("failures=")[1]) if path.exists() else 0

    def test_preference_is_separate_and_guard_needs_manual_start(self):
        _, command = self.worker()

        def page_flags():  # the GUI's settings page: "restart", "live+files+autostart", ...
            return command("SETTINGS test live").split("\t")[2].split("+")

        self.assertEqual(command("AUTOSTART test"), "AUTOSTART\toff\tready")
        self.assertNotIn("autostart", page_flags())
        self.assertEqual(self.dispatch().returncode, 0)
        self.assertFalse(self.host_lock.exists())
        for value in ("on", "off", "on"):
            self.assertEqual(command("AUTOSTART test " + value), "OK")
            self.assertEqual(command("AUTOSTART test"), f"AUTOSTART\t{value}\tready")
            self.assertEqual("autostart" in page_flags(), value == "on")
        self.private("autostart-guard", "NBTAUTOGUARD1\nfailures=3\n")
        self.assertEqual(command("AUTOSTART test"), "AUTOSTART\ton\tblocked")
        flags = page_flags()
        self.assertIn("autostart-paused", flags)
        self.assertNotIn("autostart", flags)
        self.assertIn(flags[0], ("live", "restart"))
        self.assertEqual(self.automatic().returncode, 0)
        self.assertFalse(hosts.lock_holder(self.lease))
        self.assertEqual(command("AUTOSTART test off"), "OK")
        self.assertEqual(command("AUTOSTART test on"), "OK")
        self.assertEqual(self.failures(), 3)  # toggling never bypasses the guard
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: hosts.lock_holder(self.lease))
        self.assertEqual(self.failures(), 0)
        self.assertIn("autostart", page_flags())  # a start by hand resumed it
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: not hosts.lock_holder(self.host_lock))
        for line in ("AUTOSTART", "AUTOSTART manager on", "AUTOSTART ../test on",
                     "AUTOSTART missing on", "AUTOSTART test yes", "AUTOSTART test on extra"):
            self.assertEqual(command(line), "ERR invalid-command")
        self.assertEqual(command("QUIT"), "OK")

    def test_three_failed_attempts_then_skip_and_corrupt_record_fails_closed(self):
        self.private("autostart", "NBTAUTO1\nenabled=1\n")
        active = self.root / "apps/test/bin/app"
        active.write_text("#!/bin/sh\nexit 1\n")
        record = self.root / "state/test/installed"
        digest = hashlib.sha256(active.read_bytes()).hexdigest()
        text = record.read_text()
        record.write_text(text[:text.index("sha256=")] + f"sha256={digest}\n")
        for count in (1, 2, 3):
            self.automatic()
            self.assertEqual(self.failures(), count)
            self.assertFalse(hosts.lock_holder(self.host_lock))
        before = (self.root / "state/test/autostart-guard").read_bytes()
        self.assertEqual(self.automatic().returncode, 0)
        self.assertEqual((self.root / "state/test/autostart-guard").read_bytes(), before)
        self.private("autostart-guard", "NBTAUTOGUARD1\nfailures=0\nextra")
        self.assertEqual(self.automatic().returncode, 1)
        self.private("autostart-guard", "NBTAUTOGUARD1\nfailures=0\n")
        self.private("autostart", "NBTAUTO1\nenabled=01\n")
        self.assertEqual(self.automatic().returncode, 1)
        self.assertEqual(self.failures(), 0)

    def test_path_aliases_preserve_protected_files(self):
        _, command = self.worker()
        target = self.root / "oem-standin"
        target.write_bytes(b"OEM bytes")
        target.chmod(0o600)
        for name in ("autostart", "autostart.next", "autostart-guard", "autostart-guard.next"):
            for kind in ("symlink", "hardlink", "fifo", "directory"):
                with self.subTest(name=name, kind=kind):
                    path = self.root / "state/test" / name
                    path.unlink(missing_ok=True)
                    if kind == "symlink": path.symlink_to(target)
                    elif kind == "hardlink": os.link(target, path)
                    elif kind == "fifo": os.mkfifo(path, 0o600)
                    else: path.mkdir(mode=0o700)
                    if name.startswith("autostart-guard"):
                        # Manual reset is the guard writer; never touches an unsafe destination/temp.
                        if name.endswith(".next"):
                            self.private("autostart-guard", "NBTAUTOGUARD1\nfailures=3\n")
                        self.assertEqual(command("START test"), "ERR settings")
                        self.assertFalse(hosts.lock_holder(self.host_lock))
                    else:
                        self.assertEqual(command("AUTOSTART test on"), "ERR settings")
                    self.assertEqual(target.read_bytes(), b"OEM bytes")
                    if kind == "directory": path.rmdir()
                    else: path.unlink()
        self.assertEqual(command("QUIT"), "OK")

    def test_interrupted_attempt_records_never_launch_before_commit(self):
        self.private("autostart", "NBTAUTO1\nenabled=1\n")
        for step in ("created", "written", "synced", "renamed", "barrier"):
            with self.subTest(step=step):
                self.private("autostart-guard", "NBTAUTOGUARD1\nfailures=0\n")
                result = self.automatic(fault=step)
                self.assertEqual(result.returncode, 91, result.stderr)
                self.assertFalse(hosts.lock_holder(self.lease))
                self.assertEqual(self.failures(), int(step in ("renamed", "barrier")))
        # A normal attempt discards only its safe leftover .next file.
        self.private("autostart-guard", "NBTAUTOGUARD1\nfailures=3\n")
        self.assertEqual(self.automatic().returncode, 0)

    def test_dispatch_skips_an_application_disabled_until_updated(self):
        self.private("autostart", "NBTAUTO1\nenabled=1\n")
        record = self.root / "state/test/installed"
        record.write_text(record.read_text().replace("NBTSTATE1\n", "NBTSTATE2\n") +
                          "bridge=1.4\nbridge-uses=\nbridge-optional=\n")
        self.assertEqual(self.dispatch().returncode, 0)
        self.assertFalse(self.host_lock.exists())
        self.assertEqual(self.failures(), 0)  # not a failed start: autostart is not paused

    def test_host_killed_before_health_leaves_attempt_for_next_start(self):
        self.private("autostart", "NBTAUTO1\nenabled=1\n")
        self.assertEqual(self.dispatch().returncode, 0)
        self.wait_for(lambda: hosts.lock_holder(self.lease))
        os.kill(hosts.lock_holder(self.host_lock), signal.SIGKILL)
        self.wait_for(lambda: not hosts.lock_holder(self.lease))
        self.assertEqual(self.failures(), 1)
        self.assertEqual(self.dispatch().returncode, 0)
        self.wait_for(lambda: hosts.lock_holder(self.lease))
        self.assertEqual(self.failures(), 2)
        os.kill(hosts.lock_holder(self.host_lock), signal.SIGTERM)
        self.wait_for(lambda: not hosts.lock_holder(self.host_lock))

    def test_healthy_dispatch_survives_worker_and_does_not_restart_or_show(self):
        _, command = self.worker()
        self.assertEqual(command("AUTOSTART test on"), "OK")
        self.assertEqual(command("QUIT"), "OK")
        self.daemons[-1][0].wait(timeout=5)
        self.assertEqual(self.dispatch().returncode, 0)
        self.wait_for(lambda: hosts.lock_holder(self.lease))
        host, child = hosts.lock_holder(self.host_lock), hosts.lock_holder(self.lease)
        self.assertEqual(self.failures(), 1)
        self.assertEqual(self.dispatch().returncode, 0)
        time.sleep(0.3)
        self.assertEqual(hosts.lock_holder(self.host_lock), host)
        self.assertEqual(hosts.lock_holder(self.lease), child)
        self.assertEqual(self.failures(), 1)
        self.wait_for(lambda: self.failures() == 0, seconds=36)
        os.kill(host, signal.SIGTERM)
        self.wait_for(lambda: not hosts.lock_holder(self.host_lock))


if __name__ == "__main__":
    unittest.main()
