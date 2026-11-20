# SPDX-License-Identifier: MPL-2.0
import os
import fcntl
import hashlib
import json
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time
import unittest
from unittest import mock

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build"))
STAGE = BUILD / "manager-stage-probe"
SETUP = BUILD / "manager-setup-probe"
PLUGIN = BUILD / "manager-plugin-probe"
PLUGIN_SWAP = BUILD / "manager-plugin-swap-probe"
STORE = BUILD / "manager-store-probe"
PACK = ROOT / "tools/package_release.py"
LAUNCHER = BUILD / "launcher-host"
GUI = BUILD / "manager-gui-host"


def run(*args, env=None):
    return subprocess.run([str(arg) for arg in args], env=env, capture_output=True, check=False)


class ManagerStageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assets = tempfile.TemporaryDirectory()
        cls.path = Path(cls.assets.name)
        for name in ("manager", "other"):
            key = cls.path / (name + ".pem")
            result = run("openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:3072", "-out", key)
            if result.returncode:
                raise RuntimeError(result.stderr.decode())
            result = run("openssl", "pkey", "-in", key, "-pubout", "-out", cls.path / (name + ".pub"))
            if result.returncode:
                raise RuntimeError(result.stderr.decode())
        elf = bytearray(128)
        elf[:7] = b"\x7fELF\x01\x01\x01"
        elf[16:20] = b"\x02\x00\x28\x00"
        (cls.path / "launcher").write_bytes(elf)
        plugin = cls.path / "plugin"
        plugin.mkdir()
        elf[16] = 3
        (plugin / "libNbtManager.so").write_bytes(elf)
        for name in ("NbtManager.manifest", "NbtManager.xml", "NbtManager_ALL_Texts.zip", "NbtManager_ALL_Images.zip"):
            (plugin / name).write_bytes(name.encode())
        for sequence, signer in ((1, "manager"), (2, "manager"), (3, "other")):
            elf[16] = 2
            elf[-1] = sequence
            for name in ("gui", "worker"):
                (cls.path / name).write_bytes(elf)
                (cls.path / f"{name}-{sequence}").write_bytes(elf)
            result = run("python3", PACK, "--version", f"v{sequence}", "--sequence", sequence,
                         "--compat", "demo-1", "--key", cls.path / (signer + ".pem"),
                         "--output", cls.path / f"release-{sequence}", "--launcher", cls.path / "launcher",
                         "--gui", cls.path / "gui", "--worker", cls.path / "worker", "--plugin-dir", plugin)
            if result.returncode:
                raise RuntimeError(result.stderr.decode())

    @classmethod
    def tearDownClass(cls):
        cls.assets.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "managed"
        for path in ("data", "state/manager", "trust/apps", "manager/slots/0", "manager/slots/1"):
            (self.root / path).mkdir(parents=True, mode=0o700)
        for path in [self.root, *self.root.rglob("*")]:
            if path.is_dir():
                path.chmod(0o700)
        shutil.copyfile(self.path / "manager.pub", self.root / "trust/manager.pem")
        (self.root / "trust/manager.conf").write_text("NBTMANAGERPROFILE1\ncompat=demo-1\n")
        (self.root / "trust/manager.pem").chmod(0o600)
        (self.root / "trust/manager.conf").chmod(0o600)
        self.victim = Path(self.tmp.name) / "oem"
        self.victim.write_text("unchanged")

    def tearDown(self):
        self.tmp.cleanup()

    def upload(self, sequence):
        shutil.copyfile(self.path / f"release-{sequence}", self.root / "state/manager/release.upload")
        (self.root / "state/manager/release.upload").chmod(0o600)

    def stage(self, action, fault=None, error=None):
        env = dict(os.environ)
        if fault:
            env["NBT_MANAGER_STAGE_FAULT"] = fault
        if error:
            env["NBT_MANAGER_STAGE_ERROR"] = error
        return run(STAGE, self.root, action, env=env)

    def state(self):
        result = run(STORE, self.root, "load", 1)
        self.assertEqual(result.returncode, 0, result.stderr)
        return dict(line.split("=", 1) for line in result.stdout.decode().splitlines()[1:])

    def baseline(self):
        self.upload(1)
        result = self.stage("initial")
        self.assertEqual(result.returncode, 0, result.stderr)

    def bridge_app(self, version="1.0", uses="", optional=""):
        for part in ("apps/test/bin", "state/test"):
            (self.root / part).mkdir(parents=True, mode=0o700)
        (self.root / "apps/test").chmod(0o700)
        (self.root / "apps").chmod(0o700)
        record = ("NBTSTATE2\nsequence=00000000000000000001\nhighwater=00000000000000000001\n"
                  "version=v1\nsha256=" + "1" * 64 + f"\nbridge={version}\nbridge-uses={uses}\nbridge-optional={optional}\n")
        for name, text in (("state/test/installed", record), ("trust/apps/test.conf", "NBTAPP1\ncompat=demo-1\n")):
            (self.root / name).write_text(text)
            (self.root / name).chmod(0o600)

    def bridge_release(self, sequence, *options):
        output = Path(self.tmp.name) / f"bridge-{sequence}.nbtr"
        result = run("python3", PACK, "--version", f"v{sequence}", "--sequence", sequence,
                     "--compat", "demo-1", "--key", self.path / "manager.pem", "--output", output,
                     "--launcher", self.path / "launcher", "--gui", self.path / "gui", "--worker", self.path / "worker",
                     "--plugin-dir", self.path / "plugin", *options)
        self.assertEqual(result.returncode, 0, result.stderr)
        shutil.copyfile(output, self.root / "state/manager/release.upload")
        (self.root / "state/manager/release.upload").chmod(0o600)
        return output.read_bytes()

    def test_manager_update_names_an_application_it_disables(self):
        # Not refused: the new release disables the application until it is updated.
        self.baseline()
        self.bridge_app(optional="show")
        self.bridge_release(2, "--no-bridge")
        result = self.stage("update")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"Disabled until updated: test: needs bridge 1.0; this manager does not serve that major",
                      result.stderr)
        self.assertEqual(self.state()["phase"], "pending")
        self.assertTrue((self.root / "manager/slots/1/release.nbtr").exists())
        self.bridge_release(3)  # the current provider grants the optional show
        result = self.stage("update")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn(b"Disabled", result.stderr)
        self.assertEqual(self.state()["phase"], "pending")

    def test_pending_manager_activation_blocks_new_app_dependencies(self):
        self.baseline()
        (self.root / "trust/keys").mkdir(mode=0o700)
        self.bridge_app()
        self.bridge_release(2)
        self.assertEqual(self.stage("update").returncode, 0)
        before = (self.root / "state/test/installed").read_bytes()
        # Provisioning must refuse before publishing a new profile.
        source = Path(self.tmp.name) / "app"
        source.write_bytes((self.path / "gui").read_bytes())
        package = Path(self.tmp.name) / "app.nbt"
        result = run("python3", ROOT / "tools/package.py", "--app", "other", "--version", "v1",
                     "--sequence", "1", "--compat", "demo-1", "--key", self.path / "manager.pem",
                     "--output", package, source)
        self.assertEqual(result.returncode, 0, result.stderr)
        key, payload = (self.path / "manager.pub").read_bytes(), package.read_bytes()
        env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root))
        result = subprocess.run([LAUNCHER, "--provision-app", "other", "demo-1", str(len(key)), str(len(payload)),
                                 hashlib.sha256(payload).hexdigest()], input=key + payload, env=env, capture_output=True)
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn(b"manager update awaiting confirmation", result.stderr)
        self.assertFalse((self.root / "trust/apps/other.conf").exists())
        self.assertEqual((self.root / "state/test/installed").read_bytes(), before)

    def test_provisioning_uses_the_active_signed_manager_bridge(self):
        # The package below is signed with the manager key, the apps key here.
        self.enterContext(mock.patch.dict(os.environ, {"NBT_TEST_APPS_KEY": str(self.path / "manager.pub")}))
        self.baseline()
        (self.root / "trust/keys").mkdir(mode=0o700)
        (self.root / "apps").mkdir(mode=0o700)
        source = Path(self.tmp.name) / "app"
        source.write_bytes((self.path / "gui").read_bytes())
        package = Path(self.tmp.name) / "app.nbt"
        env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root))

        def provision(bridge):
            package.unlink(missing_ok=True)
            result = run("python3", ROOT / "tools/package.py", "--app", "test", "--version", "v1", "--sequence", "1",
                         "--compat", "demo-1", "--key", self.path / "manager.pem", "--output", package,
                         "--bridge", bridge, "--bridge-optional", "show", source)
            self.assertEqual(result.returncode, 0, result.stderr)
            key, payload = (self.path / "manager.pub").read_bytes(), package.read_bytes()
            return subprocess.run([LAUNCHER, "--provision-app", "test", "demo-1", str(len(key)), str(len(payload)),
                                   hashlib.sha256(payload).hexdigest()], input=key + payload, env=env,
                                  capture_output=True), payload

        # The release lines cannot state the oldest version this manager runs; the launcher adds it.
        result, _ = provision("1.4")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn(b"declares bridge 1.4; this manager runs 1.5 and later", result.stderr)
        self.assertFalse((self.root / "trust/apps/test.conf").exists())
        result, payload = provision("1.5")
        self.assertEqual(result.returncode, 0, result.stderr)
        record = (self.root / "state/test/installed").read_text()
        self.assertTrue(record.startswith("NBTSTATE2\n"))
        self.assertIn("bridge=1.5\nbridge-uses=\nbridge-optional=show\n", record)
        (self.root / "state/test/upload").write_bytes(payload)
        (self.root / "state/test/upload").chmod(0o600)
        self.bridge_release(2)
        self.assertEqual(self.stage("update").returncode, 0)
        result = run(BUILD / "transaction-probe", self.root, "test", "begin")
        self.assertEqual(result.returncode, 1, result.stderr)
        self.assertIn(b"manager update or recovery in progress", result.stderr)
        self.assertEqual((self.root / "state/test/installed").read_text(), record)

    def test_manager_update_checks_required_capabilities_and_minor(self):
        self.baseline()
        self.bridge_app(version="1.2", uses="show")
        self.bridge_release(2, "--bridge", "1.1|show")
        result = self.stage("update")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"Disabled until updated: test: needs bridge 1.2", result.stderr)
        self.bridge_release(3, "--bridge", "1.3|status")
        result = self.stage("update")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"Disabled until updated: test: needs show", result.stderr)
        self.bridge_release(4, "--bridge", "1.3|show", "--bridge", "2.0|")
        result = self.stage("update")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertNotIn(b"Disabled", result.stderr)

    def test_explicit_manager_recovery_warns_about_bridge_dependencies(self):
        self.bridge_release(5)
        self.assertEqual(self.stage("initial").returncode, 0)
        self.bridge_app(uses="status")
        self.bridge_release(1, "--no-bridge")
        result = self.stage("recovery")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"Recovery warning: test:", result.stderr)
        self.assertEqual(self.state()["phase"], "pending")
        self.assertEqual(int(self.state()["highwater"]), 5)

    def test_manager_stage_refuses_pending_app_transaction_and_unsafe_record(self):
        self.baseline()
        self.bridge_app()
        self.bridge_release(2)
        journal = self.root / "state/test/journal"
        journal.write_text("interrupted")
        journal.chmod(0o600)
        self.assertIn(b"test: application update or recovery", self.stage("update").stderr)
        journal.unlink()
        record = self.root / "state/test/installed"
        victim = self.victim.read_bytes()
        record.unlink()
        record.symlink_to(self.victim)
        self.assertIn(b"test: invalid installed record", self.stage("update").stderr)
        self.assertEqual(self.victim.read_bytes(), victim)
        self.assertEqual(self.state()["phase"], "stable")

    def test_initial_activation_retry_requires_complete_verified_slot(self):
        self.upload(1)
        result = self.stage("initial", "slot-synced")
        self.assertEqual(result.returncode, 91)
        gui = self.root / "manager/slots/0/gui"
        original = gui.read_bytes()
        gui.write_bytes(b"corrupted")
        self.assertNotEqual(self.stage("initial").returncode, 0)
        self.assertFalse((self.root / "state/manager/activation").exists())
        self.assertEqual(gui.read_bytes(), b"corrupted")
        self.assertEqual(self.victim.read_text(), "unchanged")
        gui.write_bytes(original)
        self.assertEqual(self.stage("initial").returncode, 0)
        self.assertEqual(self.state()["slot"], "0")

    def test_initial_activation_record_interruption_can_be_retried(self):
        self.upload(1)
        for fault in ("created", "written", "synced"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as directory:
                original_root = self.root
                self.root = Path(directory) / "managed"
                shutil.copytree(original_root, self.root)
                env = dict(os.environ, NBT_MANAGER_STATE_FAULT=fault)
                self.assertEqual(run(STAGE, self.root, "initial", env=env).returncode, 91)
                self.assertTrue((self.root / "state/manager/activation.next").exists())
                self.assertEqual(self.stage("initial").returncode, 0)
                self.assertEqual(self.state()["slot"], "0")
                self.assertFalse((self.root / "state/manager/activation.next").exists())
                self.root = original_root

    def test_signed_plugin_registration_preserves_oem_and_is_idempotent(self):
        hmi = Path(self.tmp.name) / "hmi"
        (hmi / "Plugins/OEM").mkdir(parents=True, mode=0o700)
        hmi.chmod(0o700)
        (hmi / "Plugins").chmod(0o700)
        (hmi / "Plugins/OEM/keep").write_text("original")
        args = (PLUGIN, hmi, self.path / "release-1", self.path / "manager.pub", "demo-1")
        result = run(*args)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(run(*args).returncode, 0)
        target = hmi / "Plugins/NbtManager"
        self.assertEqual((target / "libNbtManager.so").read_bytes(), (self.path / "plugin/libNbtManager.so").read_bytes())
        self.assertEqual((target / "NbtManager.xml").read_bytes(), (self.path / "plugin/NbtManager.xml").read_bytes())
        self.assertEqual((hmi / "Plugins/OEM/keep").read_text(), "original")
        self.assertEqual(self.victim.read_text(), "unchanged")

    def test_plugin_registration_retries_interrupted_file_writes(self):
        for fault in [f"{phase}-{index}" for phase in ("created", "written", "synced") for index in range(5)] + ["published-0"]:
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as directory:
                hmi = Path(directory) / "hmi"
                hmi.mkdir(mode=0o700)
                args = (PLUGIN, hmi, self.path / "release-1", self.path / "manager.pub", "demo-1")
                self.assertEqual(run(*args, env=dict(os.environ, NBT_HMI_SETUP_FAULT=fault)).returncode, 91)
                result = run(*args)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue((hmi / "Plugins/NbtManager/NbtManager.manifest").is_file())

    def test_plugin_registration_recovers_interrupted_directory_publish(self):
        for fault in ("chmod", "synced", "renamed"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as directory:
                hmi = Path(directory) / "hmi"
                hmi.mkdir(mode=0o700)
                args = (PLUGIN, hmi, self.path / "release-1", self.path / "manager.pub", "demo-1")
                self.assertEqual(run(*args, env=dict(os.environ, NBT_HMI_FS_FAULT=fault)).returncode, 91)
                result = run(*args)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertTrue((hmi / "Plugins/NbtManager/NbtManager.manifest").is_file())

    def test_plugin_registration_refuses_conflicts_and_symlinks(self):
        hmi = Path(self.tmp.name) / "hmi"
        hmi.mkdir(mode=0o700)
        args = (PLUGIN, hmi, self.path / "release-1", self.path / "manager.pub", "demo-1")
        self.assertEqual(run(*args, env=dict(os.environ, NBT_HMI_SETUP_FAULT="created-0")).returncode, 91)
        temporary = hmi / "Plugins/.NbtManager.setup"
        (temporary / "libNbtManager.so").unlink()
        (temporary / "libNbtManager.so").symlink_to(self.victim)
        self.assertNotEqual(run(*args).returncode, 0)
        self.assertEqual(self.victim.read_text(), "unchanged")
        (temporary / "libNbtManager.so").unlink()
        (temporary / "extra").write_text("user data")
        self.assertNotEqual(run(*args).returncode, 0)
        self.assertEqual((temporary / "extra").read_text(), "user data")
        self.assertFalse((hmi / "Plugins/NbtManager").exists())

    def test_plugin_rejects_release_change_after_preflight(self):
        hmi = Path(self.tmp.name) / "hmi"
        hmi.mkdir(mode=0o700)
        result = run(PLUGIN_SWAP, hmi, self.path / "release-1", self.path / "release-2", self.path / "manager.pub")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertFalse((hmi / "Plugins").exists())
        self.assertEqual(self.victim.read_text(), "unchanged")

    def test_plugin_registration_refuses_existing_modified_plugin(self):
        hmi = Path(self.tmp.name) / "hmi"
        hmi.mkdir(mode=0o700)
        args = (PLUGIN, hmi, self.path / "release-1", self.path / "manager.pub", "demo-1")
        self.assertEqual(run(*args).returncode, 0)
        manifest = hmi / "Plugins/NbtManager/NbtManager.manifest"
        manifest.write_text("changed")
        self.assertNotEqual(run(*args).returncode, 0)
        self.assertEqual(manifest.read_text(), "changed")

    def test_bootstrap_stream_is_verified_before_root_creation(self):
        self.root = Path(self.tmp.name) / "bootstrap-managed"
        release = (self.path / "release-1").read_bytes()
        key = (self.path / "manager.pub").read_bytes()
        digest = hashlib.sha256(release).hexdigest()
        args = [LAUNCHER, "--bootstrap-manager", str(len(key)), str(len(release)), "demo-1", digest]
        hmi = Path(self.tmp.name) / "hmi"
        hmi.mkdir(mode=0o700)
        env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root), NBT_TEST_HMI_ROOT=str(hmi))
        for payload in (key + release[:-1], key + release + b"extra", key + release[:-1] + bytes([release[-1] ^ 1])):
            result = subprocess.run(args, input=payload, capture_output=True, env=env)
            self.assertNotEqual(result.returncode, 0)
            self.assertFalse(self.root.exists())
        result = subprocess.run(args, input=key + release, capture_output=True,
                                env=dict(env, NBT_TEST_SETUP_NO_SPACE="1"))
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.root.exists())
        result = subprocess.run(args, input=key + release, capture_output=True,
                                env=dict(env, NBT_TEST_HMI_NO_SPACE="1"))
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.root.exists())
        result = subprocess.run(args, input=key + release, capture_output=True, env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"INSTALLED\n")
        self.assertTrue((hmi / "Plugins/NbtManager/NbtManager.manifest").is_file())
        self.assertEqual(self.state()["slot"], "0")
        self.assertEqual(subprocess.run(args, input=key + release, capture_output=True, env=env).returncode, 0)
        self.assertEqual(self.victim.read_text(), "unchanged")

    def test_bootstrap_accepts_the_cars_folder_modes_and_names_refusals(self):
        """The car's /opt/hmi/ID5/data/ro is root:root 0775 (dev9 failed on it)."""
        release = (self.path / "release-1").read_bytes()
        key = (self.path / "manager.pub").read_bytes()
        args = [LAUNCHER, "--bootstrap-manager", str(len(key)), str(len(release)), "demo-1",
                hashlib.sha256(release).hexdigest()]
        for mode, trusted, reason in ((0o775, False, b"HMI layout: plugin folder writable by other users\n"),
                                      (0o757, True, b"HMI layout: plugin folder writable by other users\n"),
                                      (0o775, True, None), (0o755, False, None)):
            with self.subTest(mode=oct(mode), trusted=trusted), tempfile.TemporaryDirectory() as directory:
                self.root = Path(directory) / "managed"
                hmi = Path(directory) / "hmi"
                hmi.mkdir()
                hmi.chmod(mode)
                env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root), NBT_TEST_HMI_ROOT=str(hmi))
                if trusted:
                    env["NBT_TEST_HMI_TRUSTED_GID"] = str(os.getgid())
                result = subprocess.run(args, input=key + release, capture_output=True, env=env)
                if reason:
                    self.assertEqual((result.returncode, result.stderr), (1, reason))
                    self.assertFalse(self.root.exists())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, b"INSTALLED\n")
        with tempfile.TemporaryDirectory() as directory:
            self.root = Path(directory) / "managed"
            result = subprocess.run(args, input=key + release, capture_output=True,
                                    env=dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root),
                                             NBT_TEST_HMI_ROOT=str(Path(directory) / "missing")))
            self.assertEqual(result.stderr, b"HMI layout: plugin folder missing\n")

    def test_bootstrap_accepts_the_cars_var_opt_mode_and_names_refusals(self):
        """The car's /var/opt, the managed root's parent, is root:root 0775 (dev13 failed on it)."""
        release = (self.path / "release-1").read_bytes()
        key = (self.path / "manager.pub").read_bytes()
        args = [LAUNCHER, "--bootstrap-manager", str(len(key)), str(len(release)), "demo-1",
                hashlib.sha256(release).hexdigest()]
        refused = b"manager storage: parent folder writable by other users\n"
        for mode, trusted, reason in ((0o775, False, refused), (0o757, True, refused),
                                      (0o775, True, None), (0o755, False, None)):
            with self.subTest(mode=oct(mode), trusted=trusted), tempfile.TemporaryDirectory() as directory:
                parent = Path(directory) / "var-opt"
                parent.mkdir()
                parent.chmod(mode)
                self.root = parent / "nbt-manager"
                hmi = Path(directory) / "hmi"
                hmi.mkdir(mode=0o755)
                env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root), NBT_TEST_HMI_ROOT=str(hmi))
                if trusted:
                    env["NBT_TEST_HMI_TRUSTED_GID"] = str(os.getgid())
                result = subprocess.run(args, input=key + release, capture_output=True, env=env)
                if reason:
                    self.assertEqual((result.returncode, result.stderr), (1, reason))
                    self.assertFalse(self.root.exists())
                else:
                    self.assertEqual(result.returncode, 0, result.stderr)
                    self.assertEqual(result.stdout, b"INSTALLED\n")
                    self.assertEqual(self.state()["slot"], "0")

    def test_bootstrap_checks_hmi_conflict_before_creating_manager_root(self):
        self.root = Path(self.tmp.name) / "bootstrap-managed"
        hmi = Path(self.tmp.name) / "hmi"
        hmi.mkdir(mode=0o700)
        (hmi / "Plugins").symlink_to(self.victim.parent, target_is_directory=True)
        release = (self.path / "release-1").read_bytes()
        key = (self.path / "manager.pub").read_bytes()
        args = [LAUNCHER, "--bootstrap-manager", str(len(key)), str(len(release)), "demo-1", hashlib.sha256(release).hexdigest()]
        result = subprocess.run(args, input=key + release, capture_output=True,
                                env=dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root), NBT_TEST_HMI_ROOT=str(hmi)))
        self.assertNotEqual(result.returncode, 0)
        self.assertFalse(self.root.exists())
        self.assertEqual(self.victim.read_text(), "unchanged")

    def test_bootstrap_retries_interrupted_hmi_registration(self):
        self.root = Path(self.tmp.name) / "bootstrap-managed"
        hmi = Path(self.tmp.name) / "hmi"
        hmi.mkdir(mode=0o700)
        release = (self.path / "release-1").read_bytes()
        key = (self.path / "manager.pub").read_bytes()
        args = [LAUNCHER, "--bootstrap-manager", str(len(key)), str(len(release)), "demo-1", hashlib.sha256(release).hexdigest()]
        env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root), NBT_TEST_HMI_ROOT=str(hmi))
        self.assertEqual(subprocess.run(args, input=key + release, capture_output=True,
                                        env=dict(env, NBT_HMI_SETUP_FAULT="written-2")).returncode, 91)
        result = subprocess.run(args, input=key + release, capture_output=True, env=env)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"INSTALLED\n")
        self.assertEqual(self.state()["slot"], "0")
        self.assertTrue((hmi / "Plugins/NbtManager/NbtManager.manifest").is_file())

    def test_bootstrap_refuses_unsafe_parent_or_existing_root(self):
        self.root = Path(self.tmp.name) / "bootstrap-managed"
        self.root.symlink_to(self.victim.parent, target_is_directory=True)
        release = (self.path / "release-1").read_bytes()
        key = (self.path / "manager.pub").read_bytes()
        args = [LAUNCHER, "--bootstrap-manager", str(len(key)), str(len(release)), "demo-1", hashlib.sha256(release).hexdigest()]
        hmi = Path(self.tmp.name) / "hmi"
        hmi.mkdir(mode=0o700)
        result = subprocess.run(args, input=key + release, capture_output=True,
                                env=dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root), NBT_TEST_HMI_ROOT=str(hmi)))
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.victim.read_text(), "unchanged")

    def test_first_setup_is_idempotent_and_keeps_unrelated_files(self):
        self.root = Path(self.tmp.name) / "new-managed"
        self.root.mkdir(mode=0o700)
        args = (SETUP, self.root, self.path / "release-1", self.path / "manager.pub", "demo-1")
        result = run(*args)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(run(*args).returncode, 0)
        self.assertEqual(self.state()["slot"], "0")
        self.assertEqual((self.root / "manager/nbt-manager-launcher").read_bytes(), (self.path / "launcher").read_bytes())
        self.assertEqual((self.root / "manager/slots/0/gui").read_bytes(), (self.path / "gui-1").read_bytes())
        self.assertEqual(self.victim.read_text(), "unchanged")

    def test_first_setup_retries_published_files_after_interruption(self):
        for fault in ("key", "profile", "launcher", "upload",
                      "key-created", "key-written", "key-synced", "profile-created",
                      "profile-written", "profile-synced", "launcher-created", "launcher-written",
                      "launcher-synced", "upload-created", "upload-written", "upload-synced"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as directory:
                original = self.root
                self.root = Path(directory) / "managed"
                self.root.mkdir(mode=0o700)
                args = (SETUP, self.root, self.path / "release-1", self.path / "manager.pub", "demo-1")
                self.assertEqual(run(*args, env=dict(os.environ, NBT_MANAGER_SETUP_FAULT=fault)).returncode, 91)
                result = run(*args)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.state()["slot"], "0")
                self.root = original

    def test_first_setup_retries_each_interrupted_slot_write(self):
        for fault in ("release-created", "release-written", "release-synced", "release",
                      "gui-created", "gui-written", "gui-synced", "gui", "worker-created",
                      "worker-written", "worker-synced", "worker", "slot-synced"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as directory:
                original = self.root
                self.root = Path(directory) / "managed"
                self.root.mkdir(mode=0o700)
                args = (SETUP, self.root, self.path / "release-1", self.path / "manager.pub", "demo-1")
                self.assertEqual(run(*args, env=dict(os.environ, NBT_MANAGER_STAGE_FAULT=fault)).returncode, 91)
                result = run(*args)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(self.state()["slot"], "0")
                self.assertEqual((self.root / "manager/slots/0/gui").read_bytes(), (self.path / "gui-1").read_bytes())
                self.root = original

    def test_first_setup_rejects_conflicting_initial_slot_without_replacement(self):
        self.root = Path(self.tmp.name) / "new-managed"
        self.root.mkdir(mode=0o700)
        args = (SETUP, self.root, self.path / "release-1", self.path / "manager.pub", "demo-1")
        self.assertEqual(run(*args, env=dict(os.environ, NBT_MANAGER_STAGE_FAULT="gui-created")).returncode, 91)
        gui = self.root / "manager/slots/0/gui"
        gui.write_bytes(b"conflict")
        self.assertNotEqual(run(*args).returncode, 0)
        self.assertEqual(gui.read_bytes(), b"conflict")
        self.assertEqual(self.victim.read_text(), "unchanged")

    def test_setup_preflight_rejects_conflicts_before_layout_changes(self):
        self.root = Path(self.tmp.name) / "new-managed"
        self.root.mkdir(mode=0o700)
        (self.root / "trust").mkdir(mode=0o700)
        key = self.root / "trust/manager.pem"
        key.write_text("conflicting key")
        key.chmod(0o600)
        args = (SETUP, self.root, self.path / "release-1", self.path / "manager.pub", "demo-1")
        self.assertNotEqual(run(*args).returncode, 0)
        self.assertEqual(key.read_text(), "conflicting key")
        self.assertEqual(sorted(path.name for path in self.root.iterdir()), ["trust"])

    def test_existing_manager_without_launcher_is_not_reported_as_installed(self):
        self.root = Path(self.tmp.name) / "new-managed"
        self.root.mkdir(mode=0o700)
        args = (SETUP, self.root, self.path / "release-1", self.path / "manager.pub", "demo-1")
        self.assertEqual(run(*args).returncode, 0)
        launcher = self.root / "manager/nbt-manager-launcher"
        launcher.unlink()
        self.assertNotEqual(run(*args).returncode, 0)
        self.assertFalse(launcher.exists())
        self.assertEqual(self.state()["slot"], "0")
        self.assertEqual(self.victim.read_text(), "unchanged")

    def test_corrupt_existing_manager_record_is_checked_before_layout_changes(self):
        self.root = Path(self.tmp.name) / "new-managed"
        (self.root / "state/manager").mkdir(parents=True, mode=0o700)
        for path in (self.root, self.root / "state", self.root / "state/manager"):
            path.chmod(0o700)
        record = self.root / "state/manager/activation"
        record.write_text("corrupt")
        record.chmod(0o600)
        args = (SETUP, self.root, self.path / "release-1", self.path / "manager.pub", "demo-1")
        self.assertNotEqual(run(*args).returncode, 0)
        self.assertEqual(sorted(path.name for path in self.root.iterdir()), ["state"])
        self.assertEqual(record.read_text(), "corrupt")

    def test_first_setup_rejects_bad_key_and_conflicting_files(self):
        self.root = Path(self.tmp.name) / "new-managed"
        self.root.mkdir(mode=0o700)
        args = (SETUP, self.root, self.path / "release-1", self.path / "other.pub", "demo-1")
        self.assertNotEqual(run(*args).returncode, 0)
        self.assertEqual(list(self.root.iterdir()), [])
        (self.root / "trust").mkdir(mode=0o700)
        (self.root / "trust/manager.pem").symlink_to(self.victim)
        args = (SETUP, self.root, self.path / "release-1", self.path / "manager.pub", "demo-1")
        self.assertNotEqual(run(*args).returncode, 0)
        self.assertEqual(self.victim.read_text(), "unchanged")
        self.assertFalse((self.root / "manager/nbt-manager-launcher").exists())

    def test_signed_initial_update_and_rollback(self):
        self.baseline()
        slot0 = self.root / "manager/slots/0"
        self.assertEqual((slot0 / "gui").read_bytes(), (self.path / "gui-1").read_bytes())
        self.upload(2)
        result = self.stage("update")
        self.assertEqual(result.returncode, 0, result.stderr)
        slot1 = self.root / "manager/slots/1"
        self.assertEqual((slot1 / "worker").read_bytes(), (self.path / "worker-2").read_bytes())
        self.assertEqual((self.state()["phase"], self.state()["slot"]), ("pending", "1"))
        self.assertEqual(run(STORE, self.root, "rollback", 1).returncode, 0)
        self.assertEqual(self.state()["slot"], "0")
        self.assertEqual((slot0 / "gui").read_bytes(), (self.path / "gui-1").read_bytes())
        self.assertEqual(self.stage("update").returncode, 0)

    def test_stage_interruption_keeps_known_good(self):
        for fault in ("release", "gui", "worker", "slot-synced", "record"):
            with self.subTest(fault=fault), tempfile.TemporaryDirectory() as directory:
                old = self.tmp, self.root, self.victim
                self.tmp = tempfile.TemporaryDirectory(dir=directory)
                self.root = Path(self.tmp.name) / "managed"
                for path in ("data", "state/manager", "trust/apps", "manager/slots/0", "manager/slots/1"):
                    (self.root / path).mkdir(parents=True, mode=0o700)
                for path in [self.root, *self.root.rglob("*")]:
                    if path.is_dir(): path.chmod(0o700)
                shutil.copyfile(self.path / "manager.pub", self.root / "trust/manager.pem")
                (self.root / "trust/manager.conf").write_text("NBTMANAGERPROFILE1\ncompat=demo-1\n")
                (self.root / "trust/manager.pem").chmod(0o600)
                (self.root / "trust/manager.conf").chmod(0o600)
                self.baseline()
                self.upload(2)
                self.assertEqual(self.stage("update", fault).returncode, 91)
                state = self.state()
                if state["phase"] == "pending":
                    self.assertEqual(run(STORE, self.root, "rollback", 1).returncode, 0)
                self.assertEqual((self.root / "manager/slots/0/gui").read_bytes(), (self.path / "gui-1").read_bytes())
                self.assertEqual(self.stage("update").returncode, 0)
                self.tmp.cleanup()
                self.tmp, self.root, self.victim = old

    def test_rejects_untrusted_release_and_symlink_in_inactive_slot(self):
        self.baseline()
        self.upload(3)
        self.assertNotEqual(self.stage("update").returncode, 0)
        self.upload(2)
        (self.root / "manager/slots/1/gui").symlink_to(self.victim)
        self.assertNotEqual(self.stage("update").returncode, 0)
        self.assertEqual(self.victim.read_text(), "unchanged")
        self.assertEqual(self.state()["slot"], "0")

    def test_corrupt_known_good_does_not_clear_inactive_slot(self):
        self.baseline()
        (self.root / "manager/slots/0/gui").write_bytes(b"corrupt")
        sentinel = self.root / "manager/slots/1/gui"
        sentinel.write_bytes(b"preserve")
        self.upload(2)
        self.assertNotEqual(self.stage("update").returncode, 0)
        self.assertEqual(sentinel.read_bytes(), b"preserve")
        self.assertEqual(self.state()["slot"], "0")

    def test_replay_and_bad_profile_are_rejected(self):
        self.baseline()
        self.assertNotEqual(self.stage("update").returncode, 0)
        self.upload(2)
        (self.root / "trust/manager.conf").write_text("NBTMANAGERPROFILE1\ncompat=wrong\n")
        self.assertNotEqual(self.stage("update").returncode, 0)
        self.assertEqual(self.state()["phase"], "stable")

    def test_running_manager_blocks_staging(self):
        self.baseline()
        self.upload(2)
        with (self.root / "manager/gui.lock").open("r+") as lock:
            fcntl.lockf(lock.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            self.assertNotEqual(self.stage("update").returncode, 0)
        self.assertEqual(self.state()["slot"], "0")
        self.assertFalse((self.root / "manager/slots/1/gui").exists())

    def test_trust_key_symlink_is_rejected(self):
        self.baseline()
        self.upload(2)
        key = self.root / "trust/manager.pem"
        key.unlink()
        key.symlink_to(self.victim)
        self.assertNotEqual(self.stage("update").returncode, 0)
        self.assertEqual(self.victim.read_text(), "unchanged")
        self.assertEqual(self.state()["slot"], "0")

    def test_hard_link_in_inactive_slot_is_rejected(self):
        self.baseline()
        self.upload(2)
        os.link(self.victim, self.root / "manager/slots/1/gui")
        self.assertNotEqual(self.stage("update").returncode, 0)
        self.assertEqual(self.victim.read_text(), "unchanged")
        self.assertEqual(self.state()["slot"], "0")

    def test_failed_write_and_flush_preserve_current_slot(self):
        self.baseline()
        self.upload(2)
        for error in ("release-write", "gui-sync", "worker-sync", "slot-sync"):
            with self.subTest(error=error):
                self.assertNotEqual(self.stage("update", error=error).returncode, 0)
                self.assertEqual(self.state()["phase"], "stable")
                self.assertEqual((self.root / "manager/slots/0/gui").read_bytes(), (self.path / "gui-1").read_bytes())
                self.assertEqual(self.stage("update").returncode, 0)
                self.assertEqual(run(STORE, self.root, "rollback", 1).returncode, 0)

    def worker(self, healthy):
        path = Path(self.tmp.name) / ("worker-good" if healthy else "worker-bad")
        reply = "APPS\\t0\\n" if healthy else "ERR list\\n"
        path.write_text('''#!/usr/bin/python3
import os, sys
fd, root = int(sys.argv[2]), sys.argv[4]
def line():
    data = b""
    while not data.endswith(b"\\n"):
        chunk = os.read(fd, 1)
        if not chunk: return b""
        data += chunk
    return data
if line() != b"HELLO 1\\n": sys.exit(1)
os.write(fd, b"HELLO\\t1\\n")
open(root + "/worker-started", "w").close()
while True:
    request = line()
    if not request: break
    if request.startswith(b"PHONE STATUS"):
        os.write(fd, b"PHONE\\tnone\\n")
    elif request.startswith(b"SETUP"):
        os.write(fd, b"SETUP\\tcurrent\\n")
    elif request.startswith(b"STATUS"):
        os.write(fd, b"STATUS\\t0\\t-\\t-\\t-\\t0\\t-\\tIdle\\n")
    else:
        os.write(fd, b"RUNNING\\t0\\n" if request.startswith(b"RUNNING") else b"''' + reply + '''")
open(root + "/worker-stopped", "w").close()
''')
        path.chmod(0o700)
        return path

    def launch(self, worker, gui=GUI):
        from test_layer_manager import fake_layer_manager
        env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root),
                   NBT_TEST_MANAGER_GUI=str(gui), NBT_TEST_MANAGER_WORKER=str(worker),
                   **fake_layer_manager(self.tmp.name, bound_after=1))
        return subprocess.Popen([LAUNCHER, "--open"], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)

    def wait_for(self, predicate, timeout=10):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            if predicate(): return
            time.sleep(0.1)
        self.fail("timed out waiting for supervisor")

    def door(self, ready=True):
        worker = Path(self.tmp.name) / "door-worker"
        worker.write_text('''#!/usr/bin/python3
import os, sys, time
assert sys.argv[1:4] == ["--door", "--ready-fd", "3"]
root = sys.argv[5]
with open(root + "/door-pid", "w") as output:
    output.write(str(os.getpid()))
if ''' + repr(ready) + ''':
    os.write(3, b"READY\\n")
os.close(3)
for fd in (0, 1, 2):
    os.close(fd)
time.sleep(30)
''')
        worker.chmod(0o700)
        env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root),
                   NBT_TEST_MANAGER_WORKER=str(worker))
        result = run(LAUNCHER, "--door", env=env)
        pid_file = self.root / "door-pid"
        if pid_file.exists():
            pid = int(pid_file.read_text())
            def stop():
                try: os.kill(pid, signal.SIGTERM)
                except ProcessLookupError: pass
            self.addCleanup(stop)
        return result

    def test_door_verifies_previous_slot_during_pending_activation(self):
        self.baseline()
        self.upload(2)
        self.assertEqual(self.stage("update").returncode, 0)
        # An unproven active worker is never selected for the boot listener.
        (self.root / "manager/slots/1/worker").write_bytes(b"corrupt trial")
        self.assertEqual(self.door().returncode, 0)
        with (self.root / "state/manager/operation.lock").open("r+") as lock:
            fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        (self.root / "manager/slots/0/worker").write_bytes(b"corrupt known-good")
        (self.root / "door-pid").unlink()
        self.assertNotEqual(self.door().returncode, 0)
        self.assertFalse((self.root / "door-pid").exists())
        self.assertEqual(self.state()["phase"], "pending")

    def test_door_requires_worker_readiness_before_releasing_operation_lock(self):
        self.baseline()
        self.assertNotEqual(self.door(ready=False).returncode, 0)
        with (self.root / "state/manager/operation.lock").open("r+") as lock:
            fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)

    def autostart(self, exit_code=0):
        worker = Path(self.tmp.name) / "autostart-worker"
        worker.write_text('''#!/usr/bin/python3
import fcntl, sys
assert sys.argv[1:3] == ["--autostart", "--root"], sys.argv
root = sys.argv[3]
try:
    with open(root + "/state/manager/operation.lock", "r+") as lock:
        fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    held = "free"
except OSError:
    held = "held"
with open(root + "/autostart-ran", "w") as output:
    output.write(held)
sys.exit(''' + str(exit_code) + ''')
''')
        worker.chmod(0o700)
        env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root),
                   NBT_TEST_MANAGER_WORKER=str(worker))
        return run(LAUNCHER, "--autostart", env=env)

    def test_autostart_dispatches_with_the_known_good_worker_under_the_operation_lock(self):
        self.baseline()
        self.assertEqual(self.autostart().returncode, 0)
        # The dispatch ran while the launcher held the operation lock, and released it after.
        self.assertEqual((self.root / "autostart-ran").read_text(), "held")
        with (self.root / "state/manager/operation.lock").open("r+") as lock:
            fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        self.assertNotEqual(self.autostart(exit_code=1).returncode, 0)
        # During a pending activation only the previous, known-good slot is used.
        self.upload(2)
        self.assertEqual(self.stage("update").returncode, 0)
        (self.root / "manager/slots/1/worker").write_bytes(b"corrupt trial")
        (self.root / "autostart-ran").unlink()
        self.assertEqual(self.autostart().returncode, 0)
        (self.root / "manager/slots/0/worker").write_bytes(b"corrupt known-good")
        (self.root / "autostart-ran").unlink()
        self.assertNotEqual(self.autostart().returncode, 0)
        self.assertFalse((self.root / "autostart-ran").exists())
        self.assertEqual(self.state()["phase"], "pending")

    def test_launcher_owns_the_worker_and_restarts_both_after_its_death(self):
        self.baseline()
        worker = Path(self.tmp.name) / "worker-dies-once"
        worker.write_text('''#!/usr/bin/python3
import os, sys
fd, root = int(sys.argv[2]), sys.argv[4]
with open(root + "/worker-parents", "a") as output:
    output.write(str(os.getppid()) + "\\n")
def line():
    data = b""
    while not data.endswith(b"\\n"):
        chunk = os.read(fd, 1)
        if not chunk: return b""
        data += chunk
    return data
if line() != b"HELLO 1\\n": sys.exit(1)
os.write(fd, b"HELLO\\t1\\n")
if not os.path.exists(root + "/worker-died"):
    open(root + "/worker-died", "w").close()
    sys.exit(3)
open(root + "/worker-started", "w").close()
while line():
    os.write(fd, b"APPS\\t0\\n")
open(root + "/worker-stopped", "w").close()
''')
        worker.chmod(0o700)
        process = self.launch(worker)
        try:
            self.wait_for(lambda: (self.root / "worker-started").exists())
            parents = (self.root / "worker-parents").read_text().split()
            self.assertEqual(parents, [str(process.pid)] * 2)
            self.assertIsNone(process.poll())
        finally:
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=10)
        self.wait_for(lambda: (self.root / "worker-stopped").exists())

    def test_launcher_restores_idrive_after_a_gui_crash(self):
        from fake_hold import FakeHold
        from test_layer_manager import fake_layer_manager
        self.baseline()
        lm = fake_layer_manager(self.tmp.name, bound_after=1)
        state = Path(lm["NBT_FAKE_LM_STATE"])
        calls = Path(lm["NBT_FAKE_LM_LOG"])
        hold = FakeHold(self.tmp.name).start()
        self.addCleanup(hold.stop)
        env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root), NBT_TEST_MANAGER_GUI=str(GUI),
                   NBT_TEST_MANAGER_WORKER=str(self.worker(True)), **lm, **hold.env)
        process = subprocess.Popen([LAUNCHER, "--open"], env=env, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
        try:
            self.wait_for(lambda: json.loads(state.read_text())["order"] == [1290])
            gui = next(int(line.split()[0]) for line in calls.read_text().splitlines()
                       if line.endswith("render order 0x50a"))
            self.assertNotEqual(gui, process.pid)
            os.kill(gui, signal.SIGKILL)
            # The launcher, not the dead GUI, gives iDrive the panel back.
            self.wait_for(lambda: "%d set layer 0x80000000 render order 0x100" % process.pid
                          in calls.read_text())
        finally:
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=15)

    def test_candidate_worker_failure_rolls_back(self):
        self.baseline()
        self.upload(2)
        self.assertEqual(self.stage("update").returncode, 0)
        process = self.launch(self.worker(False))
        self.assertNotEqual(process.wait(timeout=12), 0)
        state = self.state()
        self.assertEqual((state["phase"], state["slot"], int(state["sequence"])), ("stable", "0", 1))

    def test_restart_limit_and_reset_survive_launcher_restarts(self):
        self.baseline()
        for index in range(3):
            result = run(STORE, self.root, "restart", 1, 1000 + index)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(int(self.state()["restarts"]), index + 1)
        self.assertEqual(run(STORE, self.root, "restart", 1, 1003).returncode, 1)
        self.assertEqual(int(self.state()["restarts"]), 3)
        self.assertEqual(run(STORE, self.root, "clear-restarts", 1).returncode, 0)
        self.assertEqual(int(self.state()["restarts"]), 0)
        self.assertEqual(run(STORE, self.root, "restart", 1, 1004).returncode, 0)
        self.assertEqual(int(self.state()["restarts"]), 1)

    def test_gui_closing_its_pipe_just_before_exit_is_a_close(self):
        # A GUI the user closes drops its heartbeat pipe and lease a moment
        # before it can be reaped; dev32 took that for a crash and restarted.
        self.baseline()
        gui = Path(self.tmp.name) / "gui-closes"
        gui.write_text('''#!/usr/bin/python3
import fcntl, os, sys, time
root = sys.argv[sys.argv.index("--test-root") + 1]
with open(root + "/gui-starts", "a") as output:
    output.write("start\\n")
fcntl.lockf(4, fcntl.LOCK_SH | fcntl.LOCK_NB)
for _ in range(5):
    os.write(3, b"R")
    time.sleep(0.2)
os.close(3)
os.close(4)
time.sleep(0.3)
''')
        gui.chmod(0o700)
        process = self.launch(self.worker(True), gui)
        try:
            self.assertEqual(process.wait(timeout=15), 0)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
        self.assertEqual((self.root / "gui-starts").read_text(), "start\n")  # no restart

    def test_interrupted_trial_rolls_back_before_launch(self):
        self.baseline()
        self.upload(2)
        self.assertEqual(self.stage("update").returncode, 0)
        self.assertEqual(run(STORE, self.root, "trial", 1).returncode, 0)
        process = self.launch(self.worker(True))
        try:
            self.wait_for(lambda: (self.root / "worker-started").exists())
            state = self.state()
            self.assertEqual((state["phase"], state["slot"], int(state["sequence"])), ("stable", "0", 1))
        finally:
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=10)
            self.wait_for(lambda: (self.root / "worker-stopped").exists())

    def test_killed_supervisor_rolls_back_on_next_launch(self):
        self.baseline()
        self.upload(2)
        self.assertEqual(self.stage("update").returncode, 0)
        worker = self.worker(True)
        first = self.launch(worker)
        try:
            self.wait_for(lambda: (self.root / "worker-started").exists())
            self.assertEqual(self.state()["phase"], "trial")
            duplicate = self.launch(worker)
            self.assertNotEqual(duplicate.wait(timeout=8), 0)
            first.kill()
            first.wait(timeout=10)
            self.wait_for(lambda: (self.root / "worker-stopped").exists())
        finally:
            if first.poll() is None:
                first.kill()
                first.wait(timeout=10)
        (self.root / "worker-started").unlink()
        (self.root / "worker-stopped").unlink()
        second = self.launch(worker)
        try:
            self.wait_for(lambda: (self.root / "worker-started").exists())
            self.wait_for(lambda: self.state()["phase"] == "stable")
            state = self.state()
            self.assertEqual((state["phase"], state["slot"], int(state["sequence"])), ("stable", "0", 1))
        finally:
            second.send_signal(signal.SIGTERM)
            second.wait(timeout=10)
            self.wait_for(lambda: (self.root / "worker-stopped").exists())

    def test_healthy_candidate_confirms_after_ten_seconds(self):
        self.baseline()
        self.upload(2)
        self.assertEqual(self.stage("update").returncode, 0)
        started = time.monotonic()
        process = self.launch(self.worker(True))
        try:
            self.wait_for(lambda: (self.root / "worker-started").exists())
            self.assertEqual(self.state()["phase"], "trial")
            self.wait_for(lambda: self.state()["phase"] == "stable", timeout=18)
            self.assertGreaterEqual(time.monotonic() - started, 10)
            state = self.state()
            self.assertEqual((state["slot"], int(state["sequence"]), int(state["highwater"])), ("1", 2, 2))
        finally:
            if process.poll() is None:
                process.send_signal(signal.SIGTERM)
            process.wait(timeout=10)
            self.wait_for(lambda: (self.root / "worker-stopped").exists())

    def receive(self, data, size=None, digest=None, error=None):
        env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root))
        if error:
            env["NBT_MANAGER_RECEIVE_ERROR"] = error
        size = len(data) if size is None else size
        digest = hashlib.sha256(data).hexdigest() if digest is None else digest
        return subprocess.run([LAUNCHER, "--stage-update", str(size), digest], input=data,
                              env=env, capture_output=True, timeout=15)

    def test_received_update_is_verified_before_replacing_upload(self):
        self.baseline()
        original = (self.root / "state/manager/release.upload").read_bytes()
        good = (self.path / "release-2").read_bytes()
        good_hash = hashlib.sha256(good).hexdigest()
        changed = bytearray(good)
        changed[-1] ^= 1
        for data, size, digest in ((good[:-1], len(good), good_hash),
                                   (good + b"x", len(good), good_hash),
                                   (bytes(changed), len(good), good_hash),
                                   (good, len(good), "0" * 64)):
            with self.subTest(size=len(data), digest=digest[:8]):
                self.assertNotEqual(self.receive(data, size, digest).returncode, 0)
                self.assertEqual((self.root / "state/manager/release.upload").read_bytes(), original)
                self.assertEqual(self.state()["phase"], "stable")
        result = self.receive(good)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, b"PENDING\n")
        self.assertEqual((self.root / "state/manager/release.upload").read_bytes(), good)
        self.assertEqual(self.state()["phase"], "pending")

    def test_update_replaces_a_candidate_that_never_confirmed(self):
        # dev37 in the car: an update staged but never opened blocked the next
        # one ("manager state requires recovery"). Both a staged candidate and
        # a trial cut short give way to the newer upload.
        self.baseline()
        good = (self.path / "release-2").read_bytes()
        for phase in ("pending", "trial"):
            with self.subTest(phase=phase):
                self.assertEqual(self.receive(good).returncode, 0)
                if phase == "trial":
                    self.assertEqual(run(STORE, self.root, "trial", 1).returncode, 0)
                self.assertEqual(self.state()["phase"], phase)
                result = self.receive(good)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(result.stdout, b"PENDING\n")
                state = self.state()
                self.assertEqual((state["phase"], state["slot"], int(state["sequence"])), ("pending", "1", 2))
                self.assertEqual(int(state["highwater"]), 1)

    def test_update_keeps_the_candidate_when_the_known_good_slot_is_damaged(self):
        self.baseline()
        good = (self.path / "release-2").read_bytes()
        self.assertEqual(self.receive(good).returncode, 0)
        gui = self.root / "manager/slots/0/gui"
        gui.write_bytes(gui.read_bytes() + b"x")
        result = self.receive(good)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn(b"manager state requires recovery", result.stderr)
        self.assertEqual(self.state()["phase"], "pending")

    def test_receive_rejects_unsafe_temporary_path(self):
        self.baseline()
        data = (self.path / "release-2").read_bytes()
        (self.root / "state/manager/release.upload.next").symlink_to(self.victim)
        self.assertNotEqual(self.receive(data).returncode, 0)
        self.assertEqual(self.victim.read_text(), "unchanged")
        self.assertEqual(self.state()["phase"], "stable")

    def test_receive_write_flush_and_rename_failures_do_not_activate(self):
        self.baseline()
        data = (self.path / "release-2").read_bytes()
        upload = self.root / "state/manager/release.upload"
        for error in ("write", "flush", "rename", "barrier"):
            with self.subTest(error=error):
                previous = upload.read_bytes()
                self.assertNotEqual(self.receive(data, error=error).returncode, 0)
                if error != "barrier":
                    self.assertEqual(upload.read_bytes(), previous)
                self.assertEqual(self.state()["phase"], "stable")
                self.assertEqual((self.root / "manager/slots/0/gui").read_bytes(), (self.path / "gui-1").read_bytes())
        self.assertEqual(self.receive(data).returncode, 0)

    def test_installer_update_stage_refresh_then_open(self):
        from fake_hold import FakeHold
        from test_layer_manager import fake_layer_manager
        self.baseline()
        launcher = self.root / "manager/nbt-manager-launcher"
        shutil.copyfile(self.path / "launcher", launcher)
        launcher.chmod(0o700)
        hmi = Path(self.tmp.name) / "hmi-ro"
        (hmi / "Plugins/NbtManager").mkdir(parents=True, mode=0o755)
        for d in (hmi, hmi / "Plugins", hmi / "Plugins/NbtManager"):
            d.chmod(0o755)
        lm = fake_layer_manager(self.tmp.name, bound_after=1)
        hold = FakeHold(self.tmp.name).start()
        self.addCleanup(hold.stop)
        env = dict(os.environ, NBT_TEST_MANAGER_ROOT=str(self.root), NBT_TEST_HMI_ROOT=str(hmi),
                   NBT_TEST_MANAGER_GUI=str(GUI), NBT_TEST_MANAGER_WORKER=str(self.worker(True)),
                   NBT_TEST_CONFIRM_MS="150", **lm, **hold.env)
        initial_setup = run(LAUNCHER, "--refresh-setup", env=env)
        self.assertEqual((initial_setup.returncode, initial_setup.stdout), (0, b"REFRESHED plugin\n"),
                         initial_setup.stderr)
        good = self.bridge_release(2)
        self.assertEqual(self.receive(good).returncode, 0)
        refreshed = run(LAUNCHER, "--refresh-setup", env=env)
        self.assertEqual((refreshed.returncode, refreshed.stdout), (0, b"REFRESHED\n"), refreshed.stderr)
        process = subprocess.Popen([LAUNCHER, "--open"], env=env, stdout=subprocess.DEVNULL,
                                   stderr=subprocess.DEVNULL)
        try:
            state_file = Path(lm["NBT_FAKE_LM_STATE"])
            self.wait_for(lambda: state_file.exists() and json.loads(state_file.read_text())["order"] == [1290])
            self.wait_for(lambda: self.state()["phase"] == "stable" and self.state()["slot"] == "1", timeout=15)
        finally:
            process.send_signal(signal.SIGTERM)
            process.wait(timeout=15)
            self.wait_for(lambda: (self.root / "worker-stopped").exists())


if __name__ == "__main__":
    unittest.main()
