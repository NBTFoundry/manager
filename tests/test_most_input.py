# SPDX-License-Identifier: MPL-2.0
import os
from pathlib import Path
import struct
import subprocess
import tempfile
import unittest


BUILD = Path(os.environ.get("NBT_BUILD_DIR", Path(__file__).resolve().parents[1] / "build"))
PROBE = BUILD / "most-input-probe"
FAKE = BUILD / "libfake_dsi.so"


def message(fkt, data, inst=3, fblock=0xE5, op=0xC, tx=0, tag=2, value=0, length=None):
    header = struct.pack("<IIIHHBBHB3xII", tx, tag, value, 0x100, 0x5000, fblock, inst, fkt, op, 0,
                         len(data) if length is None else length)
    return (header + bytes(data)).hex()


def rotary(position, push=0, direction=0):
    return message(0xC46, [1, position >> 8, position & 0xFF, push, direction, 0, 0, 0xFF])


def keys(*pressed):
    return message(0xC47, [1 if i in pressed else 0 for i in range(7)])


def ccs_rotary(position, overflow=0, counter=0x21):
    return message(0xC10, [1, 0x1D, counter, position >> 8, position & 0xFF, 0x1E, overflow], inst=1)


def ccs_button(status, tilt=0, cluster=0x1E, sensor=1):
    return message(0xC20, [1, 0x1D, 0x22, status, tilt, cluster, sensor], inst=1)


def registry(entries, count=None):
    """Registry attribute payload: int count, then (first word, FBlock key) pairs."""
    body = b"".join(struct.pack("<HH", first, key) for key, first in entries)
    return "ID c0000001 " + (struct.pack("<i", len(entries) if count is None else count) + body).hex()


