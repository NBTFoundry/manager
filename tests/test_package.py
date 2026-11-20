# SPDX-License-Identifier: MPL-2.0
import hashlib
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PACK = ROOT / "tools/package.py"
BUNDLE = ROOT / "tools/prepare_bundle.py"
VERIFY = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build")) / "nbt-verify"


def run(*args, input=None):
    return subprocess.run([str(a) for a in args], input=input, stdout=subprocess.PIPE,
                          stderr=subprocess.PIPE, check=False)


class PackageTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.path = Path(cls.tmp.name)
        for name in ("key", "other"):
            key = cls.path / (name + ".pem")
            result = run("openssl", "genpkey", "-algorithm", "RSA", "-pkeyopt",
                         "rsa_keygen_bits:3072", "-out", key)
            if result.returncode:
                raise RuntimeError(result.stderr.decode())
            result = run("openssl", "pkey", "-in", key, "-pubout", "-out",
                         cls.path / (name + ".pub"))
            if result.returncode:
                raise RuntimeError(result.stderr.decode())
        elf = bytearray(128)
        elf[:7] = b"\x7fELF\x01\x01\x01"
        elf[16:20] = b"\x02\x00\x28\x00"
        elf[52:] = bytes(range(76))
        cls.elf = bytes(elf)
        (cls.path / "app").write_bytes(cls.elf)
        cls.good = cls.path / "good.nbt"
        result = run("python3", PACK, "--app", "test", "--version", "v1", "--sequence", "1",
                     "--compat", "demo-1", "--key", cls.path / "key.pem", "--output", cls.good,
                     cls.path / "app")
        if result.returncode:
            raise RuntimeError(result.stderr.decode())

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def verify(self, data, key="key.pub", app="test", compat="demo-1"):
        path = self.path / "candidate.nbt"
        path.write_bytes(data)
        return run(VERIFY, path, self.path / key, app, compat)

    def signed_package(self, manifest, payload=None):
        if payload is None:
            payload = self.elf
        header = struct.pack(">8sIIQ", b"NBTMGR1\n", len(manifest), 384, len(payload))
        signature = run("openssl", "dgst", "-sha256", "-sign", self.path / "key.pem",
                        input=header + manifest)
        self.assertEqual(signature.returncode, 0, signature.stderr)
        return header + manifest + signature.stdout + payload

    def test_valid_and_identity(self):
        data = self.good.read_bytes()
        valid = self.verify(data)
        self.assertEqual(valid.returncode, 0, valid.stderr)
        self.assertIn(b"app=test\nversion=v1\nsequence=1\n", valid.stdout)
        self.assertIn(hashlib.sha256(data).hexdigest().encode(), valid.stdout)
        self.assertEqual(self.verify(data, key="other.pub").returncode, 1)
        self.assertEqual(self.verify(data, app="other").returncode, 1)
        self.assertEqual(self.verify(data, compat="wrong").returncode, 1)

    def test_prepare_bundle_uses_only_verified_release(self):
        output = self.path / "bundle"
        args = ("python3", BUNDLE, "--app", "test", "--compat", "demo-1",
                "--package", self.good, "--public-key", self.path / "key.pub",
                "--worker", self.path / "app", "--verifier", VERIFY, "--output", output)
        result = run(*args)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((output / "apps/test/bin/app").read_bytes(), self.elf)
        self.assertIn(hashlib.sha256(self.elf).hexdigest().encode(),
                      (output / "state/test/installed").read_bytes())
        self.assertEqual((output / "data/test/settings").read_text(), "NBTVALUES1\n")
        self.assertEqual((output / "state/test/settings.schema").read_text(), "NBTSETTINGS1\n")
        self.assertEqual(run(*args).returncode, 1)
        invalid = self.path / "invalid-bundle"
        bad = run("python3", BUNDLE, "--app", "test", "--compat", "demo-1",
                  "--package", self.good, "--public-key", self.path / "other.pub",
                  "--worker", self.path / "app", "--verifier", VERIFY, "--output", invalid)
        self.assertEqual(bad.returncode, 1)
        self.assertFalse(invalid.exists())

    def test_offline_bundle_preserves_signed_settings(self):
        data = self.good.read_bytes()
        length = struct.unpack(">I", data[8:12])[0]
        declared = self.path / "declared"
        declared.write_bytes(self.signed_package(data[24:24 + length] +
                             b"setting=background|Background color|red|blue,red\n", self.elf))
        output = self.path / "declared-bundle"
        result = run("python3", BUNDLE, "--app", "test", "--compat", "demo-1", "--package", declared,
                     "--public-key", self.path / "key.pub", "--worker", self.path / "app",
                     "--verifier", VERIFY, "--output", output)
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual((output / "data/test/settings").read_text(), "NBTVALUES1\nbackground=red\n")
        self.assertEqual((output / "state/test/settings.schema").read_text(),
                         "NBTSETTINGS1\nsetting=background|Background color|red|blue,red\n")

    def test_tampering_truncation_and_trailing_data(self):
        data = self.good.read_bytes()
        length = struct.unpack(">I", data[8:12])[0]
        for position in (0, 24, 24 + length, len(data) - 1):
            changed = bytearray(data)
            changed[position] ^= 1
            with self.subTest(position=position):
                self.assertEqual(self.verify(changed).returncode, 1)
        for cut in (1, 23, 24, 24 + length, len(data) - 1):
            with self.subTest(cut=cut):
                self.assertEqual(self.verify(data[:cut]).returncode, 1)
        self.assertEqual(self.verify(data + b"x").returncode, 1)

    def test_signed_malformed_manifest_and_non_arm_payload(self):
        data = self.good.read_bytes()
        length = struct.unpack(">I", data[8:12])[0]
        manifest = data[24:24 + length]
        for changed in (manifest.replace(b"app=test\n", b"app=test\napp=test\n"),
                        manifest.replace(b"sequence=1\n", b"sequence=01\n"),
                        manifest.replace(b"compat=demo-1\n", b"compat=../../etc\n"),
                        manifest.replace(b"version=v1\n", b"version=v1\x00\n")):
            self.assertEqual(self.verify(self.signed_package(changed)).returncode, 1)
        payload = bytearray(self.elf)
        payload[18] = 62
        changed = manifest.replace(hashlib.sha256(self.elf).hexdigest().encode(),
                                   hashlib.sha256(payload).hexdigest().encode())
        self.assertEqual(self.verify(self.signed_package(changed, payload)).returncode, 1)

    def test_signed_manifest_boundaries(self):
        data = self.good.read_bytes()
        length = struct.unpack(">I", data[8:12])[0]
        manifest = data[24:24 + length]
        variants = [
            manifest.replace(b"app=test", b"app=" + b"a" * count)
            for count in (0, 49, 100, 1000, 1800)
        ]
        variants += [manifest.replace(b"sequence=1", b"sequence=" + value) for value in
                     (b"0", b"18446744073709551616", b"-1", b"1" * 300)]
        variants += [manifest[:i] + b"\n" + manifest[i:] for i in range(0, len(manifest), 13)]
        for changed in variants:
            with self.subTest(length=len(changed)):
                self.assertEqual(self.verify(self.signed_package(changed)).returncode, 1)

    def test_bridge_declarations_are_signed_and_strict(self):
        data = self.good.read_bytes()
        length = struct.unpack(">I", data[8:12])[0]
        manifest = data[24:24 + length]
        valid = b"bridge=1.0\nbridge-uses=show,status\nbridge-optional=hold,future-cap\n"
        self.assertEqual(self.verify(self.signed_package(manifest + valid)).returncode, 0)
        self.assertEqual(self.verify(self.signed_package(manifest + valid.replace(b"1.0", b"65535.65535"))).returncode, 0)
        invalid = [valid + valid, valid.replace(b"1.0", b"01.0"), valid.replace(b"1.0", b"0.1"),
                   valid.replace(b"1.0", b"1.65536"), valid.replace(b"show,status", b"show,show"),
                   valid.replace(b"hold,future-cap", b"show"), valid.replace(b"hold,future-cap", b"hold,"),
                   b"bridge-uses=show\n", b"bridge=1.0\n", valid + b"unknown=x\n",
                   valid.replace(b"show,status", b"a" * 256)]
        for extra in invalid:
            with self.subTest(extra=extra[:70]):
                self.assertEqual(self.verify(self.signed_package(manifest + extra)).returncode, 1)
        changed = bytearray(self.signed_package(manifest + valid))
        changed[24 + len(manifest) + 7] ^= 1
        self.assertEqual(self.verify(changed).returncode, 1)

    def test_declared_settings(self):
        data = self.good.read_bytes()
        length = struct.unpack(">I", data[8:12])[0]
        manifest = data[24:24 + length]
        background = b"setting=background|Background color|blue|blue,red,green,black\n"
        accepted = [background,
                    background + b"setting=size|Text size, in points|s|s,m,l\n",
                    b"".join(b"setting=k%d|L|a|a,b-c_d,ef0123456789\n" % i for i in range(12)),
                    b"setting=a|" + b"x" * 24 + b"|" + b"z" * 12 + b"|a,b,c,d,e," + b"z" * 12 + b"\n"]
        for extra in accepted:
            with self.subTest(extra=extra):
                result = self.verify(self.signed_package(manifest + extra))
                self.assertEqual(result.returncode, 0, result.stderr)
        rejected = [b"setting=\n", b"setting=background\n", b"other=1\n",
                    background + background,                                   # duplicate key
                    b"".join(b"setting=k%d|L|a|a\n" % i for i in range(13)),    # too many
                    b"setting=background|Background|pink|blue,red\n",          # default not a choice
                    b"setting=Background|B|a|a\n", b"setting=1x|B|a|a\n", b"setting=a-b|B|a|a\n",
                    b"setting=" + b"k" * 17 + b"|B|a|a\n",
                    b"setting=k||a|a\n", b"setting=k| B|a|a\n", b"setting=k|B |a|a\n",
                    b"setting=k|" + b"x" * 25 + b"|a|a\n", b"setting=k|B\tC|a|a\n",
                    b"setting=k|B|a|a|b\n", b"setting=k|B|a|\n", b"setting=k|B|a|a,,b\n",
                    b"setting=k|B|a|a,a\n", b"setting=k|B|a|a,b,c,d,e,f,g\n", b"setting=k|B|A|A\n",
                    b"setting=k|B|a|a," + b"z" * 13 + b"\n", b"setting=k|B|a|a"]
        for extra in rejected:
            with self.subTest(extra=extra):
                self.assertEqual(self.verify(self.signed_package(manifest + extra)).returncode, 1)
        # Settings come after sha256, never before.
        moved = manifest.replace(b"app=test\n", b"app=test\n" + background)
        self.assertEqual(self.verify(self.signed_package(moved)).returncode, 1)
        # The packer writes them, and refuses invalid ones.
        output = self.path / "settings.nbt"
        result = run("python3", PACK, "--app", "test", "--version", "v1", "--sequence", "1", "--compat", "demo-1",
                     "--key", self.path / "key.pem", "--output", output,
                     "--setting", "background|Background color|blue|blue,red,green,black", self.path / "app")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(background, output.read_bytes())
        self.assertEqual(self.verify(output.read_bytes()).returncode, 0)
        for setting in ("background|Background|pink|blue", "a|B|a|a,a", "a|B|a", "A|B|a|a", "a| B|a|a"):
            with self.subTest(setting=setting):
                bad = self.path / "bad-settings.nbt"
                result = run("python3", PACK, "--app", "test", "--version", "v1", "--sequence", "1",
                             "--compat", "demo-1", "--key", self.path / "key.pem", "--output", bad,
                             "--setting", setting, self.path / "app")
                self.assertEqual(result.returncode, 2)
                self.assertFalse(bad.exists())

    def test_packer_settings_count_and_total_manifest_bound(self):
        base = ("python3", PACK, "--app", "test", "--version", "v1", "--sequence", "1", "--compat", "demo-1",
                "--key", self.path / "key.pem")
        for count, expected in ((12, 0), (13, 2)):
            output = self.path / f"settings-count-{count}"
            declarations = [argument for i in range(count) for argument in ("--setting", f"k{i}|L|a|a,b")]
            result = run(*base, "--output", output, *declarations, self.path / "app")
            self.assertEqual(result.returncode, expected, result.stderr)
            if not expected:
                self.assertEqual(self.verify(output.read_bytes()).returncode, 0)
            else:
                self.assertFalse(output.exists())
        # Individually valid settings still share the 2048-byte signed manifest
        # with large (valid) capability lists; reject before emitting a package.
        output = self.path / "settings-too-large"
        declarations = [argument for i in range(12) for argument in ("--setting",
            f"key{i:013d}|{'L' * 24}|abcdefghijkl|abcdefghijkl,bcdefghijklm,cdefghijklmn,defghijklmno,efghijklmnop,fghijklmnopq")]
        required = ",".join(f"required{i:015d}" for i in range(10))
        optional = ",".join(f"optional{i:015d}" for i in range(10))
        result = run(*base, "--output", output, "--bridge", "1.0", "--bridge-uses", required,
                     "--bridge-optional", optional, *declarations, self.path / "app")
        self.assertEqual(result.returncode, 2)
        self.assertIn(b"manifest exceeds 2048 bytes", result.stderr)
        self.assertFalse(output.exists())

    def test_packer_reads_an_argument_file(self):
        # Applications keep their description in one file: one argument per line,
        # spaces kept, blank lines and comments skipped. The same package results.
        args = self.path / "package.args"
        args.write_text("# test application\n--app\ntest\n\n--compat\ndemo-1\n"
                        "--setting\nbackground|Background color|blue|blue,red,green,black\n")
        output = self.path / "from-file.nbt"
        result = run("python3", PACK, f"@{args}", "--version", "v1", "--sequence", "1",
                     "--key", self.path / "key.pem", "--output", output, self.path / "app")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertIn(b"setting=background|Background color|blue|blue,red,green,black\n", output.read_bytes())
        self.assertEqual(self.verify(output.read_bytes()).returncode, 0)

    def test_packer_refuses_existing_output(self):
        target = self.path / "existing"
        target.write_bytes(b"preserve")
        result = run("python3", PACK, "--app", "test", "--version", "v1", "--sequence", "1",
                     "--compat", "demo-1", "--key", self.path / "key.pem", "--output", target,
                     self.path / "app")
        self.assertNotEqual(result.returncode, 0)
        self.assertEqual(target.read_bytes(), b"preserve")


if __name__ == "__main__":
    unittest.main()
