import os
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build"))
SHIM_TEST = BUILD / "capture-shim-test"
OBSERVER_SO = BUILD / "libnbt-capture-observer.so"
ACTIVE_SO = BUILD / "libnbt-capture-active.so"


class CaptureShimTests(unittest.TestCase):
    def run_shim(self, preload, mode="observer", exe_name="capture-shim-test", hmi_env=b""):
        with tempfile.TemporaryDirectory() as tmp:
            t = Path(tmp)
            if hmi_env:
                (t / "hmi.env").write_bytes(hmi_env)
            env = dict(os.environ)
            env["NBT_TEST_SHIM_HMI_ENV"] = str(t / "hmi.env")
            env["LD_PRELOAD"] = str(preload)
            env["LD_LIBRARY_PATH"] = f"{BUILD}:{env.get('LD_LIBRARY_PATH', '')}"
            env["NBT_TEST_CAPTURE_EXE"] = exe_name
            env["NBT_TEST_CAPTURE_CONTROL"] = str(t / "ctrl")
            env["NBT_TEST_GUI_LIVENESS"] = str(t / "live")
            env["NBT_TEST_CAPTURE_CLOCK_FILE"] = str(t / "clock")
            env["NBT_TEST_CAPTURE_DIAG"] = str(t / "diag")
            env["NBT_TEST_DSI_BASE_LIB"] = str(BUILD / "libfake_dsi_base.so")
            env["NBT_FAKE_DSI_BASE_LOG"] = str(t / "forward.log")
            args = [str(SHIM_TEST)] + (["active"] if mode == "active" else [])
            res = subprocess.run(args, env=env, capture_output=True, text=True, check=False)
            res.hmi_env_left = (t / "hmi.env").exists()
            return res, t

    def test_observer_shim_forwards_and_records_diag(self):
        res, _ = self.run_shim(OBSERVER_SO, mode="observer")
        self.assertEqual(res.returncode, 0, res.stderr + res.stdout)
        self.assertIn("capture-shim-test: OK", res.stdout)

    def test_shim_deletes_idrives_environment_file_only_inside_idrive(self):
        # Loaded into iDrive through the file, it deletes it before main, so a
        # start that fails is followed by a stock one (docs/hmi-restart.md).
        for build in (OBSERVER_SO, ACTIVE_SO):
            res, _ = self.run_shim(build, mode="observer" if build == OBSERVER_SO else "active",
                                   hmi_env=b"LD_PRELOAD=x\n")
            self.assertEqual(res.returncode, 0, res.stderr + res.stdout)
            self.assertFalse(res.hmi_env_left, build)
            res, _ = self.run_shim(build, exe_name="NotIdrive", hmi_env=b"LD_PRELOAD=x\n")
            self.assertTrue(res.hmi_env_left, build)

    def test_active_shim_suppresses_expires_and_escapes(self):
        res, _ = self.run_shim(ACTIVE_SO, mode="active")
        self.assertEqual(res.returncode, 0, res.stderr + res.stdout)
        self.assertIn("capture-shim-test: OK", res.stdout)

    def test_wrong_process_forwards_everything_in_active_mode(self):
        # Matching NBT_TEST_CAPTURE_EXE="HmiMain3D" against test binary "capture-shim-test"
        # proves non-HmiMain3D processes bypass filtering completely.
        res, _ = self.run_shim(ACTIVE_SO, mode="observer", exe_name="HmiMain3D")
        self.assertEqual(res.returncode, 13, f"expected only diag absence exit=13, got {res.returncode}: {res.stderr}")


    def selftest(self, shim, **env):
        with tempfile.TemporaryDirectory() as tmp:
            os.chmod(tmp, 0o700)
            res = subprocess.run([str(BUILD / "launcher-host"), "--capture-selftest", str(shim), tmp],
                                 env=dict(os.environ, **env), capture_output=True, text=True, check=False)
            self.assertEqual(os.listdir(tmp), [])  # its control, heartbeat and diag files removed
            return res

    def test_live_selftest_passes_for_both_builds(self):
        for so, mode in ((OBSERVER_SO, "observer"), (ACTIVE_SO, "active")):
            res = self.selftest(so)
            self.assertEqual((res.returncode, res.stdout.strip()), (0, f"CAPTURE_SELFTEST OK {mode}"), res.stdout)

    def test_live_selftest_refuses_without_entry_or_inside_hmimain3d(self):
        res = self.selftest(BUILD / "libfake_dsi_base.so")
        self.assertEqual(res.returncode, 1)
        self.assertIn("FAILED the shim has no self-test entry", res.stdout)
        # The process name the shim acts in (HmiMain3D on the car) never enters the self-test.
        res = self.selftest(OBSERVER_SO, NBT_TEST_CAPTURE_EXE="launcher-host")
        self.assertEqual(res.returncode, 1)
        self.assertIn("FAILED the shim refused the self-test", res.stdout)


if __name__ == "__main__":
    unittest.main()