class MostInputTests(unittest.TestCase):
    def run_script(self, lines, refuse=False, debug_log=False):
        with tempfile.TemporaryDirectory() as directory:
            script = Path(directory) / "script"
            log = Path(directory) / "log"
            debug = Path(directory) / "debug.log"
            script.write_text("\n".join(lines) + "\n")
            env = dict(os.environ, NBT_FAKE_DSI_SCRIPT=str(script), NBT_FAKE_DSI_LOG=str(log))
            if refuse:
                env["NBT_FAKE_DSI_REFUSE"] = "1"
            if debug_log:
                debug.touch(mode=0o600)
                env["NBT_PROBE_LOG"] = str(debug)
            result = subprocess.run([PROBE, FAKE, "400"], capture_output=True, env=env, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            output = result.stdout.decode().strip(), log.read_text().splitlines()
            if debug_log:
                return output + (debug.read_text().splitlines(),)
            return output

    def test_registration_and_subscriptions(self):
        output, log = self.run_script([], refuse=True)
        self.assertEqual(output, "")
        self.assertEqual(log[:3], ["CONNECTION", "CLIENT 4.5", "ADD 1"])
        name = b"NBTMGR_INPUT\0"
        register = struct.pack("<I", len(name)) + name + b"\0" * (-len(name) % 4)
        self.assertIn("REQUEST f " + register.hex(), log)
        notified = [line.split()[1] for line in log if line.startswith("NOTIFY")]
        self.assertEqual(notified, ["c0000002", "c0000003", "c0000001", "80000001", "80000002", "80000003",
                                    "80000004", "80000005", "80000006", "80000007", "c0000000"])
        controls = struct.pack("<II7H2x", 7, 7, 0xE501, 0xE502, 0xE503, 0xE505, 0xE510, 0xE512, 0xE514)
        # Registered exactly once, after the accepted reply; the refused reply is ignored.
        self.assertEqual([line for line in log if line.startswith("REQUEST 1 ")], ["REQUEST 1 " + controls.hex()])
        self.assertNotIn("REQUEST dead", " ".join(log))
        self.assertEqual(log[-1], "DONE")

    def test_controller_messages_become_inputs(self):
        lines = [
            rotary(100),                       # first position: no input
            rotary(102), rotary(101),          # two clockwise detents, one back
            rotary(101, push=1), rotary(101, push=1), rotary(101, push=0),
            rotary(101, direction=7), rotary(101, direction=7), rotary(101, direction=0),
            rotary(101, direction=10), rotary(101, direction=4), rotary(101, direction=1), rotary(101),
            keys(1), keys(), keys(0), keys(),  # BACK, then MENU
            keys(2), keys(),                   # OPTIONS: ignored
            rotary(0xFFFF),                    # a large jump re-bases
            rotary(1),                         # wraps: two clockwise detents
            message(0xC46, [1, 0, 9, 0, 0, 0, 0, 0xFF], inst=4),   # other instance
            message(0xC46, [1, 0, 9, 0, 0, 0, 0, 0xFF], op=0x1),   # not a status
            message(0xC46, [1, 0, 9, 0, 0, 0, 0, 0xFF], fblock=0x15),
            message(0xC46, [1, 0, 9, 1, 0, 0, 0, 0xFF], tx=1, value=19),  # segment
            message(0xC46, [1, 0, 9, 1, 0], length=200),            # length beyond payload
            message(0xC46, [1, 0, 9, 1]),                           # too short
            "DETACH",                          # link lost: positions forgotten
            rotary(50),                        # re-based, no input
            rotary(48),                        # two counter-clockwise detents
            ccs_rotary(0x10), ccs_rotary(0x11),
            ccs_rotary(0x40, overflow=1), ccs_rotary(0x3F),
            ccs_rotary(0x50, counter=0xFF), ccs_rotary(0x51),
            ccs_button(1), ccs_button(2), ccs_button(0),
            ccs_button(1, tilt=8, cluster=0x1D, sensor=0), ccs_button(0, cluster=0x1D, sensor=0),
            ccs_button(1, tilt=2, cluster=0x1D, sensor=0),
            ccs_button(1, cluster=0, sensor=2), ccs_button(0, cluster=0, sensor=2),
            ccs_button(3, cluster=0, sensor=2),
        ]
        output, log = self.run_script(lines)
        # A push reports its release (R) too, so a hold can be told from a tap.
        self.assertEqual(output, "DDU" "SR" "D" "BU" "BH" "DD" "UU" "D" "U" "SR" "B" "B")
        self.assertEqual(log[-1], "DONE")

    def test_registry_is_logged_once_per_content(self):
        small = [(0xE501, 0x0100), (0x0501, 0x0102), (0x2201, 0x0100)]
        twenty = [(0x4000 + i, i) for i in range(20)]
        many = [(0x6000 + i, 0xFFFF) for i in range(130)]
        lines = [
            registry(small),
            registry(small),                    # unchanged: not logged again
            rotary(100), rotary(102),           # input still flows
            registry(twenty),                   # changed: two lines of entries
            registry(small[:2], count=5),       # count beyond the payload
            registry([], count=-1),             # negative count
            "ID c0000001 ",                     # empty payload
            registry(twenty),
            "DETACH",                           # link lost: logged again
            registry(twenty),
            registry(many),                     # cut to the first 128
            registry([]),
        ]
        output, log, debug = self.run_script(lines, debug_log=True)
        self.assertEqual(output, "DD")
        self.assertEqual(log[-1], "DONE")
        self.assertNotIn("REQUEST dead", " ".join(log))
        found = [line.split("] ", 1)[1] for line in debug if "MOST registry" in line]
        header = "MOST registry: {} entries, FBlock key/first word in hex"
        twenty_lines = [header.format(20),
                        "MOST registry 1-16: " + " ".join("%04X/%04X" % e for e in twenty[:16]),
                        "MOST registry 17-20: " + " ".join("%04X/%04X" % e for e in twenty[16:])]
        self.assertEqual(found, [
            header.format(3),
            "MOST registry 1-3: E501/0100 0501/0102 2201/0100",
            *twenty_lines,
            "MOST registry malformed (12 bytes)",
            "MOST registry malformed (4 bytes)",
            "MOST registry malformed (0 bytes)",
            *twenty_lines,                      # differs from the empty payload before it
            *twenty_lines,                      # after the link came back
            header.format(130) + "; the first 128 shown",
            *["MOST registry %d-%d: " % (i + 1, i + 16) + " ".join("%04X/FFFF" % (0x6000 + j)
                                                                   for j in range(i, i + 16))
              for i in range(0, 128, 16)],
            header.format(0),
        ])
        self.assertTrue(all(len(line.split("] ", 1)[1]) <= 200 for line in debug))


if __name__ == "__main__":
    unittest.main()
