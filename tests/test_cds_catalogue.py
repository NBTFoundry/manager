"""Synthetic ARM factory; no firmware bytes or dumps are checked in."""
import importlib.util
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))


class CdsCatalogueTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if importlib.util.find_spec("capstone") is None:
            raise unittest.SkipTest("capstone is not installed")
        import cds_catalogue
        cls.tool = cds_catalogue

    def image(self):
        import capstone
        mod = self.tool
        image = mod.Image.__new__(mod.Image)
        code = [0xE92D4810,  # push r4,fp,lr
                0xE59F1030,  # ldr r1,[pc,#48] -> 0x103c -> property
                0xEB00007C,  # bl 0x1200: string ctor
                0xE3A00010,  # mov r0,#16
                0xEB00003A,  # bl 0x1100: allocator
                0xE1A04000,  # mov r4,r0
                0xE59F3020,  # ldr r3,[pc,#32] -> 0x1040 -> creator vtable
                0xE4803004,  # str r3,[r0],#4
                0xE3A03052,  # mov r3,#82
                0xE5843008,  # str r3,[r4,#8]: id
                0xE3A03001,  # mov r3,#1
                0xE584300C,  # str r3,[r4,#12]: ProcUnit
                0xE8BD8810,  # pop r4,fp,pc
                0, 0, 0x2000, 0x3008]
        data = bytearray(0x1100)
        data[:len(code) * 4] = struct.pack("<%dI" % len(code), *code)
        name = b"driving.gear\0"
        data[0x1000:0x1000 + len(name)] = name
        image.data = bytes(data)
        image.segs = [(0x1000, 0, len(data))]
        image.relocs = {}
        image.text_va, image.text_off, image.text_size = 0x1000, 0, len(code) * 4
        image.md = capstone.Cs(capstone.CS_ARCH_ARM, capstone.CS_MODE_ARM)
        image.md.detail = True
        image.syms = [(a, 0, "") for a in (0x1000, 0x1100, 0x1200, 0x3000)]
        image.demangled = ["CCdsPropertyFactory::CCdsPropertyFactory()", "operator new(unsigned int)",
                           "THmiString<char>::THmiString(char const*)", "vtable for TPC<CCdsPropertyGear>"]
        return image

    def test_registration_keeps_procunit_distinct_from_most(self):
        rows = self.tool.catalogue(self.image())
        self.assertEqual(len(rows), 1)
        self.assertEqual(rows[0][:8], ("driving.gear", "Gear", 82, 1, "unknown",
                                      "unknown", "unknown", "unknown"))

    def test_unrecognized_factory_fails_instead_of_empty_success(self):
        image = self.image()
        image.data = struct.pack("<I", 0xE12FFF1E) + image.data[4:]
        with self.assertRaisesRegex(ValueError, "no registrations"):
            self.tool.catalogue(image)

    def test_string_must_be_bounded_ascii(self):
        image = self.image()
        self.assertIsNone(self.tool.cstring(image, 0x900000))
        image.data = image.data[:0x1000] + b"\xff\0"
        self.assertIsNone(self.tool.cstring(image, 0x2000))


if __name__ == "__main__":
    unittest.main()
