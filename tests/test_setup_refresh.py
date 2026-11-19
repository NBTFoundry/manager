# SPDX-License-Identifier: MPL-2.0
"""SETUP: the stable launcher and the iDrive plugin follow the confirmed manager
release (src/launcher/setup_refresh.cpp, src/storage/hmi_plugin_swap.cpp)."""
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build"))
REFRESH = BUILD / "manager-refresh-probe"
SETUP = BUILD / "manager-setup-probe"
PLUGIN = BUILD / "manager-plugin-probe"
STAGE = BUILD / "manager-stage-probe"
LAUNCHER = BUILD / "launcher-host"
PACK = ROOT / "tools/package_release.py"
NAMES = ("libNbtManager.so", "NbtManager.manifest", "NbtManager.xml", "NbtManager_ALL_Texts.zip",
         "NbtManager_ALL_Images.zip")


def run(*args, env=None):
    return subprocess.run([str(arg) for arg in args], env=env, capture_output=True, check=False)


class SetupRefreshTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assets = tempfile.TemporaryDirectory()
        cls.path = Path(cls.assets.name)
        key = cls.path / "manager.pem"
        for args in (("genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:3072", "-out", key),
                     ("pkey", "-in", key, "-pubout", "-out", cls.path / "manager.pub")):
            result = run("openssl", *args)
            if result.returncode:
                raise RuntimeError(result.stderr.decode())
        # Release 1 is what an older kit installed; release 4 changes the launcher
        # and every plugin file; release 5 changes neither (a GUI/worker update).
        for sequence, generation in ((1, 1), (4, 2), (5, 2)):
            elf = bytearray(128)
            elf[:7] = b"\x7fELF\x01\x01\x01"
            elf[16:20] = b"\x02\x00\x28\x00"
            elf[-2] = generation
            folder = cls.path / f"parts-{sequence}"
            plugin = folder / "plugin"
            plugin.mkdir(parents=True)
            (folder / "launcher").write_bytes(elf)
            elf[-1] = sequence
            for name in ("gui", "worker"):
                (folder / name).write_bytes(elf)
            elf[16] = 3
            elf[-1] = 0
            (plugin / "libNbtManager.so").write_bytes(elf)
            for name in NAMES[1:]:
                (plugin / name).write_bytes(f"{name} generation {generation}".encode())
            result = run("python3", PACK, "--version", f"v{sequence}", "--sequence", sequence,
                         "--compat", "demo-1", "--key", key, "--output", cls.path / f"release-{sequence}",
                         "--launcher", folder / "launcher", "--gui", folder / "gui",
                         "--worker", folder / "worker", "--plugin-dir", plugin)
            if result.returncode:
                raise RuntimeError(result.stderr.decode())

    @classmethod
    def tearDownClass(cls):
        cls.assets.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "managed"
        self.root.mkdir(mode=0o700)
        self.hmi = Path(self.tmp.name) / "hmi"
        self.hmi.mkdir(mode=0o700)

    def tearDown(self):
        self.tmp.cleanup()

    def install(self, sequence, plugin_from=None, launcher_from=None):
        """A manager set up with `sequence`, carrying an older plugin and launcher."""
        result = run(SETUP, self.root, self.path / f"release-{sequence}", self.path / "manager.pub", "demo-1")
        self.assertEqual(result.returncode, 0, result.stderr)
        result = run(PLUGIN, self.hmi, self.path / f"release-{plugin_from or sequence}",
                     self.path / "manager.pub", "demo-1")
        self.assertEqual(result.returncode, 0, result.stderr)
        if launcher_from:
            launcher = self.root / "manager/nbt-manager-launcher"
            shutil.copyfile(self.path / f"parts-{launcher_from}/launcher", launcher)
            launcher.chmod(0o700)

    def refresh(self, fail=None, env=None):
        args = [REFRESH, self.root, self.hmi] + ([fail] if fail else [])
        return run(*args, env=dict(os.environ, **(env or {})))

    def plugin(self):
        return {name: (self.hmi / "Plugins/NbtManager" / name).read_bytes() for name in NAMES}

    def expected_plugin(self, sequence):
        folder = self.path / f"parts-{sequence}/plugin"
        return {name: (folder / name).read_bytes() for name in NAMES}

    def test_old_launcher_and_plugin_are_replaced_once(self):
        self.install(4, plugin_from=1, launcher_from=1)
        result = self.refresh()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"remount rw\nremount ro\nchanged=3\n")
        self.assertEqual((self.root / "manager/nbt-manager-launcher").read_bytes(),
                         (self.path / "parts-4/launcher").read_bytes())
        self.assertEqual((self.root / "manager/nbt-manager-launcher").stat().st_mode & 0o777, 0o700)
        self.assertEqual(self.plugin(), self.expected_plugin(4))
        plugins = self.hmi / "Plugins"
        self.assertEqual(sorted(p.name for p in plugins.iterdir()), ["NbtManager"])
        self.assertEqual((plugins / "NbtManager/libNbtManager.so").stat().st_mode & 0o777, 0o755)
        again = self.refresh()
        self.assertEqual((again.returncode, again.stdout), (0, b"changed=0\n"))

    def test_newer_release_with_the_same_plugin_touches_nothing(self):
        """Releases 4 and 5 differ in GUI and worker only: same launcher and plugin
        checksums. Updating to 5 replaces neither, remounts nothing and so asks for
        no iDrive restart (the kits dev48 to dev50 have identical ones)."""
        self.install(5, plugin_from=4)
        launcher = self.root / "manager/nbt-manager-launcher"
        before = {p: p.stat().st_mtime_ns for p in [launcher, *(self.hmi / "Plugins/NbtManager").iterdir()]}
        result = self.refresh("rw")  # a remount would fail and show
        self.assertEqual((result.returncode, result.stdout), (0, b"changed=0\n"), result.stderr)
        self.assertEqual({p: p.stat().st_mtime_ns for p in before}, before)
        env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root), NBT_TEST_HMI_ROOT=str(self.hmi))
        result = run(LAUNCHER, "--refresh-setup", env=env)
        self.assertEqual((result.returncode, result.stdout), (0, b"REFRESHED\n"), result.stderr)
        self.assertEqual({p: p.stat().st_mtime_ns for p in before}, before)

    def test_launcher_alone_needs_no_remount(self):
        self.install(4, launcher_from=1)
        result = self.refresh()
        self.assertEqual((result.returncode, result.stdout), (0, b"changed=1\n"), result.stderr)

    def test_nothing_happens_while_an_update_is_on_trial(self):
        self.install(4, plugin_from=1, launcher_from=1)
        shutil.copyfile(self.path / "release-5", self.root / "state/manager/release.upload")
        (self.root / "state/manager/release.upload").chmod(0o600)
        staged = run(STAGE, self.root, "update")
        self.assertEqual(staged.returncode, 0, staged.stderr)
        result = self.refresh()
        self.assertEqual((result.returncode, result.stdout), (0, b"trial\n"), result.stderr)
        self.assertEqual(self.plugin(), self.expected_plugin(1))
        self.assertEqual((self.root / "manager/nbt-manager-launcher").read_bytes(),
                         (self.path / "parts-1/launcher").read_bytes())

    def test_installer_update_replaces_both_right_after_staging(self):
        """--refresh-setup: the installer's Update, before any opening (pending)."""
        self.install(1)
        shutil.copyfile(self.path / "release-4", self.root / "state/manager/release.upload")
        (self.root / "state/manager/release.upload").chmod(0o600)
        staged = run(STAGE, self.root, "update")
        self.assertEqual(staged.returncode, 0, staged.stderr)
        env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root), NBT_TEST_HMI_ROOT=str(self.hmi))
        result = run(LAUNCHER, "--refresh-setup", env=env)
        self.assertEqual((result.returncode, result.stdout), (0, b"REFRESHED plugin\n"), result.stderr)
        self.assertEqual((self.root / "manager/nbt-manager-launcher").read_bytes(),
                         (self.path / "parts-4/launcher").read_bytes())
        self.assertEqual(self.plugin(), self.expected_plugin(4))
        again = run(LAUNCHER, "--refresh-setup", env=env)
        self.assertEqual((again.returncode, again.stdout), (0, b"REFRESHED\n"), again.stderr)

    def test_installer_update_waits_for_a_closed_manager(self):
        self.install(4, plugin_from=1)
        import fcntl
        with open(self.root / "state/manager/operation.lock", "a+") as lock:
            fcntl.lockf(lock, fcntl.LOCK_EX)
            env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root), NBT_TEST_HMI_ROOT=str(self.hmi))
            result = run(LAUNCHER, "--refresh-setup", env=env)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"manager operation is in progress", result.stderr)
        self.assertEqual(self.plugin(), self.expected_plugin(1))

    def test_refused_remount_leaves_the_plugin(self):
        self.install(4, plugin_from=1)
        result = self.refresh("rw")
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"cannot make the iDrive plugin folder writable", result.stderr)
        self.assertEqual(result.stdout, b"remount rw\n")
        self.assertEqual(self.plugin(), self.expected_plugin(1))

    def test_partition_goes_back_read_only_after_a_failure(self):
        self.install(4, plugin_from=1)
        self.hmi.chmod(0o757)  # an unsafe plugin folder: the swap is refused
        result = self.refresh()
        self.hmi.chmod(0o700)
        self.assertEqual(result.returncode, 1)
        self.assertIn(b"HMI layout", result.stderr)
        self.assertEqual(result.stdout, b"remount rw\nremount ro\n")
        self.assertEqual(self.plugin(), self.expected_plugin(1))

    def test_swap_interrupted_between_renames_completes_next_time(self):
        self.install(4, plugin_from=1)
        result = self.refresh(env={"NBT_HMI_FS_FAULT": "replace-old"})
        self.assertEqual(result.returncode, 91, result.stderr)
        plugins = self.hmi / "Plugins"
        self.assertFalse((plugins / "NbtManager").exists())
        self.assertTrue((plugins / ".NbtManager.old").is_dir())
        result = self.refresh()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"remount rw\nremount ro\nchanged=2\n")
        self.assertEqual(self.plugin(), self.expected_plugin(4))
        self.assertEqual(sorted(p.name for p in plugins.iterdir()), ["NbtManager"])

    def test_swap_interrupted_after_renames_cleans_up(self):
        self.install(4, plugin_from=1)
        result = self.refresh(env={"NBT_HMI_FS_FAULT": "replace-live"})
        self.assertEqual(result.returncode, 91, result.stderr)
        self.assertEqual(self.plugin(), self.expected_plugin(4))
        plugins = self.hmi / "Plugins"
        self.assertTrue((plugins / ".NbtManager.old").is_dir())
        # The leftover is removed (iDrive could load it as a second plugin), but
        # the plugin itself was already current: no iDrive restart is asked for.
        result = self.refresh()
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"remount rw\nremount ro\nchanged=0\n")
        self.assertEqual(sorted(p.name for p in plugins.iterdir()), ["NbtManager"])


if __name__ == "__main__":
    unittest.main()
