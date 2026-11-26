# SPDX-License-Identifier: MPL-2.0
import hashlib
import os
from pathlib import Path
import struct
import shutil
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
PACK = ROOT / "tools/package_release.py"
VERIFY = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build")) / "nbt-verify-release"
COPY = VERIFY.parent / "release-copy-probe"


def run(*args, input=None):
    return subprocess.run([str(a) for a in args], input=input, capture_output=True, check=False)


class ReleaseTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.root = Path(cls.tmp.name)
        for key in ("manager", "other"):
            generated = run("openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:3072",
                            "-out", cls.root / (key + ".pem"))
            if generated.returncode:
                raise RuntimeError(generated.stderr.decode())
            exported = run("openssl", "pkey", "-in", cls.root / (key + ".pem"), "-pubout",
                           "-out", cls.root / (key + ".pub"))
            if exported.returncode:
                raise RuntimeError(exported.stderr.decode())
        elf = bytearray(128)
        elf[:7] = b"\x7fELF\x01\x01\x01"
        elf[16:20] = b"\x02\x00\x28\x00"
        for file in ("launcher", "gui", "worker"):
            (cls.root / file).write_bytes(elf)
        plugin = cls.root / "plugin"
        plugin.mkdir()
        elf[16] = 3
        (plugin / "libNbtManager.so").write_bytes(elf)
        for name in ("NbtManager.manifest", "NbtManager.xml", "NbtManager_ALL_Texts.zip", "NbtManager_ALL_Images.zip"):
            (plugin / name).write_bytes(name.encode())
        cls.release = cls.root / "release.nbt"
        result = run("python3", PACK, "--version", "v1", "--sequence", "1", "--compat", "demo-1",
                     "--key", cls.root / "manager.pem", "--output", cls.release,
                     "--launcher", cls.root / "launcher", "--gui", cls.root / "gui",
                     "--worker", cls.root / "worker", "--plugin-dir", plugin)
        if result.returncode:
            raise RuntimeError(result.stderr.decode())

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def verify(self, data, key="manager.pub", compat="demo-1"):
        path = self.root / "candidate"
        path.write_bytes(data)
        return run(VERIFY, path, self.root / key, compat)

    def resign(self, manifest, payload, magic=b"NBTRLS1\n"):
        header = struct.pack(">8sII", magic, len(manifest), 384)
        signed = run("openssl", "dgst", "-sha256", "-sign", self.root / "manager.pem", input=header + manifest)
        self.assertEqual(signed.returncode, 0)
        return header + manifest + signed.stdout + payload

    def test_valid_identity_and_distinct_key(self):
        data = self.release.read_bytes()
        result = self.verify(data)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(hashlib.sha256(data).hexdigest().encode(), result.stdout)
        self.assertEqual(self.verify(data, key="other.pub").returncode, 1)
        self.assertEqual(self.verify(data, compat="wrong").returncode, 1)

    def test_tampering_truncation_and_trailing_data(self):
        data = self.release.read_bytes()
        size = struct.unpack(">I", data[8:12])[0]
        for pos in (0, 16, 16 + size, 16 + size + 384, len(data) - 1):
            changed = bytearray(data)
            changed[pos] ^= 1
            self.assertEqual(self.verify(changed).returncode, 1)
        for end in (0, 15, 16, 16 + size, len(data) - 1):
            self.assertEqual(self.verify(data[:end]).returncode, 1)
        self.assertEqual(self.verify(data + b"x").returncode, 1)

    def test_signed_invalid_manifest_and_component(self):
        data = self.release.read_bytes()
        size = struct.unpack(">I", data[8:12])[0]
        manifest, payload = data[16:16 + size], data[16 + size + 384:]
        for changed in (manifest.replace(b"sequence=1\n", b"sequence=01\n"),
                        manifest.replace(b"compat=demo-1\n", b"compat=../etc\n"),
                        manifest.replace(b"version=v1\n", b"version=v1\nversion=v1\n"),
                        manifest.replace(b"launcher_size=128\n", b"launcher_size=18446744073709551615\n"),
                        manifest.replace(b"version=v1\n", b"version=v1\x00\n")):
            self.assertEqual(self.verify(self.resign(changed, payload)).returncode, 1)
        changed_payload = bytearray(payload)
        changed_payload[18] = 62
        changed_manifest = manifest.replace(hashlib.sha256(payload[:128]).hexdigest().encode(),
                                            hashlib.sha256(changed_payload[:128]).hexdigest().encode(), 1)
        self.assertEqual(self.verify(self.resign(changed_manifest, changed_payload)).returncode, 1)

    def test_signed_bridge_provider_metadata(self):
        data = self.release.read_bytes()
        size = struct.unpack(">I", data[8:12])[0]
        manifest, payload = data[16:16 + size], data[16 + size + 384:]
        self.assertIn(b"bridge=1.9|show,status,live-settings,most,gamepad,controller,files,hold,car-data,net,runner,diag,service,companion\n", manifest)
        for bridge in (b"", b"bridge=1.3|show,status\nbridge=2.0|status\n",
                       b"bridge=65535.65535|future-cap\n"):
            result = self.verify(self.resign(manifest.replace(b"bridge=1.9|show,status,live-settings,most,gamepad,controller,files,hold,car-data,net,runner,diag,service,companion\n", bridge), payload))
            self.assertEqual(result.returncode, 0, result.stderr)
        for bridge in (b"bridge=1.0\n", b"bridge=0.1|\n", b"bridge=1.00|\n", b"bridge=1.65536|\n",
                       b"bridge=1.0|show,show\n", b"bridge=1.0|show,\n", b"bridge=1.0|Show\n",
                       b"bridge=1.0|\nbridge=1.1|\n", b"bridge=2.0|\nbridge=1.0|\n",
                       b"bridge=1.0|\nbridge=2.0|\nbridge=3.0|\n"):
            with self.subTest(bridge=bridge):
                self.assertEqual(self.verify(self.resign(manifest.replace(b"bridge=1.9|show,status,live-settings,most,gamepad,controller,files,hold,car-data,net,runner,diag,service,companion\n", bridge), payload)).returncode, 1)

    def test_packer_preserves_existing_output(self):
        result = run("python3", PACK, "--version", "v1", "--sequence", "1", "--compat", "demo-1",
                     "--key", self.root / "manager.pem", "--output", self.release,
                     "--launcher", self.root / "launcher", "--gui", self.root / "gui",
                     "--worker", self.root / "worker", "--plugin-dir", self.root / "plugin")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(self.verify(self.release.read_bytes()).returncode, 0)

    def test_copy_rechecks_every_verified_component(self):
        data = self.release.read_bytes()
        for index in range(8):
            source = self.root / f"source-{index}"
            output = self.root / f"output-{index}"
            source.write_bytes(data)
            result = run(COPY, source, self.root / "manager.pub", "demo-1", index, output, "normal")
            self.assertEqual(result.returncode, 0, result.stderr)
            expected = [self.root / "launcher", self.root / "gui", self.root / "worker",
                        self.root / "plugin/libNbtManager.so", self.root / "plugin/NbtManager.manifest",
                        self.root / "plugin/NbtManager.xml", self.root / "plugin/NbtManager_ALL_Texts.zip",
                        self.root / "plugin/NbtManager_ALL_Images.zip"][index]
            self.assertEqual(output.read_bytes(), expected.read_bytes())
            output.unlink()
            source.write_bytes(data)
            result = run(COPY, source, self.root / "manager.pub", "demo-1", index, output, "tamper")
            self.assertEqual(result.returncode, 1, result.stderr)
            output.unlink()
        source = self.root / "destination-source"
        source.write_bytes(data)
        output = self.root / "destination-nonempty"
        result = run(COPY, source, self.root / "manager.pub", "demo-1", 0, output, "nonempty")
        self.assertEqual(result.returncode, 1)
        self.assertEqual(output.read_bytes(), b"preserve")
        result = run(COPY, source, self.root / "manager.pub", "demo-1", 0, output, "special")
        self.assertEqual(result.returncode, 1)

    def test_complete_release_packaging_and_stale_installer_rejection(self):
        with tempfile.TemporaryDirectory(prefix="build release ") as temporary:
            build = Path(temporary) / "build"
            dist = Path(temporary) / "dist"
            build.mkdir()
            for source, destination in (("launcher", "nbt-manager-launcher"),
                                        ("gui", "nbt-manager-gui"),
                                        ("worker", "nbt-managerd-qnx")):
                shutil.copyfile(self.root / source, build / destination)
            shutil.copyfile(self.root / "launcher", build / "nbt-runner")
            shutil.copytree(self.root / "plugin", build / "NbtManager")
            shutil.copy2(VERIFY, build / "nbt-verify-release")
            environment = dict(os.environ, BUILD_DIR=str(build), DIST_DIR=str(dist), MANAGER_VERSION="v1",
                               MANAGER_SEQUENCE="1", MANAGER_COMPAT="demo-1",
                               MANAGER_SIGNING_KEY=str(self.root / "manager.pem"),
                               MANAGER_PUBLIC_KEY=str(self.root / "manager.pub"))
            command = ["python3", str(ROOT / "tools/build_release.py")]
            # The release carries the runner and the self test's applications
            # (docs/self-test.md): NBTRLS3 with four named components.
            (build / "nbt-runner").rename(build / "runner-elsewhere")
            result = subprocess.run(command + ["package"], env=environment, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"make qnx-runner", result.stderr)
            (build / "runner-elsewhere").rename(build / "nbt-runner")
            result = subprocess.run(command + ["package"], env=environment, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"make selftest-apps", result.stderr)

            def payload(mode, elf_type):  # a module or a guest: the SDK's NBT ABI note
                note = struct.pack("<III", 4, 12, 1) + b"NBT\0" + struct.pack("<III", 1, 1, mode)
                header = bytearray(52)
                header[:7] = b"\x7fELF\x01\x01\x01"
                struct.pack_into("<HHI", header, 16, elf_type, 40, 1)
                struct.pack_into("<I", header, 28, 52)
                struct.pack_into("<HHH", header, 40, 52, 32, 1)
                phdr = struct.pack("<IIIIIIII", 4, 84, 0, 0, len(note), len(note), 4, 4)
                return bytes(header) + phdr + note + b"code"
            (build / "selftest").mkdir()
            shutil.copyfile(self.root / "launcher", build / "selftest/selftest-native")
            (build / "selftest/selftest-guest.guest").write_bytes(payload(2, 2))
            (build / "selftest/selftest-module.so").write_bytes(payload(1, 3))
            for _ in range(2):
                result = subprocess.run(command + ["package"], env=environment, capture_output=True)
                self.assertEqual(result.returncode, 0, result.stderr)
                self.assertEqual(sorted(p.name for p in dist.iterdir()), ["manager.nbtr"])
                self.assertFalse((build / "manager.nbtr").exists())
                built = (dist / "manager.nbtr").read_bytes()
                verified = run(VERIFY, dist / "manager.nbtr", self.root / "manager.pub", "demo-1")
                self.assertIn(b"format=NBTRLS3\ncomponents=12\n", verified.stdout, verified.stderr)
                for name in (b"runner", b"selftest_native", b"selftest_guest", b"selftest"):
                    self.assertIn(b"\n" + name + b"_size=", built[:4096])
            environment["MANAGER_PUBLIC_KEY"] = str(self.root / "other.pub")
            result = subprocess.run(command + ["package"], env=environment, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertEqual((dist / "manager.nbtr").read_bytes(), built)
            self.assertEqual(sorted(p.name for p in dist.iterdir()), ["manager.nbtr"])
            environment["MANAGER_PUBLIC_KEY"] = str(self.root / "manager.pub")
            environment["MANAGER_SEQUENCE"] = "2"
            result = subprocess.run(command + ["installer"], env=environment, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"does not match requested version and sequence", result.stderr)
            # The step release (docs/manager-release.md): the same programs
            # without the named components, one sequence below, in the format
            # an older launcher reads.
            environment["MANAGER_VERSION"] = "v2"
            environment["MANAGER_STEP"] = "1"
            result = subprocess.run(command + ["package"], env=environment, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(sorted(p.name for p in dist.iterdir()), ["manager-step.nbtr", "manager.nbtr"])
            stepped = Path(temporary) / "step.nbtr"
            packed = run("python3", PACK, "--version", "v2-step", "--sequence", "1", "--compat", "demo-1",
                         "--key", self.root / "manager.pem", "--output", stepped,
                         "--launcher", self.root / "launcher", "--gui", self.root / "gui",
                         "--worker", self.root / "worker", "--plugin-dir", self.root / "plugin")
            self.assertEqual(packed.returncode, 0, packed.stderr)
            self.assertEqual((dist / "manager-step.nbtr").read_bytes(), stepped.read_bytes())
            verified = self.verify((dist / "manager-step.nbtr").read_bytes())
            self.assertIn(b"version=v2-step\nsequence=1\n", verified.stdout)
            self.assertNotIn(b"format=NBTRLS3", verified.stdout)
            # No step below sequence 1; without MANAGER_STEP an old step goes.
            environment["MANAGER_SEQUENCE"] = "1"
            result = subprocess.run(command + ["package"], env=environment, capture_output=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn(b"no step release", result.stderr)
            del environment["MANAGER_STEP"]
            result = subprocess.run(command + ["package"], env=environment, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertEqual(sorted(p.name for p in dist.iterdir()), ["manager.nbtr"])

    def test_v2_release_verification_and_capture_lib_copy(self):
        cap = self.root / "libnbt-capture.so"
        cap.write_bytes((self.root / "plugin/libNbtManager.so").read_bytes() + b"cap")
        v2 = self.root / "release-v2.nbtr"
        result = run("python3", PACK, "--version", "v2", "--sequence", "2", "--compat", "demo-1",
                     "--key", self.root / "manager.pem", "--output", v2,
                     "--launcher", self.root / "launcher", "--gui", self.root / "gui",
                     "--worker", self.root / "worker", "--plugin-dir", self.root / "plugin",
                     "--capture-lib", cap)
        self.assertEqual(result.returncode, 0, result.stderr)
        verified = self.verify(v2.read_bytes())
        self.assertEqual(verified.returncode, 0, verified.stderr)
        self.assertIn(b"format=NBTRLS2\ncomponents=9\n", verified.stdout)
        # Copy component 8 from NBTRLS2 succeeds; from NBTRLS1 fails.
        out = self.root / "copied-capture.so"
        self.assertEqual(run(COPY, v2, self.root / "manager.pub", "demo-1", 8, out, "normal").returncode, 0)
        self.assertEqual(out.read_bytes(), cap.read_bytes())
        out.unlink()
        self.assertNotEqual(run(COPY, self.release, self.root / "manager.pub", "demo-1", 8, out, "normal").returncode, 0)
        # Mismatched header/manifest or non-ET_DYN capture_lib must fail.
        data = v2.read_bytes()
        size = struct.unpack(">I", data[8:12])[0]
        manifest, payload = data[16:16 + size], data[16 + size + 384:]
        self.assertEqual(self.verify(self.resign(manifest, payload, magic=b"NBTRLS1\n")).returncode, 1)
        exec_cap = bytearray(cap.read_bytes())
        exec_cap[16] = 2  # ET_EXEC rather than ET_DYN
        bad_payload = payload[:-len(exec_cap)] + bytes(exec_cap)
        old_hash = hashlib.sha256(cap.read_bytes()).hexdigest().encode()
        new_hash = hashlib.sha256(bytes(exec_cap)).hexdigest().encode()
        self.assertEqual(
            self.verify(self.resign(manifest.replace(old_hash, new_hash), bad_payload, magic=b"NBTRLS2\n")).returncode,
            1,
        )

    def test_v3_named_components(self):
        """NBTRLS3: the runner and a component this manager does not know, by name."""
        runner = self.root / "nbt-runner"
        runner.write_bytes((self.root / "launcher").read_bytes() + b"runner")
        later = self.root / "later.bin"
        later.write_bytes(b"not a program: a later release's data")
        v3 = self.root / "release-v3.nbtr"
        args = ["--version", "v3", "--sequence", "3", "--compat", "demo-1",
                "--key", self.root / "manager.pem",
                "--launcher", self.root / "launcher", "--gui", self.root / "gui",
                "--worker", self.root / "worker", "--plugin-dir", self.root / "plugin"]
        result = run("python3", PACK, *args, "--output", v3,
                     "--component", f"runner={runner}", "--component", f"later_data={later}")
        self.assertEqual(result.returncode, 0, result.stderr)
        verified = self.verify(v3.read_bytes())
        self.assertEqual(verified.returncode, 0, verified.stderr)
        self.assertIn(b"format=NBTRLS3\ncomponents=10\n", verified.stdout)
        out = self.root / "copied-runner"
        self.assertEqual(run(COPY, v3, self.root / "manager.pub", "demo-1", 8, out, "normal").returncode, 0)
        self.assertEqual(out.read_bytes(), runner.read_bytes())
        out.unlink()
        self.assertEqual(run(COPY, v3, self.root / "manager.pub", "demo-1", 9, out, "normal").returncode, 0)
        self.assertEqual(out.read_bytes(), later.read_bytes())
        out.unlink()
        # The tool refuses bad names, fixed roles and duplicates.
        for bad in ("Runner=x", "9runner=x", "gui=x", "a" * 25 + "=x", "runner"):
            refused = run("python3", PACK, *args, "--output", self.root / "bad.nbtr", "--component", bad)
            self.assertNotEqual(refused.returncode, 0, bad)
        refused = run("python3", PACK, *args, "--output", self.root / "bad.nbtr",
                      "--component", f"runner={runner}", "--component", f"runner={runner}")
        self.assertNotEqual(refused.returncode, 0)
        data = v3.read_bytes()
        size = struct.unpack(">I", data[8:12])[0]
        manifest, payload = data[16:16 + size], data[16 + size + 384:]
        self.assertEqual(self.verify(self.resign(manifest, payload, magic=b"NBTRLS3\n")).returncode, 0)
        # NBTRLS1/2 headers do not take named components.
        for magic in (b"NBTRLS1\n", b"NBTRLS2\n", b"NBTRLS4\n"):
            self.assertEqual(self.verify(self.resign(manifest, payload, magic=magic)).returncode, 1)
        # A named component that repeats a fixed role or another name, or a bad name.
        for old, new in ((b"later_data_", b"gui_"), (b"later_data_", b"runner_"), (b"later_data_", b"Later_"),
                         (b"later_data_", b"x" * 25 + b"_")):
            self.assertEqual(self.verify(self.resign(manifest.replace(old, new), payload,
                                                     magic=b"NBTRLS3\n")).returncode, 1, new)
        # The runner must be ARM ELF.
        bad_runner = bytearray(runner.read_bytes())
        bad_runner[18] = 3  # not ARM
        old_hash = hashlib.sha256(runner.read_bytes()).hexdigest().encode()
        new_hash = hashlib.sha256(bytes(bad_runner)).hexdigest().encode()
        bad_payload = payload.replace(runner.read_bytes(), bytes(bad_runner))
        self.assertEqual(self.verify(self.resign(manifest.replace(old_hash, new_hash), bad_payload,
                                                 magic=b"NBTRLS3\n")).returncode, 1)
        # An unknown component is only hashed: any content.
        self.assertEqual(self.verify(self.resign(manifest, payload, magic=b"NBTRLS3\n")).returncode, 0)


if __name__ == "__main__":
    unittest.main()
