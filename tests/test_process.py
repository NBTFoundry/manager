# SPDX-License-Identifier: MPL-2.0
import os
from pathlib import Path
import shutil
import signal
import subprocess
import tempfile
import time
import unittest

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build"))


# Follows the lifecycle contract but ignores SIGTERM.
STUBBORN = """#!/usr/bin/python3
import fcntl, os, signal, time
signal.signal(signal.SIGTERM, signal.SIG_IGN)
fcntl.lockf(4, fcntl.LOCK_SH)
while True:
    os.write(3, b"R")
    time.sleep(0.2)
"""


class ManagedProcessTests(unittest.TestCase):
    def run_probe(self, mode, program=None):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "managed"
            for part in ("apps/test/bin", "state/test"):
                (root / part).mkdir(parents=True, mode=0o700)
            for path in [root, *root.rglob("*")]:
                path.chmod(0o700)
            app = root / "apps/test/bin/app"
            if program:
                app.write_text(program)
            else:
                shutil.copyfile(BUILD / "test-app-host", app)
            app.chmod(0o700)
            result = subprocess.run([BUILD / "process-probe", root, "test", mode], capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)

    def test_gamepad_descriptor_survives_sandbox_mapping_without_status_or_most(self):
        program = """#!/usr/bin/python3
import fcntl, os, socket, time
assert os.environ["NBT_GAMEPAD_FD"] == "7"
for fd in (5, 6):
    try:
        os.fstat(fd)
    except OSError:
        pass
    else:
        raise AssertionError("ungranted descriptor")
controller = socket.socket(fileno=7)
assert controller.getsockopt(socket.SOL_SOCKET, socket.SO_TYPE) == socket.SOCK_DGRAM
controller.send(b"gamepad")
fcntl.lockf(4, fcntl.LOCK_SH)
while True:
    os.write(3, b"R")
    time.sleep(0.2)
"""
        self.run_probe("gamepad", program)

    def test_start_health_contract_and_stop(self):
        self.run_probe("stop")

    def test_install_check_output_and_crash_reach_the_application_log(self):
        # The worker runs an install's health check without a host; in the car
        # (dev58 to dev61) every log read "No log yet" after an install.
        crash = ("#!/usr/bin/python3\nimport fcntl, os, signal, sys\n"
                 "fcntl.lockf(4, fcntl.LOCK_SH)\nos.write(3, b'R')\n"
                 "print('checking out', flush=True)\nsys.stderr.write('[demo] FATAL sig=11\\n')\n"
                 "sys.stderr.flush()\nos.kill(os.getpid(), signal.SIGSEGV)\n")
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "managed"
            for part in ("apps/test/bin", "state/test"):
                (root / part).mkdir(parents=True, mode=0o700)
            for path in [root, *root.rglob("*")]:
                path.chmod(0o700)
            app = root / "apps/test/bin/app"
            app.write_text(crash)
            app.chmod(0o700)
            result = subprocess.run([BUILD / "process-probe", root, "test", "check"], capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            self.assertNotIn(b"checking out", result.stdout + result.stderr)
            lines = (root / "state/test/output.log").read_text().splitlines()
            self.assertRegex(lines[0], r"^--- \d{4}-\d\d-\d\d \d\d:\d\d:\d\d install check of v2 started \(pid \d+\)$")
            self.assertEqual(lines[1:3], ["checking out", "[demo] FATAL sig=11"])
            self.assertTrue(lines[3].endswith(" ended by signal 11 (SIGSEGV, invalid memory access)"), lines)
            self.assertTrue(lines[4].endswith(" install check failed"), lines)
            self.assertEqual(len(lines), 5, lines)

    def test_sandbox_enforces_limits_priority_and_kills_process_group(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory) / "managed"
            for part in ("apps/test/bin", "state/test"):
                (root / part).mkdir(parents=True, mode=0o700)
            for path in [root, *root.rglob("*")]:
                path.chmod(0o700)
            report = Path(directory) / "report.txt"
            helper_pid = Path(directory) / "helper.pid"
            app = root / "apps/test/bin/app"
            app.write_text(f'''#!/usr/bin/python3
import fcntl, os, resource, signal, subprocess, time
fcntl.lockf(4, fcntl.LOCK_SH)
helper = subprocess.Popen(["/usr/bin/python3", "-c", "import time; time.sleep(60)"])
with open({str(helper_pid)!r}, "w") as f:
    f.write(str(helper.pid))
with open({str(report)!r}, "w") as f:
    core = resource.getrlimit(resource.RLIMIT_CORE)
    nofile = resource.getrlimit(resource.RLIMIT_NOFILE)
    data = resource.getrlimit(resource.RLIMIT_DATA)
    nice = os.nice(0)
    usr1_dfl = signal.getsignal(signal.SIGUSR1) == signal.SIG_DFL
    hup_dfl = signal.getsignal(signal.SIGHUP) == signal.SIG_DFL
    f.write(f"{{core[1]}} {{nofile[1]}} {{data[1]}} {{nice}} {{usr1_dfl and hup_dfl}}\\n")
while True:
    os.write(3, b"R")
    time.sleep(0.2)
''')
            app.chmod(0o700)
            parent_nice = os.nice(0)

            def ignore_signals():
                signal.signal(signal.SIGUSR1, signal.SIG_IGN)
                signal.signal(signal.SIGHUP, signal.SIG_IGN)

            result = subprocess.run([BUILD / "process-probe", root, "test", "stop"],
                                    preexec_fn=ignore_signals, capture_output=True, timeout=20)
            self.assertEqual(result.returncode, 0, result.stderr)
            core, nofile, data, child_nice, signals_dfl = report.read_text().strip().split()
            self.assertEqual(int(core), 0)
            self.assertLessEqual(int(nofile), 128)
            if os.environ.get("NBT_TEST_ASAN") == "1":
                self.assertEqual(int(data), -1)  # instrumented test children require shadow memory
            else:
                self.assertLessEqual(int(data), 256 * 1024 * 1024)
            self.assertGreaterEqual(int(child_nice), min(19, parent_nice + 5))
            self.assertEqual(signals_dfl, "True")
            # Process group kill terminated the grandchild helper too.
            pid = int(helper_pid.read_text().strip())
            deadline = time.monotonic() + 5
            while time.monotonic() < deadline:
                try:
                    os.kill(pid, 0)
                    time.sleep(0.05)
                except ProcessLookupError:
                    break
            else:
                os.kill(pid, 9)
                self.fail(f"grandchild helper {pid} survived process group stop")

    def test_non_blocking_termination(self):
        self.run_probe("terminate")

    def test_stubborn_child_is_killed_after_grace_period(self):
        self.run_probe("stubborn", STUBBORN)


if __name__ == "__main__":
    unittest.main()
