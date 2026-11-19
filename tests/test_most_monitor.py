import os
from pathlib import Path
import select
import signal
import socket
import struct
import subprocess
import tempfile
import unittest

BUILD = Path(os.environ.get("NBT_BUILD_DIR", Path(__file__).resolve().parents[1] / "build"))


class MonitorTests(unittest.TestCase):
    def launch(self, get="off", managed=False, health=False, directory=None):
        left, right = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        self.addCleanup(left.close)
        self.addCleanup(right.close)
        env = dict(os.environ, NBT_MOST_FD="6", NBT_SETTING_GET=get,
                   NBT_LAUNCH="check" if health else "user")
        mapping = [(right.fileno(), 6)]
        args = []
        if managed:
            if directory is None:
                temp = tempfile.TemporaryDirectory()
                self.addCleanup(temp.cleanup)
                directory = temp.name
            env["NBT_DATA_DIR"] = str(directory)
            read, write = os.pipe()
            lease = os.open(Path(directory) / "lease", os.O_RDWR | os.O_CREAT, 0o600)
            self.addCleanup(os.close, read)
            self.addCleanup(os.close, write)
            self.addCleanup(os.close, lease)
            mapping += [(write, 3), (lease, 4)]
            args = ["--ready-fd", "3", "--lease-fd", "4"]
            self.ready = read
            self.capture = Path(directory) / "most-capture.log"
        # Run the fd setup in an exec'd interpreter, avoiding preexec_fn.
        wrapper = """import fcntl, os, sys
pairs = %r
copies = [(fcntl.fcntl(a, fcntl.F_DUPFD_CLOEXEC, 20), b) for a, b in pairs]
for a, b in copies:
    os.dup2(a, b)
os.execv(sys.argv[1], sys.argv[1:])
""" % mapping
        process = subprocess.Popen(["python3", "-c", wrapper, str(BUILD / "most-monitor"), *args],
                                   env=env, pass_fds=tuple(a for a, _ in mapping),
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)

        def cleanup():
            if process.poll() is None:
                process.terminate()
            process.communicate(timeout=3)
        self.addCleanup(cleanup)
        left.settimeout(2)
        return process, left

    @staticmethod
    def state(link=4):
        return struct.pack("<BBBBBBHI", 1, 1, 0, 0, link, 0, 0x5011, 0)

    @staticmethod
    def registry():
        return struct.pack("<BBBBIHH", 1, 2, 0, 0, 1, 0x101, 0x500)

    def test_passive_records_without_commands(self):
        process, sock = self.launch()
        sock.send(self.state())
        sock.send(self.registry())
        sock.send(struct.pack("<BBBBHHBBHIIHH", 1, 3, 0, 12, 0x101, 0x5011,
                              5, 0, 0x621, 7, 0, 2, 0) + b"\x12\x34")
        sock.settimeout(0.2)
        with self.assertRaises(socket.timeout):
            sock.recv(100)
        process.send_signal(signal.SIGTERM)
        output, error = process.communicate(timeout=3)
        self.assertEqual(process.returncode, 0, error)
        self.assertIn(b"STATE 4 5011 dropped=0", output)
        self.assertIn(b"NODE 0101 05.00", output)
        self.assertIn(b"05.00.621 op=C flags=0 tag=7 dropped=0 1234", output)

    def test_get_once_after_both_state_and_registry(self):
        process, sock = self.launch(get="speed")
        sock.send(self.registry())
        sock.send(self.state())
        request = sock.recv(100)
        self.assertEqual(request, struct.pack("<BBBBHHBBHIIHH", 1, 0x81, 0, 1,
                                              0, 0, 5, 0, 0x621, 0, 1, 0, 0))
        sock.send(self.state())
        sock.send(self.registry())
        sock.settimeout(0.2)
        with self.assertRaises(socket.timeout):
            sock.recv(100)
        process.terminate()
        process.communicate(timeout=3)
        self.assertEqual(process.returncode, 0)

    def test_health_heartbeats_without_capture(self):
        process, _ = self.launch(managed=True, health=True)
        self.assertTrue(select.select([self.ready], [], [], 2)[0])
        self.assertEqual(os.read(self.ready, 1), b"R")
        self.assertFalse(self.capture.exists())
        process.terminate()
        process.communicate(timeout=3)
        self.assertEqual(process.returncode, 0)

    def test_managed_capture_and_symlink_rejection(self):
        process, sock = self.launch(managed=True)
        self.assertTrue(select.select([self.ready], [], [], 2)[0])
        sock.send(self.state())
        sock.settimeout(0.2)
        with self.assertRaises(socket.timeout):
            sock.recv(100)
        process.terminate()
        process.communicate(timeout=3)
        self.assertEqual(process.returncode, 0)
        self.assertIn("STATE 4", self.capture.read_text())
        self.assertEqual(self.capture.stat().st_mode & 0o777, 0o600)
        with tempfile.TemporaryDirectory() as directory:
            target = Path(directory) / "target"
            target.write_text("preserve")
            (Path(directory) / "most-capture.log").symlink_to(target)
            other, _ = self.launch(managed=True, directory=directory)
            other.communicate(timeout=3)
            self.assertEqual(other.returncode, 1)
            self.assertEqual(target.read_text(), "preserve")

    def test_malformed_record_and_setting_rejected(self):
        process, sock = self.launch()
        sock.send(self.state() + b"extra")
        process.communicate(timeout=3)
        self.assertEqual(process.returncode, 1)
        other, _ = self.launch(get="arbitrary")
        other.communicate(timeout=3)
        self.assertEqual(other.returncode, 2)

    def test_full_capture_stops_without_appending(self):
        process, sock = self.launch(managed=True)
        self.assertTrue(select.select([self.ready], [], [], 2)[0])
        with self.capture.open("r+b") as output:
            output.truncate(16 * 1024 * 1024)
        sock.send(self.state())
        process.communicate(timeout=3)
        self.assertEqual(process.returncode, 1)
        self.assertEqual(self.capture.stat().st_size, 16 * 1024 * 1024)


if __name__ == "__main__":
    unittest.main()
