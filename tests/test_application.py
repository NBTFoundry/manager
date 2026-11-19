import fcntl
import os
from pathlib import Path
import select
import signal
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
APP = Path(os.environ.get("NBT_BUILD_DIR", ROOT / "build")) / "test-app-host"


class TestApplication(unittest.TestCase):
    def test_ready_and_lifetime_lease(self):
        with tempfile.TemporaryFile() as lease:
            read_fd, write_fd = os.pipe()
            child = subprocess.Popen([APP, "--ready-fd", str(write_fd), "--lease-fd", str(lease.fileno())],
                                     pass_fds=(write_fd, lease.fileno()), stdout=subprocess.PIPE,
                                     stderr=subprocess.PIPE)
            os.close(write_fd)
            try:
                self.assertTrue(select.select([read_fd], [], [], 5)[0])
                self.assertEqual(os.read(read_fd, 1), b"R")
                with self.assertRaises(BlockingIOError):
                    fcntl.lockf(lease.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
                child.send_signal(signal.SIGTERM)
                _, stderr = child.communicate(timeout=5)
                self.assertEqual(child.returncode, 0, stderr)
                fcntl.lockf(lease.fileno(), fcntl.LOCK_EX | fcntl.LOCK_NB)
            finally:
                os.close(read_fd)
                if child.poll() is None:
                    child.kill()
                    child.wait()

    def test_rejects_invalid_descriptor_arguments(self):
        result = subprocess.run([APP, "--ready-fd", "3", "--lease-fd", "3"], capture_output=True)
        self.assertEqual(result.returncode, 2, result.stderr)


if __name__ == "__main__":
    unittest.main()
