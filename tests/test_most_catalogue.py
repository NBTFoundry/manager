# SPDX-License-Identifier: MPL-2.0
"""Register tracking of tools/most_catalogue.py on hand-encoded ARM code.

The instruction shapes are those of the generated MOST classes in HmiMain3D
(docs/head-unit-interfaces.md): a member init call, a member address spilled
to the stack and reloaded for an out-of-line constructor, and a vtable
address loaded from a literal and stored into the object.
"""
import importlib.util
from pathlib import Path
import struct
import unittest

ROOT = Path(__file__).resolve().parents[1]

CODE_VA = 0x1000
CODE = [
    0xE92D4810,  # push {r4, fp, lr}
    0xE28DB008,  # add fp, sp, #8
    0xE1A04000,  # mov r4, r0
    0xE284006C,  # add r0, r4, #0x6c
    0xE1A01004,  # mov r1, r4
    0xE3002228,  # movw r2, #0x228
    0xEB000038,  # bl 0x1100
    0xE2843C0E,  # add r3, r4, #0xe00
    0xE50B3028,  # str r3, [fp, #-40]
    0xE51B0028,  # ldr r0, [fp, #-40]
    0xEB000034,  # bl 0x1100
    0xE59F2008,  # ldr r2, [pc, #8]  (literal at 0x103c)
    0xE2822008,  # add r2, r2, #8
    0xE5842100,  # str r2, [r4, #0x100]
    0xE8BD8810,  # pop {r4, fp, pc}
    0x00002000,  # literal: a vtable address
]


def load_tool():
    if importlib.util.find_spec("capstone") is None:
        raise unittest.SkipTest("capstone is not installed")
    spec = importlib.util.spec_from_file_location("most_catalogue", ROOT / "tools" / "most_catalogue.py")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def fake_image(tool):
    data = b"".join(struct.pack("<I", w) for w in CODE)
    img = tool.Image.__new__(tool.Image)
    img.data = data
    img.segs = [(CODE_VA, 0, len(data))]
    img.relocs = {}
    img.text_va, img.text_off, img.text_size = CODE_VA, 0, len(data)
    img.md = tool.capstone.Cs(tool.capstone.CS_ARCH_ARM, tool.capstone.CS_MODE_ARM)
    img.md.detail = True
    return img


class RegisterTrackingTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tool = load_tool()
        cls.img = fake_image(cls.tool)

    def test_member_init_call_arguments(self):
        calls, _ = self.img.emulate(CODE_VA)
        site, target, r0, r1, r2 = calls[0]
        self.assertEqual((site, target), (0x1018, 0x1100))
        self.assertEqual((r0.base, r0.k), (self.tool.THIS, 0x6C))
        self.assertEqual((r1.base, r1.k), (self.tool.THIS, 0))
        self.assertEqual((r2.base, r2.k), (None, 0x228))

    def test_stack_spill_keeps_member_address(self):
        calls, _ = self.img.emulate(CODE_VA)
        site, target, r0, r1, r2 = calls[1]
        self.assertEqual((site, target), (0x1028, 0x1100))
        self.assertEqual((r0.base, r0.k), (self.tool.THIS, 0xE00))
        self.assertIsNone(r1)  # clobbered by the first call

    def test_vtable_store_and_return(self):
        calls, stores = self.img.emulate(CODE_VA)
        self.assertEqual(len(calls), 2)
        self.assertEqual(stores, [(0x100, 0x2008)])

    def test_function_start_and_bounds(self):
        self.assertEqual(self.img.func_start(0x1030), CODE_VA)
        self.assertIsNone(self.img.word(CODE_VA + len(CODE) * 4))
        self.assertEqual(self.img.callers()[0x1100], [0x1018, 0x1028])


if __name__ == "__main__":
    unittest.main()
