"""The car's Wi-Fi Direct group in the SESSION reply, read as NBT-AAD documents
it: WFDPass.conf XORed with "NBTEvo0715", the name "DIRECT-03-" + the device
name from the CarPlay configs or Connectivity's WLAN persistency."""
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build"))
DAEMON = BUILD / "managerd-test"
FAKE_DSI = BUILD / "libfake_dsi.so"
KEY = b"NBTEvo0715"
PASS = "k3;Z:9,x\\Q"  # characters a Wi-Fi code has to escape


def encode(passphrase):
    data = passphrase.encode()
    return bytes(b ^ KEY[i % len(KEY)] for i, b in enumerate(data)).hex().upper()


def most_rx_msg(fkt, op, payload=b"", fblock=0xA0, inst=0x03, src=0x203F, dst=0x1234):
    hdr = bytearray(32)
    hdr[4] = 2  # normal / variant
    hdr[12:14] = dst.to_bytes(2, "little")
    hdr[14:16] = src.to_bytes(2, "little")
    hdr[16] = fblock
    hdr[17] = inst
    hdr[18:20] = fkt.to_bytes(2, "little")
    hdr[20] = op
    hdr[28:32] = len(payload).to_bytes(4, "little")
    return (bytes(hdr) + payload).hex()


def most_registry(entries):
    out = bytearray(len(entries).to_bytes(4, "little"))
    for addr, key in entries:
        out.extend(addr.to_bytes(2, "little"))
        out.extend(key.to_bytes(2, "little"))
    return out.hex()


def most_tstring_utf8(text):
    return b"\x02" + text.encode("utf-8") + b"\x00"


def most_tstring_utf16(text):
    return b"\x00" + text.encode("utf-16le") + b"\x00\x00"


def wlan_config_payload(ssid, passphrase=b"", visible=1, utf16=False):
    enc = most_tstring_utf16 if utf16 else most_tstring_utf8
    parts = [
        most_tstring_utf8("00:11:22:33:44:55"),
        enc("BMW-HU"),
        most_tstring_utf8("127.0.0.1"),
        most_tstring_utf8("0-0"),
        most_tstring_utf8(""),
        b"\x01",
        enc(ssid),
        bytes([visible]),
        b"\x04",
        passphrase if isinstance(passphrase, bytes) else enc(passphrase),
    ]
    return b"".join(parts)


class WifiDirectTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "managed"
        (self.root / "state/manager").mkdir(parents=True, mode=0o700)
        (self.root / "trust").mkdir(mode=0o700)
        self.root.chmod(0o700)
        (self.root / "state").chmod(0o700)
        self.conn = Path(self.tmp.name) / "car"

    def tearDown(self):
        self.tmp.cleanup()

    def put(self, path, data):
        target = self.conn / path.lstrip("/")
        target.parent.mkdir(parents=True, exist_ok=True)
        target.write_bytes(data if isinstance(data, bytes) else data.encode())

    def session(self, iface="lo", extra_env=None, dsi_script=None, after=None):
        try:
            parent, child = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        env = dict(os.environ, NBT_TEST_CONN_ROOT=str(self.conn))
        if extra_env:
            env.update(extra_env)
        if dsi_script is not None:
            script = Path(self.tmp.name) / "dsi.script"
            script.write_text("\n".join(dsi_script) + "\n")
            log = Path(self.tmp.name) / "dsi.log"
            env.update(
                NBT_TEST_DSI_LIB=str(FAKE_DSI),
                NBT_TEST_WFD_MOCK_MOST="1",
                NBT_FAKE_DSI_SCRIPT=str(script),
                NBT_FAKE_DSI_LOG=str(log),
                NBT_FAKE_DSI_NO_CONTROLS="1",
            )
        daemon = subprocess.Popen([DAEMON, "--control-fd", str(child.fileno()), "--root", self.root],
                                  pass_fds=(child.fileno(),), env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)
        child.close()
        parent.settimeout(12)
        stream = parent.makefile("rb")

        def command(line):
            parent.sendall(line.encode() + b"\n")
            return stream.readline().decode().rstrip("\n")

        try:
            self.assertEqual(command("HELLO 1"), "HELLO\t1")
            reply = command(f"SESSION {iface} 0")
            if after:
                after(command)
            return reply.split("\t")
        finally:
            try:
                command("QUIT")
            except OSError:
                pass
            stream.close()
            parent.close()
            daemon.communicate(timeout=5)

    def test_carplay_hotspot_name_gives_the_group(self):
        self.put("/var/opt/conn/WFDPass.conf", encode(PASS) + "\r\n")
        self.put("/var/opt/conn/carplay/iap2_Wifi.cfg", "[wifi]\nSSID=BMW 34567 CarPlay\n")
        fields = self.session()
        self.assertEqual(fields[0], "SESSION")
        self.assertEqual(fields[2:], ["DIRECT-03-BMW34567", PASS])

    def test_accessory_name_and_the_persistent_copies(self):
        self.put("/fs/sda1/opt/conn/WFDPass.conf", encode(PASS))
        self.put("/var/opt/conn/carplay/iap2.cfg",
                 "[transport]\nname=usb\n[accessory]\nname = Toyota 51807\n")
        self.assertEqual(self.session()[2:], ["DIRECT-03-Toyota51807", PASS])

    def test_persistency_shapes(self):
        persistency = "/var/opt/sys/persistence/early/pers_ConnectivityWlanPersistencyPersMapper"
        self.put("/var/opt/conn/WFDPass.conf", encode(PASS))
        for blob, ssid in ((b"\x01\x00WBA1234\x00DIRECT-03-WBA1234\x00\x07", "DIRECT-03-WBA1234"),
                           (b"\x02Toyota51807\x00Toyota51807 CarPlay\x00240B6170131A7370505E", "DIRECT-03-Toyota51807"),
                           (b"\x00\x03WBA8E9C51GK123456\x00" + encode(PASS).encode() + b"\x00", "DIRECT-03-WBA8E9C51GK123456")):
            with self.subTest(ssid=ssid):
                self.put(persistency, blob)
                self.assertEqual(self.session()[2:], [ssid, PASS])

    def test_no_group_without_both_halves(self):
        self.assertEqual(len(self.session()), 2)  # nothing at all
        self.put("/var/opt/conn/carplay/iap2_Wifi.cfg", "SSID=BMW 34567 CarPlay\n")
        self.assertEqual(len(self.session()), 2)  # a name, no passphrase
        for bad in ("12345", "zz" * 10, encode("short"), encode("\x05" * 10)):  # odd, not hex, < 8, control
            with self.subTest(bad=bad):
                self.put("/var/opt/conn/WFDPass.conf", bad)
                self.assertEqual(len(self.session()), 2)
        self.put("/var/opt/conn/WFDPass.conf", encode(PASS))
        self.put("/var/opt/conn/carplay/iap2_Wifi.cfg", "SSID=BMW Hotspot\n")  # not "<name> CarPlay"
        self.assertEqual(len(self.session()), 2)

    def test_most_wfd_enable_and_restore_on_cancel(self):
        reg = most_registry([(0x203F, 0xA001), (0x203F, 0xA003)])
        cfg = wlan_config_payload("DIRECT-03-LIVE", "LiveWfdKey99", visible=1)
        script = [
            f"ON1 a003100c01 {most_rx_msg(0xC10, 0x0C, bytes([0]))}",
            f"ON a003110c060000000000000003000000010001 {most_rx_msg(0xC11, 0x0A, bytes([1, 0]))}",
            f"ON a003110c060000000000000003000000010001 {most_rx_msg(0xC11, 0x0D, bytes([1, 0, 1]))}",
            f"ON a003110c060000000000000003000000010001 {most_rx_msg(0xC10, 0x0C, bytes([1]))}",
            f"ON a003120c01 {most_rx_msg(0xC12, 0x0C, cfg)}",
            f"ON a003110c060000000000000003000000020000 {most_rx_msg(0xC11, 0x0D, bytes([2, 0, 0]))}",
            f"ID c0000001 {reg}",
        ]
        for with_cancel in (True, False):
            with self.subTest(with_cancel=with_cancel):
                log_file = Path(self.tmp.name) / "dsi.log"
                if log_file.exists():
                    log_file.unlink()
                cancelled = []
                fields = self.session(
                    dsi_script=script,
                    after=(lambda cmd: cancelled.append(cmd("CANCEL"))) if with_cancel else None,
                )
                self.assertEqual(fields[0], "SESSION")
                self.assertEqual(fields[2:], ["DIRECT-03-LIVE", "LiveWfdKey99"])
                if with_cancel:
                    self.assertEqual(cancelled, ["OK"])
                log = log_file.read_text()
                self.assertIn("03000000010001", log)
                self.assertIn("03000000020000", log)
                self.assertNotIn("a001", log)  # never touches CarPlay A0.01

    def test_most_wfd_left_on_when_android_auto_uses_it(self):
        # NBT-AAD set to the Wi-Fi Direct group: switching the group off after
        # Pair a phone would cut Android Auto, so it stays on. Set to the
        # CarPlay hotspot (NBT_ALT_WIFI wins over the checkbox), it goes off.
        reg = most_registry([(0x203F, 0xA003)])
        cfg = wlan_config_payload("DIRECT-03-LIVE", "LiveWfdKey99", visible=1)
        script = [
            f"ON1 a003100c01 {most_rx_msg(0xC10, 0x0C, bytes([0]))}",
            f"ON a003110c060000000000000003000000010001 {most_rx_msg(0xC11, 0x0D, bytes([1, 0, 1]))}",
            f"ON a003120c01 {most_rx_msg(0xC12, 0x0C, cfg)}",
            f"ON a003110c060000000000000003000000020000 {most_rx_msg(0xC11, 0x0D, bytes([2, 0, 0]))}",
            f"ID c0000001 {reg}",
        ]
        cases = (
            ({"/var/opt/aa/wifi-direct": ""}, False),
            ({"/opt/aa/nbt-aad.env": "# Wi-Fi\nNBT_ALT_WIFI= direct \n"}, False),
            ({"/opt/aa/wifi-direct": "", "/opt/aa/nbt-aad.env": "NBT_ALT_WIFI=CARPLAY\n"}, True),
        )
        for files, restored in cases:
            with self.subTest(files=sorted(files), restored=restored):
                shutil.rmtree(self.conn, ignore_errors=True)
                for path, text in files.items():
                    self.put(path, text)
                log_file = Path(self.tmp.name) / "dsi.log"
                if log_file.exists():
                    log_file.unlink()
                fields = self.session(dsi_script=script, after=lambda cmd: self.assertEqual(cmd("CANCEL"), "OK"))
                self.assertEqual(fields[2:], ["DIRECT-03-LIVE", "LiveWfdKey99"])
                log = log_file.read_text()
                self.assertIn("03000000010001", log)
                self.assertEqual("03000000020000" in log, restored)

    def test_most_wfd_state_report_alone_completes_the_switch(self):
        # dev66 in the car: the group came up within 2 s and WLANState3 said so,
        # but the answer to SelectWLANState3 went unread; after 10 s the worker
        # gave up and switched the group off. The state report is enough.
        reg = most_registry([(0x203F, 0xA003)])
        cfg = wlan_config_payload("DIRECT-03-LIVE", "LiveWfdKey99", visible=1)
        script = [
            f"ON1 a003100c01 {most_rx_msg(0xC10, 0x0C, bytes([0]))}",
            f"ON a003110c060000000000000003000000010001 {most_rx_msg(0xC10, 0x0C, bytes([1]))}",
            f"ON a003120c01 {most_rx_msg(0xC12, 0x0C, cfg)}",
            f"ON a003110c060000000000000003000000020000 {most_rx_msg(0xC10, 0x0C, bytes([0]))}",
            f"ID c0000001 {reg}",
        ]
        started = time.monotonic()
        fields = self.session(dsi_script=script, after=lambda cmd: self.assertEqual(cmd("CANCEL"), "OK"))
        self.assertEqual(fields[0], "SESSION")
        self.assertEqual(fields[2:], ["DIRECT-03-LIVE", "LiveWfdKey99"])
        self.assertLess(time.monotonic() - started, 5)  # not the 10 s timeout
        self.assertIn("03000000020000", (Path(self.tmp.name) / "dsi.log").read_text())

    def test_most_wfd_prefers_wfdpass_over_the_most_key(self):
        # dev69 in the car: MOST's key differed from WFDPass.conf and phones
        # given it dropped after associating, without a DHCP lease.
        self.put("/var/opt/conn/WFDPass.conf", encode(PASS))
        reg = most_registry([(0x203F, 0xA003)])
        cfg = wlan_config_payload("DIRECT-03-PRE", "MostKeyWrong9", visible=1)
        script = [
            f"ON a003100c01 {most_rx_msg(0xC10, 0x0C, bytes([1]))}",
            f"ON a003120c01 {most_rx_msg(0xC12, 0x0C, cfg)}",
            f"ID c0000001 {reg}",
        ]
        fields = self.session(dsi_script=script, after=lambda cmd: self.assertEqual(cmd("CANCEL"), "OK"))
        self.assertEqual(fields[2:], ["DIRECT-03-PRE", PASS])

    def test_most_wfd_already_active_preserves_state_and_uses_wfdpass_fallback(self):
        self.put("/var/opt/conn/WFDPass.conf", encode(PASS))
        reg = most_registry([(0x203F, 0xA001), (0x203F, 0xA003)])
        cfg = wlan_config_payload("DIRECT-03-PRE", passphrase=most_tstring_utf8(""), visible=1, utf16=True)
        script = [
            f"ON a003100c01 {most_rx_msg(0xC10, 0x0C, bytes([1]))}",
            f"ON a003120c01 {most_rx_msg(0xC12, 0x0C, cfg)}",
            f"ID c0000001 {reg}",
        ]
        fields = self.session(dsi_script=script, after=lambda cmd: self.assertEqual(cmd("CANCEL"), "OK"))
        self.assertEqual(fields[0], "SESSION")
        self.assertEqual(fields[2:], ["DIRECT-03-PRE", PASS])
        log = (Path(self.tmp.name) / "dsi.log").read_text()
        self.assertNotIn("a003110c06", log)  # neither enabled nor disabled

    def test_slow_preparation_keeps_replies_in_order(self):
        # dev62 in the car: preparing Wi-Fi Direct inside the worker's loop
        # stalled everything for seconds. It runs on its own thread now; the
        # commands sent behind SESSION wait for its reply, in order.
        try:
            parent, child = socket.socketpair()
        except PermissionError:
            self.skipTest("sandbox blocks local socket traffic")
        script = Path(self.tmp.name) / "dsi.script"
        script.write_text(f"ID c0000001 {most_registry([(0x203F, 0xA001)])}\n")  # no A0.03
        env = dict(os.environ, NBT_TEST_CONN_ROOT=str(self.conn), NBT_TEST_DSI_LIB=str(FAKE_DSI),
                   NBT_TEST_WFD_MOCK_MOST="1", NBT_FAKE_DSI_SCRIPT=str(script),
                   NBT_FAKE_DSI_NO_CONTROLS="1", NBT_TEST_WFD_TIMEOUT_MS="1500")
        daemon = subprocess.Popen([DAEMON, "--control-fd", str(child.fileno()), "--root", self.root],
                                  pass_fds=(child.fileno(),), env=env, stdout=subprocess.PIPE,
                                  stderr=subprocess.PIPE)
        child.close()
        parent.settimeout(12)
        stream = parent.makefile("rb")
        try:
            parent.sendall(b"HELLO 1\n")
            self.assertEqual(stream.readline(), b"HELLO\t1\n")
            started = time.monotonic()
            parent.sendall(b"SESSION wfd0 0\nLIST\nQUIT\n")
            self.assertEqual(stream.readline(), b"ERR network-unavailable\n")
            self.assertGreater(time.monotonic() - started, 1.2)
            list_reply = stream.readline()
            self.assertTrue(list_reply.startswith((b"APPS\t", b"ERR list")), list_reply)
            self.assertEqual(stream.readline(), b"OK\n")
            _, stderr = daemon.communicate(timeout=5)
            self.assertEqual(daemon.returncode, 0, stderr)
        finally:
            stream.close()
            parent.close()
            if daemon.poll() is None:
                daemon.kill()
                daemon.communicate()

    def test_most_wfd_rejects_missing_or_failed_group(self):
        self.put("/var/opt/conn/WFDPass.conf", encode(PASS))
        self.put("/var/opt/conn/carplay/iap2_Wifi.cfg", "SSID=BMW 34567 CarPlay\n")
        # wfd0 must never succeed via offline config fallback when MOST/interface is unavailable.
        self.assertEqual(self.session(iface="wfd0"), ["ERR network-unavailable"])

        reg = most_registry([(0x203F, 0xA003)])
        hidden_cfg = wlan_config_payload("DIRECT-03-HID", "LiveWfdKey99", visible=0)
        scenarios = [
            ([f"ID c0000001 {most_registry([(0x203F, 0xA001)])}"], {"NBT_TEST_WFD_TIMEOUT_MS": "150"}, False),
            ([
                f"ON a003100c01 {most_rx_msg(0xC10, 0x0C, bytes([0]))}",
                f"ON a003110c060000000000000003000000010001 {most_rx_msg(0xC11, 0x09, bytes([1, 0, 0x0B]))}",
                f"ID c0000001 {reg}",
            ], {"NBT_TEST_WFD_TIMEOUT_MS": "500"}, False),
            ([
                f"ON1 a003100c01 {most_rx_msg(0xC10, 0x0C, bytes([0]))}",
                f"ON a003110c060000000000000003000000010001 {most_rx_msg(0xC11, 0x0D, bytes([1, 0, 1]))}",
                f"ON a003110c060000000000000003000000010001 {most_rx_msg(0xC10, 0x0C, bytes([1]))}",
                f"ON a003120c01 {most_rx_msg(0xC12, 0x0C, hidden_cfg)}",
                f"ON a003110c060000000000000003000000020000 {most_rx_msg(0xC11, 0x0D, bytes([2, 0, 0]))}",
                f"ID c0000001 {reg}",
            ], {"NBT_TEST_WFD_TIMEOUT_MS": "1000"}, True),
        ]
        for idx, (script, env, expect_restore) in enumerate(scenarios):
            with self.subTest(case=idx):
                log_file = Path(self.tmp.name) / "dsi.log"
                if log_file.exists():
                    log_file.unlink()
                self.assertEqual(self.session(extra_env=env, dsi_script=script), ["ERR network-unavailable"])
                if expect_restore:
                    self.assertIn("03000000020000", log_file.read_text())


if __name__ == "__main__":
    unittest.main()
