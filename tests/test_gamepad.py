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

    def state(self, client, connected, timeout=2, family=None):
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            ready, _, _ = select.select([client], [], [], deadline - time.monotonic())
            if not ready:
                break
            packet = client.recv(25)
            self.assertEqual(len(packet), 24)
            if packet[2] == connected and (family is None or packet[3] == family):
                return True
        return False

    def steady(self, client, seconds):
        """True when every state for `seconds` says connected, none more than
        250 ms after the previous one (the relay sends at least every 100 ms)."""
        end = time.monotonic() + seconds
        last = time.monotonic()
        while time.monotonic() < end:
            ready, _, _ = select.select([client], [], [], 0.25)
            if not ready or client.recv(25)[2] != 1:
                return False
            last = time.monotonic()
        return time.monotonic() - last < 0.25

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

    def test_relay_keeps_the_pad_while_layer_manager_is_slow_or_silent(self):
        # dev122: the relay asked LayerManager itself every 250 ms, sent nothing
        # while it waited and dropped the pad when it got no answer, so the
        # application saw the pad come and go and held buttons released.
        env = fake_layer_manager(self.tmp.name)
        lm_state = Path(env["NBT_FAKE_LM_STATE"])
        shown = {"order": [1291], "visible": {"1291": 1}, "bound_after": 0}
        lm_state.write_text(json.dumps(shown))
        _, relayed = self.spawn(lambda fd: ["--relay", str(fd), str(self.shared), "1"], env)
        _, owner = self.service()
        self.assertTrue(self.state(owner, 1))
        self.assertTrue(self.state(relayed, 1))
        lm_state.write_text(json.dumps(dict(shown, layer_hang_s=1.2)))
        self.assertTrue(self.steady(relayed, 2.0))
        lm_state.write_text(json.dumps(dict(shown, layer_fails=1)))
        self.assertTrue(self.steady(relayed, 1.5))
        # An answer still counts: iDrive on the panel stops the pad.
        lm_state.write_text(json.dumps({"order": [256], "visible": {"256": 1}, "bound_after": 0}))
        self.assertTrue(self.state(relayed, 0))

    def test_the_pads_family_reaches_only_bridge_1_5_applications(self):
        env = fake_layer_manager(self.tmp.name)
        Path(env["NBT_FAKE_LM_STATE"]).write_text(
            json.dumps({"order": [1291], "visible": {"1291": 1}, "bound_after": 0}))
        _, new = self.spawn(lambda fd: ["--relay", str(fd), str(self.shared), "1"], env)
        _, old = self.spawn(lambda fd: ["--relay", str(fd), str(self.shared), "0"], env)
        _, owner = self.service()
        # The simulated pad is a DualShock 4 (family 2): the GUI and a bridge
        # 1.5 application see it; an older one gets byte 3 at 0, as it requires.
        self.assertTrue(self.state(owner, 1, family=2))
        self.assertTrue(self.state(new, 1, family=2))
        self.assertTrue(self.state(old, 1, family=0))
        self.assertFalse(self.state(old, 1, timeout=0.5, family=2))
