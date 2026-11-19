# SPDX-License-Identifier: MPL-2.0
import json
import os
from pathlib import Path
import select
import socket
import subprocess
import tempfile
import time
import unittest

from test_layer_manager import fake_layer_manager

BUILD = Path(os.environ.get("NBT_BUILD_DIR", Path(__file__).resolve().parents[1] / "build"))


class GamepadOwnershipTests(unittest.TestCase):
    """One USB owner, the GUI, for its whole life; application hosts relay its
    published state (src/gamepad/relay.cpp)."""

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.root = Path(self.tmp.name)
        (self.root / "state/manager").mkdir(parents=True, mode=0o700)
        (self.root / "state").chmod(0o700)
        self.shared = self.root / "state/manager/pad-state"
        self.children = []
        self.sockets = []

    def tearDown(self):
        for process in self.children:
            if process.poll() is None:
                process.stdin.write(b"q")
                process.stdin.flush()
            try:
                process.communicate(timeout=4)
            except subprocess.TimeoutExpired:
                process.kill()
                process.communicate()
        for client in self.sockets:
            client.close()
        self.tmp.cleanup()

    def spawn(self, args, env=None):
        host, client = socket.socketpair(socket.AF_UNIX, socket.SOCK_DGRAM)
        process = subprocess.Popen([BUILD / "gamepad-test", *args(host.fileno())],
                                   pass_fds=(host.fileno(),), stdin=subprocess.PIPE,
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                   env=dict(os.environ, **(env or {})))
        host.close()
        self.children.append(process)
        self.sockets.append(client)
        return process, client

    def service(self):
        return self.spawn(lambda fd: ["--serve", str(self.root), str(fd), str(self.shared)])

    def state(self, client, connected, timeout=2):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            ready, _, _ = select.select([client], [], [], deadline - time.monotonic())
            if not ready:
                break
            packet = client.recv(25)
            self.assertEqual(len(packet), 24)
            if packet[2] == connected:
                return True
        return False

    def report(self, process):
        process.stdin.write(b"r")
        process.stdin.flush()
        return process.stdout.readline().decode().split()

    def test_the_owner_keeps_the_pads_until_it_ends(self):
        first, one = self.service()
        self.assertTrue(self.state(one, 1))
        second, two = self.service()
        self.assertFalse(self.state(two, 1, timeout=0.5))
        # The Status page names the cause: another process holds the pads.
        self.assertEqual(self.report(second), ["report", "1", "0", "1"])
        self.assertEqual(self.report(first), ["report", "1", "1", "0"])
        first.stdin.write(b"q")
        first.stdin.flush()
        first.wait(timeout=4)
        self.assertTrue(self.state(two, 1))
        self.assertIsNone(second.poll())
        self.assertEqual(self.report(second), ["report", "1", "1", "0"])

    def test_process_death_releases_kernel_ownership(self):
        first, one = self.service()
        self.assertTrue(self.state(one, 1))
        _, two = self.service()
        self.assertFalse(self.state(two, 1, timeout=0.25))
        first.kill()
        first.wait(timeout=2)
        self.assertTrue(self.state(two, 1))

    def test_relay_passes_the_owners_pads_only_while_shown(self):
        env = fake_layer_manager(self.tmp.name)
        lm_state = Path(env["NBT_FAKE_LM_STATE"])
        lm_state.write_text(json.dumps({"order": [1291], "visible": {"1291": 1}, "bound_after": 0}))
        relay, relayed = self.spawn(lambda fd: ["--relay", str(fd), str(self.shared)], env)
        # No owner yet: the application sees no pad.
        self.assertTrue(self.state(relayed, 0))
        _, owner = self.service()
        self.assertTrue(self.state(owner, 1))
        self.assertTrue(self.state(relayed, 1))
        # Paused (iDrive took the screen): no pad for the application.
        relay.stdin.write(b"p")
        relay.stdin.flush()
        self.assertTrue(self.state(relayed, 0))
        relay.stdin.write(b"a")
        relay.stdin.flush()
        self.assertTrue(self.state(relayed, 1))
        # The application's surface left the panel: no pad either.
        lm_state.write_text(json.dumps({"order": [256], "visible": {"256": 1}, "bound_after": 0}))
        self.assertTrue(self.state(relayed, 0))
