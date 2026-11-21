# SPDX-License-Identifier: MPL-2.0
"""iDrive's App shortcut entries: which application each opens (SET manager
shortcut<n>) and what a press does (nbt-managerd --shortcut<n>,
src/worker/shortcut_dispatch.cpp)."""
import os
from pathlib import Path
import subprocess
import time
import unittest
from unittest import mock

import test_app_host as hosts


class ShortcutTests(unittest.TestCase):
    setUp = hosts.AppHostTests.setUp
    tearDown = hosts.AppHostTests.tearDown
    wait_for = hosts.AppHostTests.wait_for
    worker = hosts.AppHostTests.worker
    traced_app = hosts.AppHostTests.traced_app
    events = hosts.AppHostTests.events

    def record_path(self):
        return Path(self.tmp.name) / "shortcut-record"

    def press(self, slot):
        env = dict(os.environ, NBT_TEST_SHORTCUT_RECORD=str(self.record_path()))
        return subprocess.run([hosts.DAEMON, f"--shortcut{slot}", "--root", self.root], env=env,
                              stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=8)

    def answer(self):
        """The record's word and application, without its time."""
        path = self.record_path()
        return path.read_text().split(" ", 1)[1].strip() if path.exists() else None

    def flags(self, command):
        return command("SETTINGS test live").split("\t")[2].split("+")

    def test_entries_are_assigned_from_the_applications_page(self):
        _, command = self.worker()
        self.assertNotIn("shortcut1", self.flags(command))
        self.assertEqual(command("SET manager shortcut1 test"), "OK")
        self.assertIn("shortcut1", self.flags(command))
        self.assertEqual((self.root / "manager/shortcuts").read_text(), "test\n\n\n")
        # Another entry for the same application frees the first.
        self.assertEqual(command("SET manager shortcut3 test"), "OK")
        self.assertEqual((self.root / "manager/shortcuts").read_text(), "\n\ntest\n")
        self.assertEqual(self.flags(command)[1:], ["shortcut3"])
        self.assertEqual(command("SET manager shortcut3 off"), "OK")
        self.assertNotIn("shortcut3", self.flags(command))
        for line in ("SET manager shortcut4 test", "SET manager shortcut0 test",
                     "SET manager shortcut1 manager", "SET manager shortcut1 ../x", "SET manager shortcut test"):
            self.assertEqual(command(line), "ERR settings", line)
        self.assertEqual(command("QUIT"), "OK")

    def test_a_press_opens_the_manager_or_brings_the_application_back(self):
        # No application on the entry: the manager opens (to choose one).
        self.assertEqual(self.press(1).returncode, 0)
        self.assertEqual(self.answer(), "open")
        self.traced_app(uses="", optional="hold", bridge="1.6")
        daemon, command = self.worker()
        self.assertEqual(command("SET manager shortcut2 test"), "OK")
        # Not running: the manager opens and starts it (shortcut_ui.hpp).
        self.assertEqual(self.press(2).returncode, 0)
        self.assertEqual(self.answer(), "start test")
        # Running (Android Auto behind iDrive): it is asked back, nothing opens.
        self.assertEqual(command("START test"), "OK")
        self.wait_for(lambda: hosts.show_holder(self.host_lock))
        self.record_path().unlink()
        self.assertEqual(self.press(2).returncode, 0)
        self.wait_for(lambda: "show" in self.events())
        self.assertIsNone(self.answer())
        self.assertEqual(self.press(4).returncode, 2)
        self.assertEqual(command("STOP test"), "OK")
        self.wait_for(lambda: command("RUNNING") == "RUNNING\t0")
        self.assertEqual(command("QUIT"), "OK")
        self.assertEqual(daemon.wait(timeout=10), 0)

    def test_a_press_while_the_manager_is_open_goes_to_its_worker(self):
        handover = Path(self.tmp.name) / "shortcut-pending"
        with mock.patch.dict(os.environ, {"NBT_TEST_SHORTCUT_HANDOVER": str(handover),
                                          "NBT_TEST_SHORTCUT_RECORD": str(self.record_path())}):
            _, command = self.worker()
        self.assertEqual(command("SET manager shortcut1 test"), "OK")
        handover.write_text("1")
        handover.chmod(0o600)
        self.wait_for(lambda: self.answer() == "start test")
        self.assertEqual(handover.read_text(), "0")
        self.assertEqual(command("QUIT"), "OK")


if __name__ == "__main__":
    unittest.main()
