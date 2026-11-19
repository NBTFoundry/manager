"""nbt-managerd --app-exec: the car's limits wrapper (sandbox_limits.hpp).

It sets the application's resource limits, reports them on standard error,
then execve()s the application, which keeps its pid and descriptors."""
import os
from pathlib import Path
import subprocess
import sys
import unittest


ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build"))
DAEMON = BUILD / "managerd-test"


def run(*args):
    return subprocess.run([str(DAEMON), "--app-exec", *args], capture_output=True, text=True,
                          timeout=10)


class AppExecTest(unittest.TestCase):
    def test_limits_are_set_and_reported_then_the_application_runs(self):
        script = ("import os, resource as r, sys\n"
                  "print(os.getpid(), *[r.getrlimit(k)[1] for k in "
                  "(r.RLIMIT_NOFILE, r.RLIMIT_CORE, r.RLIMIT_DATA, r.RLIMIT_FSIZE)])\n"
                  "sys.exit(7)\n")
        python = os.path.realpath(sys.executable)
        child = subprocess.Popen(
            [str(DAEMON), "--app-exec", python, "app-name", "-I", "-c", script],
            stdout=subprocess.PIPE, stderr=subprocess.PIPE, text=True)
        out, err = child.communicate(timeout=10)
        self.assertEqual(child.returncode, 7, err)
        values = [int(v) for v in out.split()]
        # The application is the wrapper itself after execve: the same process.
        self.assertEqual(values[0], child.pid)
        expected_data = -1 if os.environ.get("NBT_TEST_ASAN") == "1" else 256 << 20
        self.assertEqual(values[1:], [128, 0, expected_data, 128 << 20])
        expected = "nbt-managerd: limits: core 0, files 128, file size 128 MiB"
        if expected_data >= 0:
            expected += ", data 256 MiB"
        self.assertEqual(err, expected + "\n")

    def test_the_application_gets_its_own_argv0_and_arguments(self):
        result = run("/bin/sh", "app-name", "-c", "tr '\\0' ' ' < /proc/$$/cmdline", "x")
        self.assertEqual(result.returncode, 0, result.stderr)
        self.assertEqual(result.stdout, "app-name -c tr '\\0' ' ' < /proc/$$/cmdline x ")

    def test_a_missing_application_is_reported_on_standard_error(self):
        result = run("/nonexistent/app", "app")
        self.assertEqual(result.returncode, 127)
        self.assertIn("nbt-managerd: cannot start /nonexistent/app (errno 2)", result.stderr)

    def test_a_relative_path_or_missing_argv0_is_refused(self):
        for args in (("bin/sh", "sh"), ("/bin/sh",)):
            result = run(*args)
            self.assertEqual(result.returncode, 127)
            self.assertIn("--app-exec PATH ARGV0", result.stderr)


if __name__ == "__main__":
    unittest.main()
