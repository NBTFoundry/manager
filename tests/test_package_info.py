# SPDX-License-Identifier: MPL-2.0
"""Package information for people: property= and text= lines (PROTOCOL.md,
"Package information"), written by tools/package.py and checked by the worker's
manifest parser (nbt-verify)."""
from pathlib import Path
import struct
import tempfile
import unittest

from test_package import PACK, VERIFY, run


class PackageInfoTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.path = Path(cls.tmp.name)
        key = cls.path / "key.pem"
        for args in (("genpkey", "-algorithm", "RSA", "-pkeyopt", "rsa_keygen_bits:3072", "-out", key),
                     ("pkey", "-in", key, "-pubout", "-out", cls.path / "key.pub")):
            result = run("openssl", *args)
            if result.returncode:
                raise RuntimeError(result.stderr.decode())
        elf = bytearray(128)
        elf[:7] = b"\x7fELF\x01\x01\x01"
        elf[16:20] = b"\x02\x00\x28\x00"
        cls.elf = bytes(elf)
        (cls.path / "app").write_bytes(cls.elf)

    @classmethod
    def tearDownClass(cls):
        cls.tmp.cleanup()

    def pack(self, name, *extra):
        output = self.path / name
        output.unlink(missing_ok=True)
        return output, run("python3", PACK, "--app", "test", "--version", "v1", "--sequence", "1",
                           "--compat", "demo-1", "--key", self.path / "key.pem", "--output", output,
                           "--setting", "background|Background color|blue|blue,red", *extra, self.path / "app")

    def verify(self, data):
        path = self.path / "candidate.nbt"
        path.write_bytes(data)
        return run(VERIFY, path, self.path / "key.pub", "test", "demo-1")

    def signed(self, manifest):
        header = struct.pack(">8sIIQ", b"NBTMGR1\n", len(manifest), 384, len(self.elf))
        signature = run("openssl", "dgst", "-sha256", "-sign", self.path / "key.pem", input=header + manifest)
        self.assertEqual(signature.returncode, 0, signature.stderr)
        return header + manifest + signature.stdout + self.elf

    def test_packer_writes_them_after_the_settings(self):
        output, result = self.pack("info.nbt", "--property", "NBT-AAD|1.9.117 (+2, 5c13e5f)",
                                   "--property", "Licence|PolyForm Noncommercial 1.0.0",
                                   "--text", "Android Auto by NBT-AAD.", "--text=", "--text", "Second | paragraph.")
        self.assertEqual(result.returncode, 0, result.stderr)
        data = output.read_bytes()
        self.assertIn(b"setting=background|Background color|blue|blue,red\n"
                      b"property=NBT-AAD|1.9.117 (+2, 5c13e5f)\nproperty=Licence|PolyForm Noncommercial 1.0.0\n"
                      b"text=Android Auto by NBT-AAD.\ntext=\ntext=Second | paragraph.\n", data)
        result = self.verify(data)
        self.assertEqual(result.returncode, 0, result.stderr)

    def test_packer_refuses_what_the_worker_would(self):
        refused = [("--property", "NBT-AAD"), ("--property", "|1"), ("--property", "A|"), ("--property", " A|1"),
                   ("--property", "A|1 "), ("--property", "A|B|C"), ("--property", "x" * 25 + "|1"),
                   ("--property", "A|" + "x" * 65), ("--property", "A|café"), ("--text", "x" * 121),
                   ("--text", "tab\there")]
        refused += [tuple(a for i in range(9) for a in ("--property", f"P{i}|v"))]
        refused += [tuple(a for i in range(21) for a in ("--text", f"line {i}"))]
        for extra in refused:
            with self.subTest(extra=extra[:4]):
                output, result = self.pack("refused.nbt", *extra)
                self.assertEqual(result.returncode, 2)
                self.assertFalse(output.exists())

    def test_worker_checks_the_signed_lines(self):
        output, result = self.pack("plain.nbt")
        self.assertEqual(result.returncode, 0, result.stderr)
        data = output.read_bytes()
        manifest = data[24:24 + struct.unpack(">I", data[8:12])[0]]
        accepted = [b"property=A|1\n", b"text=\n", b"property=A|1\ntext=x\n",
                    b"".join(b"property=P%d|%s\n" % (i, b"v" * 64) for i in range(8)),
                    b"".join(b"text=%s\n" % (b"y" * 120) for _ in range(12))]
        for extra in accepted:
            with self.subTest(extra=extra[:40]):
                result = self.verify(self.signed(manifest + extra))
                self.assertEqual(result.returncode, 0, result.stderr)
        rejected = [b"property=A\n", b"property=|1\n", b"property=A|\n", b"property=A |1\n", b"property=A|1|2\n",
                    b"property=A|\x7f\n", b"text=\t\n", b"text=" + b"y" * 121 + b"\n", b"text=x",
                    b"text=x\nproperty=A|1\n",                      # properties come before text
                    b"property=A|1\nsetting=size|Size|s|s,m\n",    # settings before properties
                    b"".join(b"property=P%d|v\n" % i for i in range(9)),
                    b"".join(b"text=%d\n" % i for i in range(21)), b"note=1\n"]
        for extra in rejected:
            with self.subTest(extra=extra[:40]):
                self.assertEqual(self.verify(self.signed(manifest + extra)).returncode, 1)


if __name__ == "__main__":
    unittest.main()
