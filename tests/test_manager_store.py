import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PROBE = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build")) / "manager-store-probe"


class ManagerStoreTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "managed"
        (self.root / "state/manager").mkdir(parents=True, mode=0o700)
        for path in (self.root, self.root / "state", self.root / "state/manager"):
            path.chmod(0o700)
        self.victim = Path(self.tmp.name) / "oem"
        self.victim.write_text("unchanged")

    def tearDown(self):
        self.tmp.cleanup()

    def run_probe(self, action, sequence=1, fault=None):
        env = dict(os.environ)
        if fault:
            env["NBT_MANAGER_STATE_FAULT"] = fault
        return subprocess.run([PROBE, self.root, action, str(sequence)], env=env, capture_output=True)

    def fields(self):
        result = self.run_probe("load")
        self.assertEqual(result.returncode, 0, result.stderr)
        return dict(line.split("=", 1) for line in result.stdout.decode().splitlines()[1:])

    def test_initial_pending_and_rollback(self):
        self.assertEqual(self.run_probe("initial").returncode, 0)
        self.assertNotEqual(self.run_probe("initial").returncode, 0)
        self.assertNotEqual(self.run_probe("begin", 1).returncode, 0)
        self.assertEqual(self.run_probe("begin", 2).returncode, 0)
        state = self.fields()
        self.assertEqual((state["phase"], state["slot"], int(state["sequence"])), ("pending", "1", 2))
        self.assertEqual((state["previous_slot"], int(state["previous_sequence"])), ("0", 1))
        self.assertEqual(self.run_probe("rollback").returncode, 0)
        state = self.fields()
        self.assertEqual((state["phase"], state["slot"], int(state["sequence"]), int(state["highwater"])),
                         ("stable", "0", 1, 1))

    def test_interrupted_record_writes_preserve_committed_state(self):
        for fault in ("created", "written", "synced", "renamed", "barrier"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as directory:
                old_root = self.root
                self.root = Path(directory) / "managed"
                (self.root / "state/manager").mkdir(parents=True, mode=0o700)
                for path in (self.root, self.root / "state", self.root / "state/manager"):
                    path.chmod(0o700)
                self.assertEqual(self.run_probe("initial").returncode, 0)
                self.assertEqual(self.run_probe("begin", 2, fault).returncode, 91)
                state = self.fields()
                self.assertIn(state["phase"], ("stable", "pending"))
                if state["phase"] == "pending":
                    self.assertEqual(self.run_probe("rollback").returncode, 0)
                state = self.fields()
                self.assertEqual((state["slot"], int(state["sequence"]), int(state["highwater"])), ("0", 1, 1))
                self.assertFalse((self.root / "state/manager/activation.next").exists())
                self.root = old_root

    def test_initial_interruption_without_committed_record_fails_closed(self):
        self.assertEqual(self.run_probe("initial", fault="written").returncode, 91)
        self.assertNotEqual(self.run_probe("load").returncode, 0)
        self.assertNotEqual(self.run_probe("initial").returncode, 0)
        self.assertTrue((self.root / "state/manager/activation.next").exists())

    def test_protected_files_and_corrupt_state_are_not_modified(self):
        next_path = self.root / "state/manager/activation.next"
        next_path.symlink_to(self.victim)
        self.assertNotEqual(self.run_probe("initial").returncode, 0)
        self.assertEqual(self.victim.read_text(), "unchanged")
        next_path.unlink()
        self.assertEqual(self.run_probe("initial").returncode, 0)
        state = self.root / "state/manager/activation"
        state.write_text("corrupt")
        next_path.write_text("keep")
        self.assertNotEqual(self.run_probe("load").returncode, 0)
        self.assertEqual(next_path.read_text(), "keep")
        self.assertEqual(self.victim.read_text(), "unchanged")


if __name__ == "__main__":
    unittest.main()
