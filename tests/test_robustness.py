# SPDX-License-Identifier: MPL-2.0
import http.client
import os
from pathlib import Path
import resource
import signal
import socket
import subprocess
import tempfile
import time
import unittest


ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build"))
DAEMON = BUILD / "managerd-test"
GUI = BUILD / "manager-gui-host"


class DaemonRobustnessTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name) / "managed"
        (self.root / "state/manager").mkdir(parents=True, mode=0o700)
        for path in [self.root, *self.root.rglob("*")]:
            if path.is_dir():
                path.chmod(0o700)
        (self.root / "trust").mkdir(mode=0o700)
        try:
            self.parent, child = socket.socketpair()
            self.parent.sendall(b"x")
            child.recv(1)
        except PermissionError:
            self.parent.close()
            child.close()
            self.tmp.cleanup()
            self.skipTest("sandbox blocks local socket traffic")
        self.parent.settimeout(5)

        def limit_descriptors():
            resource.setrlimit(resource.RLIMIT_NOFILE, (32, 32))

        self.daemon = subprocess.Popen(
            [DAEMON, "--control-fd", str(child.fileno()), "--root", self.root],
            pass_fds=(child.fileno(),), stdout=subprocess.PIPE, stderr=subprocess.PIPE,
            preexec_fn=limit_descriptors)
        child.close()
        self.stream = self.parent.makefile("rb")
        self.peers = []
        self.assertEqual(self.command("HELLO 1"), "HELLO\t1")

    def tearDown(self):
        for peer in self.peers:
            peer.close()
        if self.daemon.poll() is None:
            try:
                self.command("QUIT")
            except (OSError, ValueError):
                pass
        self.stream.close()
        self.parent.close()
        if self.daemon.poll() is None:
            self.daemon.kill()
        _, stderr = self.daemon.communicate(timeout=5)
        self.tmp.cleanup()
        self.assertEqual(stderr, b"")

    def command(self, line):
        self.parent.sendall(line.encode() + b"\n")
        reply = self.stream.readline()
        self.assertNotEqual(reply, b"")
        return reply.decode().rstrip("\n")

    def peer(self):
        address, port = self.host.rsplit(":", 1)
        connection = socket.create_connection((address, int(port)), timeout=2)
        connection.settimeout(2)
        self.peers.append(connection)
        return connection

    def test_hostile_http_peers_do_not_block_new_requests(self):
        fields = self.command("SESSION lo 0").split("\t")
        self.assertEqual(fields[0], "SESSION")
        self.host = fields[1]
        self.peer()  # Half-open: connect without sending a request.
        slow = self.peer()
        slow.sendall(("GET / HTTP/1.1\r\nHost: " + self.host + "\r\n").encode())

        for request in (b"garbage\r\n\r\n",
                        b"GET /" + b"x" * 9000 + b" HTTP/1.1\r\nHost: " + self.host.encode() + b"\r\n\r\n"):
            client = self.peer()
            client.sendall(request)
            self.assertIn(b" 400 ", client.recv(4096))

        for _ in range(24):
            self.peer()
        self.assertEqual(self.command("STATUS").split("\t")[:3], ["STATUS", "1", "-"])
        self.assertIsNone(self.daemon.poll())

        for peer in self.peers:
            peer.close()
        self.peers = []
        deadline = time.monotonic() + 5
        while True:
            try:
                connection = http.client.HTTPConnection(self.host, timeout=2)
                connection.request("GET", "/v1/nonce")
                response = connection.getresponse()
                body = response.read()
                connection.close()
                self.assertEqual(response.status, 200)
                self.assertEqual(body, b"NBT-REQ-1\n")
                break
            except (ConnectionError, OSError):
                if time.monotonic() >= deadline:
                    raise
                time.sleep(0.05)
        self.assertEqual(self.command("STATUS").split("\t")[:3], ["STATUS", "1", "-"])

    def test_fragmented_and_unknown_control_commands_keep_the_session_alive(self):
        self.parent.sendall(b"STAT")
        self.parent.sendall(b"US\nGARBAGE command\nSTATUS\n")
        self.assertTrue(self.stream.readline().startswith(b"STATUS\t"))
        self.assertEqual(self.stream.readline(), b"ERR invalid-command\n")
        self.assertTrue(self.stream.readline().startswith(b"STATUS\t"))
        self.assertIsNone(self.daemon.poll())


class WorkerWatchdogTests(unittest.TestCase):
    def test_stopped_worker_ends_the_gui(self):
        # A hung worker times out the GUI's request; the GUI ends instead of
        # starting another worker (the launcher restarts both).
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "root"
            (root / "manager").mkdir(parents=True, mode=0o700)
            root.chmod(0o700)
            worker = Path(directory) / "worker"
            worker.write_text('''#!/usr/bin/python3
import os, sys
fd = int(sys.argv[2])
root = sys.argv[4]
with open(root + "/starts", "a") as output:
    output.write(str(os.getpid()) + "\\n")
request = b""
while not request.endswith(b"\\n"):
    data = os.read(fd, 1)
    if not data:
        sys.exit(1)
    request += data
if request != b"HELLO 1\\n":
    sys.exit(1)
os.write(fd, b"HELLO\\t1\\n")
pending = b""
while True:
    data = os.read(fd, 1024)
    if not data:
        break
    pending += data
    while b"\\n" in pending:
        line, pending = pending.split(b"\\n", 1)
        if line == b"LIST":
            os.write(fd, b"APPS\\t0\\n")
        elif line == b"RUNNING status":
            os.write(fd, b"RUNNING\\t0\\n")
        elif line == b"PHONE STATUS":
            os.write(fd, b"PHONE\\tnone\\n")
        elif line == b"SETUP":
            os.write(fd, b"SETUP\\tcurrent\\n")
        elif line == b"STATUS":
            os.write(fd, b"STATUS\\t0\\t-\\t-\\t-\\t0\\t-\\tIdle\\n")
        else:
            sys.exit(1)
open(root + "/stopped", "w").close()
''')
            worker.chmod(0o700)
            from test_layer_manager import fake_layer_manager
            env = dict(os.environ, NBT_FAKE_SCREEN_LOG=str(Path(directory) / "screen.log"),
                       **fake_layer_manager(directory, bound_after=1))
            process = subprocess.Popen([GUI, "--test-root", root, "--test-worker", worker], env=env)
            try:
                deadline = time.monotonic() + 8
                starts = root / "starts"
                while not starts.exists() or not starts.read_text().splitlines():
                    self.assertIsNone(process.poll())
                    self.assertLess(time.monotonic(), deadline)
                    time.sleep(0.02)
                stopped = int(starts.read_text().splitlines()[0])
                os.kill(stopped, signal.SIGSTOP)
                self.assertEqual(process.wait(timeout=15), 1)
                self.assertEqual(len(starts.read_text().splitlines()), 1)
            finally:
                if process.poll() is None:
                    process.terminate()
                process.wait(timeout=10)


if __name__ == "__main__":
    unittest.main()
