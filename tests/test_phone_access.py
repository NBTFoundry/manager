"""Signed requests against the real host worker and door, without a car."""
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest
from phone_client import SoftwarePhone

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get('NBT_BUILD_DIR', ROOT / 'build'))

class PhoneAccessTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / 'managed'
        for part in ('state/manager', 'trust/apps', 'apps', 'data'):
            (self.root / part).mkdir(parents=True, exist_ok=True, mode=0o700)
        for path in [self.root, *self.root.rglob('*')]:
            path.chmod(0o700)
        self.parent, child = socket.socketpair()
        self.parent.settimeout(5)
        # Stands in for the car's sloginfo; each test writes it, or leaves it missing.
        self.sloginfo = Path(self.tmp.name) / 'sloginfo'
        self.worker = subprocess.Popen([BUILD / 'managerd-test', '--control-fd', str(child.fileno()),
                                        '--root', self.root], pass_fds=[child.fileno()],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       env=dict(os.environ, NBT_TEST_SLOGINFO=str(self.sloginfo)))
        child.close()
        self.stream = self.parent.makefile('rb')
        self.addCleanup(self.close_worker)
        self.assertEqual(self.command('HELLO 1'), 'HELLO\t1')
        self.host = self.command('SESSION lo 0').split('\t')[1]
        self.phone = SoftwarePhone(self.tmp.name)

    def close_worker(self):
        if self.worker.poll() is None:
            try:
                self.command('QUIT')
            except OSError:
                self.worker.kill()
        self.stream.close()
        self.parent.close()
        _, err = self.worker.communicate(timeout=5)
        self.assertEqual(self.worker.returncode, 0, err)

    def command(self, text):
        self.parent.sendall(text.encode() + b'\n')
        return self.stream.readline().decode().rstrip('\n')

    def test_pairing_requires_car_window_and_confirmation(self):
        self.assertEqual(self.phone.pair_request(self.host)[0], 403)
        self.assertEqual(self.command('PHONE OPEN'), 'OK')
        status, data = self.phone.pair_request(self.host)
        self.assertEqual(status, 202, data)
        pending = self.command('PHONE STATUS').split('\t')
        self.assertEqual(pending[:2], ['PHONE', 'pair'])
        self.assertEqual(self.phone.request(self.host, 'GET', '/v1/apps')[0], 403)
        self.assertEqual(self.command('PHONE YES ' + '0'*32), 'ERR phones')
        self.assertEqual(self.command('PHONE NO ' + pending[2]), 'OK cancelled')
        status, data = self.phone.request(self.host, 'GET', '/v1/status')
        self.assertEqual(status, 200)
        self.assertIn(b'\tcancelled\t', data)
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/approve')[0], 400)
        self.assertEqual(list((self.root / 'trust/phones').glob('*.conf')), [])

    def test_signed_requests_replay_tampering_unknown_key_and_revocation(self):
        self.phone.pair(self.command, self.host)
        status, data = self.phone.request(self.host, 'GET', '/v1/apps')
        self.assertEqual((status, data), (200, b'APPS\t0\n'))
        used = self.phone.last_nonce
        self.assertEqual(self.phone.request(self.host, 'GET', '/v1/apps', nonce=used)[0], 403)
        self.assertEqual(self.phone.request(self.host, 'GET', '/v1/apps', bad_signature=True)[0], 403)
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/settings/read', b'test\n', signed_body=b'evil\n')[0], 403)
        other = SoftwarePhone(self.tmp.name, 'Unknown phone')
        self.assertEqual(other.request(self.host, 'GET', '/v1/apps')[0], 403)
        self.assertEqual(self.command('PHONE FORGET ' + self.phone.id), 'OK')
        self.assertEqual(self.phone.request(self.host, 'GET', '/v1/apps')[0], 403)
        self.assertEqual(self.phone.request(self.host, 'GET', '/v1/status')[0], 403)

    def test_phone_store_limit_rejection_and_symlink_policy(self):
        for i in range(4):
            phone = SoftwarePhone(self.tmp.name, f'Phone {i}')
            phone.pair(self.command, self.host)
        self.assertEqual(len(list((self.root / 'trust/phones').glob('*.conf'))), 4)
        self.assertEqual(self.command('PHONE OPEN'), 'OK')
        self.assertEqual(self.phone.pair_request(self.host)[0], 409)
        self.assertEqual(self.command('PHONE FORGET ../../outside'), 'ERR phones')
        profile = next((self.root / 'trust/phones').glob('*.conf'))
        backup = profile.read_bytes()
        profile.unlink()
        outside = Path(self.tmp.name) / 'outside'
        outside.write_bytes(backup)
        profile.symlink_to(outside)
        self.assertEqual(self.command('PHONE LIST'), 'ERR phones')
        self.assertEqual(self.command('PHONE FORGET ' + profile.stem), 'ERR phones')
        self.assertEqual(outside.read_bytes(), backup)

    def test_invalid_upload_signature_leaves_installed_state_untouched(self):
        self.phone.pair(self.command, self.host)
        before = self.command('STATUS')
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/upload', b'bad package', bad_signature=True)[0], 403)
        self.assertEqual(self.command('STATUS'), before)
        self.assertFalse((self.root / 'state/manager/phone.upload').exists())

    def test_application_log_is_read_by_a_paired_phone_only(self):
        profile = self.root / 'trust/apps/test.conf'
        profile.write_text('NBTAPP1\ncompat=demo-1\n')
        profile.chmod(0o600)
        (self.root / 'state/test').mkdir(mode=0o700)
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/app', b'test\n')[0], 403)
        self.phone.pair(self.command, self.host)
        # Nothing written yet: an empty log.
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/app', b'test\n'), (200, b''))
        old, current = self.root / 'state/test/output.log.1', self.root / 'state/test/output.log'
        old.write_bytes(b'older line\n' * 3)
        current.write_bytes(b'--- 2026-11-16 10:00:00 started v1 (pid 7)\nnewer \xff line\n')
        for path in (old, current):
            path.chmod(0o600)
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/app', b'test\n'),
                         (200, old.read_bytes() + current.read_bytes()))
        for body in (b'manager\n', b'missing\n', b'../x\n', b'test', b''):
            self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/app', body)[0], 400, body)
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/app', b'test\n',
                                            signed_body=b'other\n')[0], 403)
        # Clearing empties the log in place: a host holding it open keeps appending to it.
        with current.open('ab') as host:
            self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/clear', b'test\n'), (200, b'OK\n'))
            self.assertFalse(old.exists())
            host.write(b'after clearing\n')
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/app', b'test\n'), (200, b'after clearing\n'))
        for body in (b'manager\n', b'missing\n', b'test'):
            self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/clear', body)[0], 400, body)
        # A log that is a link to elsewhere is refused, not followed.
        current.unlink()
        current.symlink_to(Path(self.tmp.name) / 'sloginfo')
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/app', b'test\n')[0], 503)

    def test_manager_log_is_the_debug_log(self):
        # The user reads logs on the phone only: the manager's debug log too.
        self.phone.pair(self.command, self.host)
        (self.root / 'manager').mkdir(mode=0o700, exist_ok=True)
        debug = self.root / 'manager/debug.log'
        old = self.root / 'manager/debug.log.1'
        for path in (debug, old):
            path.unlink(missing_ok=True)
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/manager'), (200, b''))
        old.write_bytes(b'older\n')
        old.chmod(0o600)
        debug.write_bytes(b'newer\n')
        debug.chmod(0o600)
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/manager'), (200, b'older\nnewer\n'))
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/manager', b'x')[0], 400)

    def test_system_log_is_sloginfo_newest_part(self):
        self.phone.pair(self.command, self.host)
        # No sloginfo on this car.
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/system')[0], 503)
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/system', b'x')[0], 400)
        # 9 MiB of lines: the newest 8 MiB arrive.
        self.sloginfo.write_text('#!/usr/bin/python3\nimport sys\n'
                                 'line = b"%07d " + b"x" * 1015 + b"\\n"\n'
                                 'for i in range(9216):\n'
                                 '    sys.stdout.buffer.write(b"%07d " % i + b"x" * 1015 + b"\\n")\n')
        self.sloginfo.chmod(0o700)
        status, data = self.phone.request(self.host, 'POST', '/v1/logs/system')
        self.assertEqual(status, 200)
        self.assertEqual(len(data), 8 * 1024 * 1024)
        self.assertTrue(data.startswith(b'0001024 ') and data.endswith(b'x\n'))
        self.assertIn(b'0009215 ', data[-1024:])
        # A failing sloginfo is an error, and the next request works again.
        self.sloginfo.write_text('#!/bin/sh\necho partial\nexit 3\n')
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/system')[0], 503)
        self.sloginfo.write_text('#!/bin/sh\necho "slog line"\n')
        self.assertEqual(self.phone.request(self.host, 'POST', '/v1/logs/system'), (200, b'slog line\n'))
        self.assertEqual(list(Path('/tmp').glob(f'nbt-manager-slog.{self.worker.pid}')), [])

    def test_door_routes_off_switch_and_manager_lock_independence(self):
        self.phone.pair(self.command, self.host)
        with socket.socket() as available:
            available.bind(('127.0.0.1', 0))
            port = available.getsockname()[1]
        host = f'127.0.0.1:{port}'
        process = subprocess.Popen([BUILD / 'managerd-test', '--door', '--root', self.root,
                                    '--interface', 'lo', '--port', str(port)],
                                   stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        try:
            deadline = time.monotonic()+5
            while True:
                try:
                    status, data = self.phone.request(host, 'GET', '/v1/apps')
                    break
                except OSError:
                    self.assertLess(time.monotonic(), deadline)
                    time.sleep(0.02)
            self.assertEqual((status, data), (200, b'APPS\t0\n'))
            self.assertEqual(self.phone.request(host, 'POST', '/v1/wake'), (200, b'WAKE\toff\n'))
            self.assertEqual(self.phone.request(host, 'POST', '/v1/remove', b'test\n')[0], 403)
            self.assertEqual(self.phone.request(host, 'POST', '/v1/pair', b'x')[0], 403)
            # The door serves the logs too.
            self.assertEqual(self.phone.request(host, 'POST', '/v1/logs/app', b'missing\n')[0], 400)
            self.assertEqual(self.command('SET manager phone_open on'), 'OK')
            self.assertEqual(self.phone.request(host, 'POST', '/v1/wake'), (202, b'WAKE\topening\n'))
            marker = self.root / 'state/manager/phone.wake'
            original = marker.read_bytes()
            self.assertEqual(self.phone.request(host, 'POST', '/v1/wake'), (200, b'WAKE\topening\n'))
            self.assertEqual(marker.read_bytes(), original)
            # The door does not take the manager operation lock.
            import fcntl
            with (self.root / 'state/manager/operation.lock').open('a') as lock:
                fcntl.lockf(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        finally:
            process.terminate()
            _, err = process.communicate(timeout=5)
            self.assertEqual(process.returncode, 0, err)

if __name__ == '__main__':
    unittest.main()
