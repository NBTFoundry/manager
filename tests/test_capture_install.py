"""Transactional capture shim installation, starter.cfg splicing, fault recovery, and removal."""
import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build"))
PROBE = BUILD / "capture-install-probe"
REFRESH = BUILD / "manager-refresh-probe"
SETUP = BUILD / "manager-setup-probe"
LAUNCHER = BUILD / "launcher-host"
PACK = ROOT / "tools/package_release.py"
# A head unit's own /opt/sys/etc/starter.cfg, if one is at hand; else a stock copy.
REAL_STARTER = Path(os.environ.get("NBT_REAL_STARTER_CFG", "/nonexistent/starter.cfg"))
NAMES = ("libNbtManager.so", "NbtManager.manifest", "NbtManager.xml", "NbtManager_ALL_Texts.zip",
         "NbtManager_ALL_Images.zip")

HMI_BLOCK = (
    b"  <Process>\r\n      <Number>17</Number>\r\n"
    b"      <Name>/opt/hmi/bin/zipped/HmiMain3D</Name>\r\n      <WorkingDir/>\r\n"
    b"      <EnvFile/>\r\n      <RequiresEnvironment>6</RequiresEnvironment>\r\n"
    b"  </Process>\r\n"
)
STOCK_CFG = (
    b"<Starter>\r\n"
    b"  <Environment>\r\n"
    b"      <Number>5</Number>\r\n"
    b"      <Name>Other</Name>\r\n"
    b"      <EnvName>LD_PRELOAD</EnvName>\r\n"
    b"      <Value>other.so</Value>\r\n"
    b"      <Overwrite>1</Overwrite>\r\n"
    b"  </Environment>\r\n"
    b"  <Environment>\r\n"
    b"      <Number>6</Number>\r\n"
    b"      <Name>System_HMI_PRELOAD</Name>\r\n"
    b"      <EnvName>LD_PRELOAD</EnvName>\r\n"
    b"      <Value>libEGL_X.gpp.so:libGLESv2_X_FORWARD.gpp.so:libKD_X.gpp.so</Value>\r\n"
    b"      <Overwrite>1</Overwrite>\r\n"
    b"  </Environment>\r\n"
    b"  <Process>\r\n      <Number>16</Number>\r\n      <Name>/bin/other</Name>\r\n"
    b"      <EnvFile/>\r\n  </Process>\r\n"
    + HMI_BLOCK +
    b"</Starter>\r\n"
)
ENV_ON = b"<EnvFile>/var/opt/nbt-manager/state/manager/hmi.env</EnvFile>"
STOCK_PRELOAD = b"libEGL_X.gpp.so:libGLESv2_X_FORWARD.gpp.so:libKD_X.gpp.so"
SHIM = b"/var/opt/nbt-manager/state/manager/libnbt-capture.so"
LEGACY_SHIM = b"/opt/hmi/ID5/data/ro/NbtCapture/libnbt-capture.so"
LEGACY_CFG = STOCK_CFG.replace(b"<Value>" + STOCK_PRELOAD + b"</Value>",
                               b"<Value>" + STOCK_PRELOAD + b":" + LEGACY_SHIM + b"</Value>")


def with_env_file(cfg):
    """The install's one edit: HmiMain3D's own empty EnvFile names ours."""
    at = cfg.index(b"<Name>/opt/hmi/bin/zipped/HmiMain3D</Name>")
    start = cfg.rindex(b"<Process>", 0, at)
    elem = cfg.index(b"<EnvFile/>", start, cfg.index(b"</Process>", at))
    return cfg[:elem] + ENV_ON + cfg[elem + len(b"<EnvFile/>"):]


def run(*args, env=None):
    return subprocess.run([str(arg) for arg in args], env=env, capture_output=True, check=False)


class CaptureInstallTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assets = tempfile.TemporaryDirectory()
        cls.path = Path(cls.assets.name)
        key = cls.path / "manager.pem"
        for args in (("genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:3072", "-out", key),
                     ("pkey", "-in", key, "-pubout", "-out", cls.path / "manager.pub")):
            r = run("openssl", *args)
            if r.returncode:
                raise RuntimeError(r.stderr.decode())

        def mk_release(seq, with_capture, cap_ver=1):
            elf = bytearray(128)
            elf[:7] = b"\x7fELF\x01\x01\x01"
            elf[16:20] = b"\x02\x00\x28\x00"
            folder = cls.path / f"parts-{seq}"
            plugin = folder / "plugin"
            plugin.mkdir(parents=True)
            (folder / "launcher").write_bytes(elf)
            elf[-1] = seq
            (folder / "gui").write_bytes(elf)
            (folder / "worker").write_bytes(elf)
            elf[16] = 3
            elf[-1] = 0
            (plugin / "libNbtManager.so").write_bytes(elf)
            for name in NAMES[1:]:
                (plugin / name).write_bytes(f"{name}".encode())
            cmd = ["python3", PACK, "--version", f"v{seq}", "--sequence", seq,
                   "--compat", "demo-1", "--key", key, "--output", cls.path / f"release-{seq}",
                   "--launcher", folder / "launcher", "--gui", folder / "gui",
                   "--worker", folder / "worker", "--plugin-dir", plugin]
            if with_capture:
                cap = bytearray(256)
                cap[:7] = b"\x7fELF\x01\x01\x01"
                cap[16:20] = b"\x03\x00\x28\x00"
                cap[-1] = cap_ver
                (folder / "cap.so").write_bytes(cap)
                cmd.extend(["--capture-lib", folder / "cap.so"])
            r = run(*cmd)
            if r.returncode:
                raise RuntimeError(r.stderr.decode())

        mk_release(1, False)
        mk_release(2, True, cap_ver=1)
        mk_release(3, True, cap_ver=2)

    @classmethod
    def tearDownClass(cls):
        cls.assets.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.base = Path(self.tmp.name)
        self.root = self.base / "managed"
        self.root.mkdir(mode=0o700)
        self.hmi = self.base / "hmi"
        self.hmi.mkdir(mode=0o755)
        self.etc = self.base / "etc"
        self.etc.mkdir(mode=0o755)
        self.cfg = self.etc / "starter.cfg"
        self.cfg.write_bytes(STOCK_CFG)
        self.cfg.chmod(0o664)
        self.pub = self.path / "manager.pub"

    def tearDown(self):
        self.tmp.cleanup()

    def shim(self):
        return self.root / "state/manager/libnbt-capture.so"

    def env(self, **extra):
        out = dict(os.environ, NBT_TEST_HMI_ROOT=str(self.hmi),
                   NBT_TEST_MANAGER_ROOT=str(self.root), NBT_TEST_STARTER_CFG=str(self.cfg),
                   NBT_TEST_HMI_TRUSTED_GID=str(os.getgid()))
        out.update(extra)
        return out

    def setup_fs(self, seq):
        r = run(SETUP, self.root, self.path / f"release-{seq}", self.pub, "demo-1", env=self.env())
        self.assertEqual(r.returncode, 0, r.stderr)

    def test_starter_cfg_preserves_exact_crlf_and_permissions(self):
        self.setup_fs(2)
        src = REAL_STARTER.read_bytes() if REAL_STARTER.is_file() else STOCK_CFG
        self.cfg.write_bytes(src)
        self.cfg.chmod(0o664)
        r = run(PROBE, "install", self.root, self.hmi, self.cfg,
                self.path / "release-2", self.pub, "demo-1", env=self.env())
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("changed=1", r.stdout.decode())
        self.assertEqual(r.stdout.decode().splitlines(), ["remount rw", "remount ro", "changed=1"])
        updated = self.cfg.read_bytes()
        self.assertEqual(updated, with_env_file(src))
        self.assertEqual(len(updated) - len(src), len(ENV_ON) - len(b"<EnvFile/>"))
        self.assertEqual(self.cfg.stat().st_mode & 0o777, 0o664)
        self.assertEqual(self.shim().stat().st_mode & 0o777, 0o700)
        self.assertEqual(run(PROBE, "inspect", self.cfg, env=self.env()).stdout.decode().strip(), "with-shim")

        # Idempotent re-run performs zero mutations or remounts.
        r2 = run(PROBE, "install", self.root, self.hmi, self.cfg,
                 self.path / "release-2", self.pub, "demo-1", env=self.env())
        self.assertEqual(r2.returncode, 0, r2.stderr)
        self.assertEqual(r2.stdout.decode().strip(), "changed=0")

        # Upgrade v2 -> v3 replaces shim binary cleanly while keeping original backup intact.
        r3 = run(PROBE, "install", self.root, self.hmi, self.cfg,
                 self.path / "release-3", self.pub, "demo-1", env=self.env())
        self.assertEqual(r3.returncode, 0, r3.stderr)
        self.assertIn("changed=1", r3.stdout.decode())
        self.assertEqual((self.root / "state/manager/starter.cfg.orig").read_bytes(), src)
        self.assertEqual(self.shim().read_bytes()[-1], 2)

        # Remove restores byte-for-byte original and removes NbtCapture directory.
        r4 = run(PROBE, "remove", self.root, self.hmi, self.cfg, env=self.env())
        self.assertEqual(r4.returncode, 0, r4.stderr)
        self.assertEqual(self.cfg.read_bytes(), src)
        self.assertEqual(self.cfg.stat().st_mode & 0o777, 0o664)
        self.assertFalse(self.shim().exists() or (self.hmi / "NbtCapture").exists())
        self.assertFalse((self.root / "state/manager/starter.cfg.orig").exists())
        self.assertFalse((self.root / "state/manager/capture.tx").exists())

    def test_hardlinks_symlinks_and_ambiguous_configs_rejected(self):
        self.setup_fs(2)
        # Hardlink nlink == 2 allowed; nlink == 3 rejected.
        link2 = self.etc / "starter.cfg.hl2"
        os.link(self.cfg, link2)
        self.assertEqual(run(PROBE, "inspect", self.cfg, env=self.env()).returncode, 0)
        link3 = self.etc / "starter.cfg.hl3"
        os.link(self.cfg, link3)
        r = run(PROBE, "install", self.root, self.hmi, self.cfg,
                self.path / "release-2", self.pub, "demo-1", env=self.env())
        self.assertNotEqual(r.returncode, 0)
        link2.unlink()
        link3.unlink()

        # Symlink starter.cfg rejected.
        real = self.etc / "real.cfg"
        self.cfg.rename(real)
        self.cfg.symlink_to(real)
        self.assertNotEqual(run(PROBE, "install", self.root, self.hmi, self.cfg,
                                self.path / "release-2", self.pub, "demo-1", env=self.env()).returncode, 0)
        self.cfg.unlink()
        real.rename(self.cfg)

        # Symlink NbtCapture rejected.
        decoy = self.base / "decoy"
        decoy.mkdir()
        (self.hmi / "NbtCapture").symlink_to(decoy)
        self.assertNotEqual(run(PROBE, "install", self.root, self.hmi, self.cfg,
                                self.path / "release-2", self.pub, "demo-1", env=self.env()).returncode, 0)
        (self.hmi / "NbtCapture").unlink()

        # Ambiguous duplicate Environment 6 rejected.
        self.cfg.write_bytes(STOCK_CFG.replace(b"</Starter>",
                                               b"  <Environment><Number>6</Number></Environment>\r\n</Starter>"))
        self.assertNotEqual(run(PROBE, "install", self.root, self.hmi, self.cfg,
                                self.path / "release-2", self.pub, "demo-1", env=self.env()).returncode, 0)

        # Leftover cfg .next temporary without tx file rejected.
        self.cfg.write_bytes(STOCK_CFG)
        (self.etc / ".starter.cfg.next").write_bytes(b"junk")
        self.assertNotEqual(run(PROBE, "install", self.root, self.hmi, self.cfg,
                                self.path / "release-2", self.pub, "demo-1", env=self.env()).returncode, 0)

    def test_fault_injection_install_and_remove_recovery(self):
        for fault in ("enospc", "short-shim-write", "sync-shim", "before-shim-rename",
                      "after-shim-rename", "short-cfg-write", "sync-cfg", "before-cfg-rename"):
            with self.subTest(fault=fault):
                self.tearDown()
                self.setUp()
                self.setup_fs(2)
                r = run(PROBE, "install", self.root, self.hmi, self.cfg,
                        self.path / "release-2", self.pub, "demo-1",
                        env=self.env(NBT_TEST_CAPTURE_FAULT=fault))
                self.assertNotEqual(r.returncode, 0, fault)
                out = r.stdout.decode()
                if fault.startswith(("before-", "after-")):
                    self.assertEqual(r.returncode, 91, fault)
                if "cfg" in fault:  # starter.cfg: /fs/sda0, writable only for it
                    self.assertIn("remount rw", out)
                    self.assertEqual("remount ro" in out, not fault.startswith("before-"))
                else:  # the shim is written next to the environment file first
                    self.assertNotIn("remount", out)
                    self.assertFalse((self.root / "state/manager/hmi.env").exists())
                # Recovery install without fault succeeds cleanly and leaves final state valid.
                r_rec = run(PROBE, "install", self.root, self.hmi, self.cfg,
                            self.path / "release-2", self.pub, "demo-1", env=self.env())
                self.assertEqual(r_rec.returncode, 0, f"{fault}: {r_rec.stderr.decode()}")
                self.assertEqual(run(PROBE, "inspect", self.cfg, env=self.env()).stdout.decode().strip(), "with-shim")
                self.assertFalse((self.root / "state/manager/capture.tx").exists())

        for fault in ("short-restore-cfg", "sync-restore-cfg", "before-restore-rename",
                      "before-remove-shim"):
            with self.subTest(rm_fault=fault):
                self.tearDown()
                self.setUp()
                self.setup_fs(2)
                self.assertEqual(run(PROBE, "install", self.root, self.hmi, self.cfg,
                                     self.path / "release-2", self.pub, "demo-1", env=self.env()).returncode, 0)
                r = run(PROBE, "remove", self.root, self.hmi, self.cfg,
                        env=self.env(NBT_TEST_CAPTURE_FAULT=fault))
                self.assertNotEqual(r.returncode, 0, fault)
                if fault.startswith(("before-", "after-")):
                    self.assertEqual(r.returncode, 91, fault)
                    self.assertIn("remount rw", r.stdout.decode())
                else:
                    self.assertIn("remount rw", r.stdout.decode())
                    self.assertIn("remount ro", r.stdout.decode())
                if fault != "before-remove-shim":
                    # Configuration restoration failed before completion -> shim MUST remain retained.
                    self.assertTrue(self.shim().exists())
                # Recovery removal completes cleanly.
                r_rec = run(PROBE, "remove", self.root, self.hmi, self.cfg, env=self.env())
                self.assertEqual(r_rec.returncode, 0, f"{fault}: {r_rec.stderr.decode()}")
                self.assertEqual(self.cfg.read_bytes(), STOCK_CFG)
                self.assertFalse(self.shim().exists() or (self.hmi / "NbtCapture").exists())

    def test_refuses_auto_restore_when_starter_modified_externally(self):
        self.setup_fs(2)
        self.assertEqual(run(PROBE, "install", self.root, self.hmi, self.cfg,
                             self.path / "release-2", self.pub, "demo-1", env=self.env()).returncode, 0)
        edited = self.cfg.read_bytes().replace(b"<Name>Other</Name>", b"<Name>Custom</Name>")
        self.cfg.write_bytes(edited)
        r = run(PROBE, "remove", self.root, self.hmi, self.cfg, env=self.env())
        self.assertNotEqual(r.returncode, 0)
        self.assertIn("changed outside capture preload", r.stderr.decode())
        self.assertTrue(self.shim().exists())
        self.assertTrue((self.root / "state/manager/starter.cfg.orig").exists())
        self.assertEqual(self.cfg.read_bytes(), edited)

    def test_lost_backup_is_rebuilt_exactly(self):
        # An older installer's Remove deletes the manager root, backup included,
        # but leaves the shim and the edited starter.cfg (iDrive keeps starting).
        self.setup_fs(2)
        self.assertEqual(run(PROBE, "install", self.root, self.hmi, self.cfg,
                             self.path / "release-2", self.pub, "demo-1", env=self.env()).returncode, 0)
        installed = self.cfg.read_bytes()
        (self.root / "state/manager/starter.cfg.orig").unlink()

        # Install again: no refusal, the backup comes back as the exact stock file.
        r = run(PROBE, "install", self.root, self.hmi, self.cfg,
                self.path / "release-2", self.pub, "demo-1", env=self.env())
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual((self.root / "state/manager/starter.cfg.orig").read_bytes(), STOCK_CFG)
        self.assertEqual(self.cfg.read_bytes(), installed)

        # Remove with the backup gone again: starter.cfg is restored byte for byte first.
        (self.root / "state/manager/starter.cfg.orig").unlink()
        r = run(PROBE, "remove", self.root, self.hmi, self.cfg, env=self.env())
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(self.cfg.read_bytes(), STOCK_CFG)
        self.assertFalse(self.shim().exists() or (self.hmi / "NbtCapture").exists())

        # Anything but our exact entry is still refused, shim kept.
        self.assertEqual(run(PROBE, "install", self.root, self.hmi, self.cfg,
                             self.path / "release-2", self.pub, "demo-1", env=self.env()).returncode, 0)
        (self.root / "state/manager/starter.cfg.orig").unlink()
        self.cfg.write_bytes(self.cfg.read_bytes().replace(b"hmi.env</EnvFile>", b"hmi.env2</EnvFile>"))
        r = run(PROBE, "remove", self.root, self.hmi, self.cfg, env=self.env())
        self.assertNotEqual(r.returncode, 0)
        self.assertTrue(self.shim().exists())

    def test_older_install_in_environment_6_is_moved_or_removed(self):
        # dev91-dev99 listed the shim in Environment 6; it is recognised, so an
        # Update moves it to the environment file and Remove restores stock.
        self.setup_fs(2)
        self.cfg.write_bytes(LEGACY_CFG)
        old = self.hmi / "NbtCapture"
        old.mkdir()
        (old / "libnbt-capture.so").write_bytes(b"old shim")
        self.assertEqual(run(PROBE, "inspect", self.cfg, env=self.env()).stdout.strip(), b"legacy")
        r = run(PROBE, "install", self.root, self.hmi, self.cfg, self.path / "release-2", self.pub,
                "demo-1", env=self.env())
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(self.cfg.read_bytes(), with_env_file(STOCK_CFG))
        self.assertEqual((self.root / "state/manager/starter.cfg.orig").read_bytes(), STOCK_CFG)
        # The shim moved next to the environment file, which arms it.
        self.assertFalse(old.exists())
        self.assertEqual(self.shim().read_bytes(), (self.path / "parts-2/cap.so").read_bytes())
        self.assertIn(SHIM, (self.root / "state/manager/hmi.env").read_bytes())
        # Removing an older install restores stock and clears the old folder.
        self.cfg.write_bytes(LEGACY_CFG)
        old.mkdir()
        (old / "libnbt-capture.so").write_bytes(b"old shim")
        r = run(PROBE, "remove", self.root, self.hmi, self.cfg, env=self.env())
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(self.cfg.read_bytes(), STOCK_CFG)
        self.assertFalse(old.exists() or self.shim().exists())
        # Anything else in the old folder is not ours: left alone, refused.
        old.mkdir()
        (old / "other").write_bytes(b"x")
        self.cfg.write_bytes(LEGACY_CFG)
        self.assertNotEqual(run(PROBE, "remove", self.root, self.hmi, self.cfg, env=self.env()).returncode, 0)
        self.assertTrue((old / "other").exists())
        (old / "other").unlink()
        old.rmdir()
        # Both forms at once is nobody's install: refused, nothing changed.
        both = with_env_file(LEGACY_CFG)
        self.cfg.write_bytes(both)
        self.assertEqual(run(PROBE, "inspect", self.cfg, env=self.env()).stdout.strip(), b"invalid")
        self.assertNotEqual(run(PROBE, "remove", self.root, self.hmi, self.cfg, env=self.env()).returncode, 0)
        self.assertEqual(self.cfg.read_bytes(), both)

    def test_shim_is_armed_and_kept_automatically(self):
        self.setup_fs(2)
        state = self.root / "state/manager"
        env_file, record = state / "hmi.env", state / "shim-state"
        launcher = lambda *args: run(LAUNCHER, *args, env=self.env())
        check = lambda: launcher("--shim-check")
        r = launcher("--shim", "on")
        self.assertEqual(r.returncode, 1)
        self.assertIn(b"no shim installed", r.stdout)
        self.assertEqual(check().stdout, b"SHIM not installed\n")
        # Install arms the new shim for iDrive's next start.
        self.assertEqual(run(PROBE, "install", self.root, self.hmi, self.cfg, self.path / "release-2",
                             self.pub, "demo-1", env=self.env()).returncode, 0)
        sha = hashlib.sha256(self.shim().read_bytes()).hexdigest()
        line = b"LD_PRELOAD=" + STOCK_PRELOAD + b":" + SHIM + b"\n"
        self.assertEqual(env_file.read_bytes(), line)
        self.assertEqual(record.read_text(), f"v2\nstate=armed\nstrikes=0\nshim={sha}\n")
        # Here iDrive never has the shim mapped. File still there: not read yet.
        r = check()
        self.assertEqual(r.returncode, 0, r.stdout)
        self.assertIn(b"has not started with it yet", r.stdout)
        self.assertTrue(env_file.exists())
        # A start consumed it and did not reach the check: once more, then off.
        env_file.unlink()
        self.assertIn(b"trying it once more", check().stdout)
        self.assertEqual(env_file.read_bytes(), line)
        self.assertEqual(record.read_text(), f"v2\nstate=armed\nstrikes=1\nshim={sha}\n")
        env_file.unlink()
        self.assertIn(b"two starts with the shim ended before 60 s; shim off", check().stdout)
        self.assertFalse(env_file.exists())
        self.assertIn("state=failed", record.read_text())
        self.assertIn(b"shim off after failed starts", check().stdout)
        self.assertFalse(env_file.exists())
        # On by hand: armed again with no strikes; off by hand: stays off.
        self.assertEqual(launcher("--shim", "on").returncode, 0)
        self.assertEqual(record.read_text(), f"v2\nstate=armed\nstrikes=0\nshim={sha}\n")
        self.assertTrue(env_file.exists())
        self.assertEqual(launcher("--shim", "off").stdout,
                         b"SHIM OFF iDrive starts without the shim from its next start\n")
        self.assertFalse(env_file.exists())
        self.assertIn(b"shim turned off", check().stdout)
        self.assertFalse(env_file.exists())
        # A trial changes nothing while iDrive is not running (here, never).
        r = launcher("--try-shim")
        self.assertEqual(r.returncode, 1)
        self.assertEqual(r.stdout, b"SHIM_TRIAL FAILED iDrive is not running (nothing changed)\n")
        self.assertFalse(env_file.exists())
        # Reinstalling the same shim keeps its state; a new shim is armed.
        self.assertEqual(run(PROBE, "install", self.root, self.hmi, self.cfg, self.path / "release-2",
                             self.pub, "demo-1", env=self.env()).returncode, 0)
        self.assertIn("state=off", record.read_text())
        self.assertEqual(run(PROBE, "install", self.root, self.hmi, self.cfg, self.path / "release-3",
                             self.pub, "demo-1", env=self.env()).returncode, 0)
        new = hashlib.sha256(self.shim().read_bytes()).hexdigest()
        self.assertEqual(record.read_text(), f"v2\nstate=armed\nstrikes=0\nshim={new}\n")
        self.assertTrue(env_file.exists())
        # A malformed record is replaced, never trusted.
        record.write_text("v2\nstate=kept\nstrikes=0\nshim=zz\n")
        record.chmod(0o600)
        env_file.unlink()
        self.assertIn(b"shim armed for iDrive's next start", check().stdout)
        self.assertTrue(env_file.exists())
        # Remove deletes the file and the record first.
        r = run(PROBE, "remove", self.root, self.hmi, self.cfg, env=self.env())
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertFalse(env_file.exists())
        self.assertFalse(record.exists())
        self.assertEqual(self.cfg.read_bytes(), STOCK_CFG)

    def script(self, name, body):
        path = self.base / name
        path.write_text("#!/bin/sh\n" + body)
        path.chmod(0o755)
        return path

    def test_load_test_uses_the_real_loader(self):
        # The host loader stands in for QNX's: an unresolved symbol under
        # LD_BIND_NOW stops the program before main (exit 127 on glibc).
        self.setup_fs(2)
        libs = {}
        for name, source in (("good", "int nbt_probe_ok(void) { return 1; }\n"),
                             ("bad", "extern int nbt_probe_missing(void);\n"
                                     "int nbt_probe_bad(void) { return nbt_probe_missing(); }\n")):
            (self.base / f"{name}.c").write_text(source)
            libs[name] = self.base / f"lib{name}.so"
            r = run("cc", "-shared", "-fPIC", "-o", libs[name], self.base / f"{name}.c")
            self.assertEqual(r.returncode, 0, r.stderr)
        copy = self.root / "state/manager/capture-probe.so"
        ok = run(PROBE, "load-test", self.root, libs["good"], env=self.env(NBT_TEST_PRELOAD_PROBE="/bin/true"))
        self.assertEqual(ok.returncode, 0, ok.stderr)
        self.assertTrue(ok.stdout.startswith(b"loaded: shim loaded after iDrive's preloads, all bound"), ok.stdout)
        bad = run(PROBE, "load-test", self.root, libs["bad"], env=self.env(NBT_TEST_PRELOAD_PROBE="/bin/true"))
        self.assertEqual(bad.returncode, 1)
        self.assertIn(b"exit status 127", bad.stderr)
        # A program that fails on its own proves nothing: refused before the shim.
        alone = run(PROBE, "load-test", self.root, libs["good"], env=self.env(NBT_TEST_PRELOAD_PROBE="/bin/false"))
        self.assertEqual(alone.returncode, 1)
        self.assertIn(b"exit status 1", alone.stderr)
        self.assertFalse(copy.exists())

    def test_load_test_uses_idrive_environment_and_preload_order(self):
        # HmiMain3D's Process block and the Environment entries it requires, as
        # in the car's starter.cfg; other processes' entries must not leak in.
        cfg = STOCK_CFG.replace(HMI_BLOCK, b"").replace(b"</Starter>\r\n", (
            b"  <Environment>\r\n      <Number>4</Number>\r\n      <Name>Lib</Name>\r\n"
            b"      <EnvName>LD_LIBRARY_PATH</EnvName>\r\n      <Value>/opt/hmi/lib:/lib</Value>\r\n"
            b"  </Environment>\r\n"
            b"  <Environment>\r\n      <Number>3</Number>\r\n      <Name>Gfx</Name>\r\n"
            b"      <EnvName>GRAPHICS_ROOT</EnvName>\r\n      <Value>/lib/graphics/omap5430</Value>\r\n"
            b"  </Environment>\r\n"
            b"  <Process>\r\n      <Number>18</Number>\r\n      <Name>/bin/more</Name>\r\n"
            b"      <RequiresEnvironment>5</RequiresEnvironment>\r\n  </Process>\r\n"
            b"  <Process>\r\n      <Number>17</Number>\r\n"
            b"      <Name>/opt/hmi/bin/zipped/HmiMain3D</Name>\r\n"
            b"      <RequiresEnvironment>3</RequiresEnvironment>\r\n"
            b"      <RequiresEnvironment>6</RequiresEnvironment>\r\n"
            b"      <RequiresEnvironment>4</RequiresEnvironment>\r\n  </Process>\r\n</Starter>\r\n"))
        cfg_path = self.base / "hmi-starter.cfg"
        cfg_path.write_bytes(cfg)
        self.setup_fs(2)
        lib = self.base / "libgood.so"
        (self.base / "good.c").write_text("int nbt_probe_ok(void) { return 1; }\n")
        self.assertEqual(run("cc", "-shared", "-fPIC", "-o", lib, self.base / "good.c").returncode, 0)
        stock = "libEGL_X.gpp.so:libGLESv2_X_FORWARD.gpp.so:libKD_X.gpp.so"
        copy = self.root / "state/manager/capture-probe.so"
        log = self.base / "runs.log"
        # Records each run; iDrive's list "fails" when every symbol is bound.
        recorder = self.script("record.sh",
                               f'echo "$LD_PRELOAD|$LD_BIND_NOW|$GRAPHICS_ROOT|$LD_LIBRARY_PATH" >> "{log}"\n'
                               f'[ "$LD_PRELOAD|$LD_BIND_NOW" = "{stock}|1" ] && [ -n "$FAIL_BOUND" ] && exit 1\n'
                               f'[ "$LD_PRELOAD" = "{stock}" ] && [ -n "$FAIL_LIST" ] && exit 1\n'
                               'exit 0\n')
        env = self.env(NBT_TEST_PRELOAD_PROBE=str(recorder), GRAPHICS_ROOT="/inherited",
                       LD_PRELOAD="/inherited.so")
        hmi = "|/lib/graphics/omap5430|/opt/hmi/lib:/lib"
        r = run(PROBE, "load-test", self.root, lib, cfg_path, env=env)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn(b"after iDrive's preloads, all bound, HmiMain3D env", r.stdout)
        self.assertEqual(log.read_text().splitlines(),
                         [f"|{hmi}", f"{stock}|1{hmi}", f"{stock}:{copy}|1{hmi}"])
        log.unlink()
        r = run(PROBE, "load-test", self.root, lib, cfg_path, env=dict(env, FAIL_BOUND="1"))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn(b"after iDrive's preloads lazily, alone all bound, HmiMain3D env", r.stdout)
        self.assertEqual(log.read_text().splitlines(),
                         [f"|{hmi}", f"{stock}|1{hmi}", f"{stock}|{hmi}", f"{stock}:{copy}|{hmi}",
                          f"{copy}|1{hmi}"])
        log.unlink()
        r = run(PROBE, "load-test", self.root, lib, cfg_path, env=dict(env, FAIL_BOUND="1", FAIL_LIST="1"))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn(b"alone, all bound (iDrive's preloads do not load here)", r.stdout)
        log.unlink()
        # Without a usable HmiMain3D block, the caller's environment (still no inherited preload).
        no_hmi = self.base / "no-hmi.cfg"
        no_hmi.write_bytes(STOCK_CFG.replace(HMI_BLOCK, b""))
        r = run(PROBE, "load-test", self.root, lib, no_hmi, env=env)
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn(b"caller env", r.stdout)
        self.assertEqual(log.read_text().splitlines()[0], "||/inherited|" + os.environ.get("LD_LIBRARY_PATH", ""))
        self.assertFalse(copy.exists())

    def test_install_runs_the_live_selftest(self):
        self.setup_fs(2)
        env = self.env(NBT_TEST_PRELOAD_PROBE="/bin/true", NBT_TEST_CAPTURE_SELFTEST=str(LAUNCHER))
        ok = run(PROBE, "load-test", self.root, BUILD / "libnbt-capture-observer.so", env=env)
        self.assertEqual(ok.returncode, 0, ok.stderr)
        self.assertIn(b"; live test OK observer", ok.stdout)
        active = run(PROBE, "load-test", self.root, BUILD / "libnbt-capture-active.so", env=env)
        self.assertIn(b"; live test OK active", active.stdout)
        # Loadable but not a capture shim: the live test refuses it.
        bad = run(PROBE, "load-test", self.root, BUILD / "libfake_dsi_base.so", env=env)
        self.assertEqual(bad.returncode, 1)
        self.assertIn(b"live test: FAILED the shim has no self-test entry", bad.stderr)
        for name in ("capture-probe.so", "capture-selftest.control", "capture-selftest.live",
                     "capture-selftest.diag"):
            self.assertFalse((self.root / "state/manager" / name).exists(), name)

    def test_failed_load_test_changes_nothing(self):
        self.setup_fs(2)
        seen = self.base / "seen.so"
        passes = self.script("passes.sh", f'[ "$1" = --capabilities ] || exit 3\n'
                                          f'[ -z "$LD_PRELOAD" ] && exit 0\n'
                                          f'[ "$LD_BIND_NOW" = 1 ] || exit 4\n'
                                          f'cp "$LD_PRELOAD" "{seen}"\n')
        fails = self.script("fails.sh", '[ -z "$LD_PRELOAD" ]\n')
        r = run(PROBE, "install", self.root, self.hmi, self.cfg, self.path / "release-2", self.pub,
                "demo-1", env=self.env(NBT_TEST_PRELOAD_PROBE=str(fails)))
        self.assertEqual(r.returncode, 1)
        self.assertIn(b"capture shim refused, nothing changed: exit status 1", r.stderr)
        self.assertEqual(r.stdout, b"")  # no remount
        self.assertEqual(self.cfg.read_bytes(), STOCK_CFG)
        self.assertFalse(self.shim().exists() or (self.hmi / "NbtCapture").exists())
        for name in ("starter.cfg.orig", "capture.tx", "capture-probe.so"):
            self.assertFalse((self.root / "state/manager" / name).exists(), name)
        # The installer's Update: launcher and plugin still replaced, the shim
        # skipped and named on standard error.
        r = run(LAUNCHER, "--refresh-setup", env=self.env(NBT_TEST_PRELOAD_PROBE=str(fails)))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(r.stdout, b"REFRESHED plugin\n")
        self.assertIn(b"capture shim refused, nothing changed: exit status 1", r.stderr)
        self.assertEqual(self.cfg.read_bytes(), STOCK_CFG)
        self.assertFalse(self.shim().exists())
        r = run(PROBE, "install", self.root, self.hmi, self.cfg, self.path / "release-2", self.pub,
                "demo-1", env=self.env(NBT_TEST_PRELOAD_PROBE=str(passes)))
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertEqual(seen.read_bytes(), (self.path / "parts-2/cap.so").read_bytes())
        self.assertEqual(run(PROBE, "inspect", self.cfg, env=self.env()).stdout.strip(), b"with-shim")
        self.assertFalse((self.root / "state/manager/capture-probe.so").exists())

    def test_refresh_and_launcher_cli_and_operational_status(self):
        # Empty NBTRLS2 install via refresh-probe -> sets NBT_REFRESH_CAPTURE (4) | PLUGIN (2)
        self.setup_fs(2)
        r = run(REFRESH, self.root, self.hmi, env=self.env())
        self.assertEqual(r.returncode, 0, r.stderr)
        self.assertIn("changed=6", r.stdout.decode())
        self.assertEqual(run(PROBE, "inspect", self.cfg, env=self.env()).stdout.decode().strip(), "with-shim")

        # Staged launcher --refresh-setup is idempotent after refresh.
        self.assertEqual(run(LAUNCHER, "--refresh-setup", env=self.env()).stdout.decode().strip(),
                         "REFRESHED")
        # Refreshing against NBTRLS1 removes capture cleanly.
        r_down = run(PROBE, "install", self.root, self.hmi, self.cfg,
                     self.path / "release-1", self.pub, "demo-1", env=self.env())
        self.assertEqual(r_down.returncode, 0, r_down.stderr)
        self.assertEqual(self.cfg.read_bytes(), STOCK_CFG)
        self.assertFalse(self.shim().exists() or (self.hmi / "NbtCapture").exists())

        # Re-install NBTRLS2 via fresh root and test launcher --refresh-setup & --remove-capture.
        self.tearDown()
        self.setUp()
        self.setup_fs(2)
        r_up = run(LAUNCHER, "--refresh-setup", env=self.env())
        self.assertEqual(r_up.returncode, 0, r_up.stderr)
        self.assertEqual(r_up.stdout.decode().strip(), "REFRESHED plugin")
        r_rm = run(LAUNCHER, "--remove-capture", env=self.env())
        self.assertEqual(r_rm.returncode, 0, r_rm.stderr)
        self.assertEqual(r_rm.stdout.decode().strip(), "CAPTURE_REMOVED")
        self.assertEqual(self.cfg.read_bytes(), STOCK_CFG)
        self.assertFalse(self.shim().exists() or (self.hmi / "NbtCapture").exists())

        # Operational status: pending-restart before diag shm exists, observer once populated.
        diag = self.base / "diag.shm"
        s0 = run(PROBE, "status", diag, env=self.env())
        self.assertEqual(s0.stdout.decode().strip(), "op=0 status=pending-restart")
        self.assertEqual(run(PROBE, "write-diag", diag, "1", "4321", env=self.env()).returncode, 0)
        s1 = run(PROBE, "status", diag, env=self.env())
        self.assertEqual(s1.stdout.decode().strip(), "op=1 status=observer pid=4321")


if __name__ == "__main__":
    unittest.main()
