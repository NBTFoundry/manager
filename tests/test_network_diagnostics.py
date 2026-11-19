# SPDX-License-Identifier: MPL-2.0
"""The read-only car probe reports configuration without exposing stored keys."""
import os
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class NetworkDiagnosticsTests(unittest.TestCase):
    def test_probe_omits_secrets_and_preserves_files(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            fixtures = {
                "var/opt/conn/WFDPass.conf": "PASSPHRASE_CANARY\n",
                "var/opt/conn/WFDWps.conf": "WPS_PIN_CANARY\n",
                "var/opt/conn/wifidir_init.conf": (
                    "DEVICE_NAME=TestCar\nSSID_SUFFIX=TestCar\n"
                    "OP_CHANNEL_NUMBER=6\nNETWORK_KEY=NETWORK_KEY_CANARY\n"),
                "var/opt/conn/dhcpd.wfd0.conf": "secret DHCP_CONFIG_CANARY\n",
                "var/opt/conn/ConfigGen_Options.ini": (
                    "NetworkingService_wfd0.autogo = yes\n"
                    "NetworkingService_wfd0.ipaddress = 172.16.222.1\n"
                    "Unrelated.secret=UNRELATED_CANARY\n"),
                "var/opt/sys/persistence/early/pers_ConnectivityWlanPersistencyPersMapper":
                    "PROFILE_CANARY\n",
                "var/opt/nbt-manager/state/manager/activation": "phase=pending\nslot=1\n",
            }
            for name, data in fixtures.items():
                path = root / name
                path.parent.mkdir(parents=True, exist_ok=True)
                path.write_text(data)
            binary = root / "bin"
            binary.mkdir()
            for name, text in (("ifconfig", "echo interface-probe"),
                               ("pidin", "echo '123 /opt/conn/bin/Connectivity'")):
                path = binary / name
                path.write_text(f"#!/bin/sh\n{text}\n")
                path.chmod(0o700)
            # Rebase read-only absolute paths into the fixture; production has
            # no environment hook and still reads only the fixed car paths.
            script = (ROOT / "tools/car_network_diagnostics.sh").read_text()
            script = re.sub(r"/(?:var/opt|opt/conn)/",
                            lambda match: str(root) + match.group(), script)
            result = subprocess.run(["sh"], input=script, text=True, capture_output=True,
                                    env=dict(os.environ, PATH=f"{binary}:{os.environ['PATH']}"),
                                    timeout=5)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertIn("DEVICE_NAME=TestCar", result.stdout)
            self.assertIn("NetworkingService_wfd0.autogo = yes", result.stdout)
            self.assertIn("phase=pending", result.stdout)
            self.assertNotIn("CANARY", result.stdout + result.stderr)
            self.assertEqual({name: (root / name).read_text() for name in fixtures}, fixtures)
