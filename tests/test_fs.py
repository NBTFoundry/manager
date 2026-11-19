import os
from pathlib import Path
import subprocess
import tempfile
import unittest


PROBE = Path(os.environ.get("NBT_BUILD_DIR", Path(__file__).resolve().parents[1] / "build")) / "fs-probe"


class PathPolicyTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "managed"
        for path in ("apps/test/bin", "state/test", "trust/apps", "trust/keys"):
            (self.root / path).mkdir(parents=True, exist_ok=True, mode=0o700)
        for path in [self.root, *self.root.rglob("*")]:
            if path.is_dir():
                path.chmod(0o700)
        self.victim = Path(self.tmp.name) / "oem-file"
        self.victim.write_text("do not touch")

    def tearDown(self):
        self.tmp.cleanup()

    def probe(self, command, app="test", tx="0" * 32):
        return subprocess.run([PROBE, self.root, command, app, tx], capture_output=True).returncode

    def test_create_is_constrained_and_exclusive(self):
        self.assertEqual(self.probe("create"), 0)
        self.assertEqual((self.root / "state/test/upload").read_text(), "safe")
        self.assertEqual(self.probe("create"), 1)
        self.assertEqual(self.probe("forbidden"), 0)
        self.assertEqual(self.probe("path", "../../etc"), 1)
        self.assertEqual(self.probe("tx", tx="../outside"), 1)
        self.assertEqual(self.victim.read_text(), "do not touch")

    def test_rejects_symlink_at_target_and_parent(self):
        upload = self.root / "state/test/upload"
        upload.symlink_to(self.victim)
        self.assertEqual(self.probe("create"), 1)
        self.assertEqual(self.victim.read_text(), "do not touch")
        app = self.root / "apps/test/bin"
        app.rmdir()
        app.symlink_to(self.victim.parent, target_is_directory=True)
        self.assertEqual(self.probe("path"), 1)
        self.assertEqual(self.probe("read"), 1)

    def test_rejects_hardlinks_and_untrusted_directories(self):
        active = self.root / "apps/test/bin/app"
        os.link(self.victim, active)
        self.assertEqual(self.probe("read"), 1)
        active.unlink()
        active.write_text("managed")
        active.chmod(0o600)
        self.assertEqual(self.probe("read"), 0)
        (self.root / "apps/test").chmod(0o777)
        self.assertEqual(self.probe("read"), 1)
        self.assertEqual(self.victim.read_text(), "do not touch")

    def test_setup_layout_is_idempotent_and_creation_is_exclusive(self):
        self.assertEqual(self.probe("setup-layout"), 0)
        self.assertEqual(self.probe("setup-layout"), 0)
        for directory in ("apps", "data", "state/manager", "trust/apps", "trust/keys", "manager/slots/0", "manager/slots/1"):
            self.assertTrue((self.root / directory).is_dir())
        self.assertEqual(self.probe("setup-forbidden"), 0)
        self.assertEqual(self.probe("setup-key"), 0)
        self.assertEqual(self.probe("setup-key"), 1)
        self.assertEqual((self.root / "trust/manager.pem").read_text(), "key")
        self.assertEqual(self.victim.read_text(), "do not touch")

    def test_setup_rejects_conflicting_directory_before_creating_files(self):
        (self.root / "manager").symlink_to(self.victim.parent, target_is_directory=True)
        self.assertEqual(self.probe("setup-layout"), 1)
        self.assertFalse((self.root / "data").exists())
        self.assertFalse((self.root / "state/manager").exists())
        self.assertEqual(self.victim.read_text(), "do not touch")

    def test_root_must_be_private_and_not_symlink(self):
        self.root.chmod(0o755)
        self.assertEqual(self.probe("path"), 2)
        self.root.chmod(0o700)
        link = self.root.parent / "link"
        link.symlink_to(self.root, target_is_directory=True)
        result = subprocess.run([PROBE, link, "path", "test", "0" * 32], capture_output=True)
        self.assertEqual(result.returncode, 2)


if __name__ == "__main__":
    unittest.main()
