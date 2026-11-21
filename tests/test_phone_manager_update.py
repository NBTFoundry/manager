# SPDX-License-Identifier: MPL-2.0
"""A manager release sent by a paired phone (src/worker/manager_upload.cpp):
reviewed and approved on the car, staged by the launcher at the next opening
(src/launcher/stage_request.cpp)."""
import hashlib
import os
from pathlib import Path
import shutil
import socket
import subprocess
import tempfile
import unittest
from phone_client import SoftwarePhone

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get('NBT_BUILD_DIR', ROOT / 'build'))
PACK = ROOT / 'tools/package_release.py'


def run(*args):
    return subprocess.run([str(a) for a in args], capture_output=True, check=False)


class PhoneManagerUpdateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assets = tempfile.TemporaryDirectory()
        cls.path = Path(cls.assets.name)
        for name in ('manager', 'other'):
            key = cls.path / f'{name}.pem'
            for args in (('genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:3072', '-out', key),
                         ('pkey', '-in', key, '-pubout', '-out', cls.path / f'{name}.pub')):
                result = run('openssl', *args)
                if result.returncode:
                    raise RuntimeError(result.stderr.decode())
        # Each release has its own launcher, GUI and worker; the plugin is shared.
        for sequence, signer in ((1, 'manager'), (2, 'manager'), (3, 'manager'), (4, 'other')):
            folder = cls.path / f'parts-{sequence}'
            plugin = folder / 'plugin'
            plugin.mkdir(parents=True)
            elf = bytearray(128)
            elf[:7] = b'\x7fELF\x01\x01\x01'
            elf[16:20] = b'\x02\x00\x28\x00'
            elf[-1] = sequence
            for name in ('launcher', 'gui', 'worker'):
                elf[-2] = len(name)
                (folder / name).write_bytes(elf)
            elf[16], elf[-1], elf[-2] = 3, 0, 0
            (plugin / 'libNbtManager.so').write_bytes(elf)
            for name in ('NbtManager.manifest', 'NbtManager.xml', 'NbtManager_ALL_Texts.zip',
                         'NbtManager_ALL_Images.zip'):
                (plugin / name).write_bytes(name.encode())
            result = run('python3', PACK, '--version', f'v{sequence}', '--sequence', sequence,
                         '--compat', 'demo-1', '--key', cls.path / f'{signer}.pem',
                         '--output', cls.path / f'release-{sequence}', '--launcher', folder / 'launcher',
                         '--gui', folder / 'gui', '--worker', folder / 'worker', '--plugin-dir', plugin)
            if result.returncode:
                raise RuntimeError(result.stderr.decode())

    @classmethod
    def tearDownClass(cls):
        cls.assets.cleanup()

    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / 'managed'
        self.root.mkdir(mode=0o700)
        # A car set up with v2: its launcher is v2's.
        result = run(BUILD / 'manager-setup-probe', self.root, self.path / 'release-2',
                     self.path / 'manager.pub', 'demo-1')
        self.assertEqual(result.returncode, 0, result.stderr)
        for part in ('trust/apps', 'apps', 'data'):
            (self.root / part).mkdir(parents=True, exist_ok=True, mode=0o700)
        self.parent, child = socket.socketpair()
        self.parent.settimeout(5)
        self.worker = subprocess.Popen([BUILD / 'managerd-test', '--control-fd', str(child.fileno()),
                                        '--root', self.root], pass_fds=[child.fileno()],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        child.close()
        self.stream = self.parent.makefile('rb')
        self.addCleanup(self.close_worker)
        self.assertEqual(self.command('HELLO 1'), 'HELLO\t1')
        self.host = self.command('SESSION lo 0').split('\t')[1]
        self.phone = SoftwarePhone(self.tmp.name)
        self.phone.pair(self.command, self.host)
        self.assertEqual(self.command('CANCEL'), 'OK')

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

    def send(self, sequence):
        self.host = self.command('SESSION lo 0').split('\t')[1]
        body = (self.path / f'release-{sequence}').read_bytes()
        status, data = self.phone.request(self.host, 'POST', '/v1/upload', body)
        self.assertLess(status, 500, data)
        return hashlib.sha256(body).hexdigest(), self.command('STATUS').split('\t')

    def test_newer_release_is_reviewed_approved_and_staged_at_the_next_opening(self):
        digest, status = self.send(3)
        self.assertEqual(status[:7], ['STATUS', '2', 'manager', 'v2', 'v3', '3', digest])
        self.assertEqual(self.command(f'APPROVE {digest} normal'), 'OK installing')
        status = self.command('STATUS').split('\t')
        self.assertEqual(status[1:3], ['4', 'manager'])
        self.assertEqual(status[-1], 'NBT Manager v3 starts when App Manager opens again')
        state = self.root / 'state/manager'
        self.assertEqual((state / 'release.upload').read_bytes(), (self.path / 'release-3').read_bytes())
        self.assertEqual((state / 'stage.request').read_text(), f'NBTSTAGE1\nsha256={digest}\n')
        self.assertFalse((state / 'session.upload').exists())
        # The launcher's step at the next opening: staged, then supervised on trial.
        staged = run(BUILD / 'manager-stage-probe', self.root, 'requested')
        self.assertEqual((staged.returncode, staged.stdout), (0, b'1\n'), staged.stderr)
        self.assertFalse((state / 'stage.request').exists())
        loaded = run(BUILD / 'manager-store-probe', self.root, 'load', 1)
        record = dict(line.split('=', 1) for line in loaded.stdout.decode().splitlines()[1:])
        self.assertEqual((record['phase'], int(record['sequence'])), ('pending', 3))
        again = run(BUILD / 'manager-stage-probe', self.root, 'requested')
        self.assertEqual((again.returncode, again.stdout), (0, b'0\n'))

    def rejected(self, sequence, reason):
        _, status = self.send(sequence)
        self.assertEqual(status[1], '5')
        self.assertEqual(status[-1], f'Package rejected: {reason}')
        self.assertFalse((self.root / 'state/manager/stage.request').exists())
        self.assertFalse((self.root / 'state/manager/session.upload').exists())

    def test_older_release_is_refused(self):
        self.rejected(1, 'this NBT Manager is not newer than the installed one')

    def test_release_signed_with_another_key_is_refused(self):
        self.rejected(4, 'not an NBT Manager release for this car')

    def test_an_older_launcher_needs_the_installer_once(self):
        launcher = self.root / 'manager/nbt-manager-launcher'
        shutil.copyfile(self.path / 'parts-1/launcher', launcher)
        launcher.chmod(0o700)
        self.rejected(3, 'update NBT Manager once with the installer first')

    def test_v2_release_requires_compatible_launcher(self):
        cap = self.path / 'cap.so'
        elf = bytearray(128)
        elf[:7] = b'\x7fELF\x01\x01\x01'
        elf[16:20] = b'\x03\x00\x28\x00'
        cap.write_bytes(elf)
        v2_file = self.path / 'release-v2'
        parts = self.path / 'parts-3'
        res = run('python3', PACK, '--version', 'v4', '--sequence', 4, '--compat', 'demo-1',
                  '--key', self.path / 'manager.pem', '--output', v2_file,
                  '--launcher', parts / 'launcher', '--gui', parts / 'gui',
                  '--worker', parts / 'worker', '--plugin-dir', parts / 'plugin',
                  '--capture-lib', cap)
        self.assertEqual(res.returncode, 0, res.stderr)
        body = v2_file.read_bytes()
        # Dummy 128-byte ARM launcher in manager/ cannot answer --capabilities -> rejected cleanly.
        self.host = self.command('SESSION lo 0').split('\t')[1]
        status, data = self.phone.request(self.host, 'POST', '/v1/upload', body)
        self.assertLess(status, 500, data)
        reply = self.command('STATUS').split('\t')
        self.assertEqual(reply[1], '5')
        self.assertEqual(reply[-1], 'Package rejected: update NBT Manager once with the installer first')

    def test_the_phone_reads_the_installed_manager_and_the_formats_it_takes(self):
        # The installed launcher here is a stand-in that cannot run: NBTRLS1 only.
        self.assertEqual(self.phone.request(self.host, 'GET', '/v1/manager'),
                         (200, b'MANAGER\tv2\t2\tNBTRLS1\n'))
        # A launcher that lists every format.
        launcher = self.root / 'manager/nbt-manager-launcher'
        shutil.copyfile(BUILD / 'launcher-host', launcher)
        launcher.chmod(0o700)
        self.assertEqual(self.phone.request(self.host, 'GET', '/v1/manager'),
                         (200, b'MANAGER\tv2\t2\tNBTRLS1,NBTRLS2,NBTRLS3\n'))
        # An installed release that no longer verifies.
        (self.root / 'trust/manager.pem').write_bytes((self.path / 'other.pub').read_bytes())
        self.assertEqual(self.phone.request(self.host, 'GET', '/v1/manager')[0], 503)

    def test_launcher_capabilities_cli(self):
        res = run(BUILD / 'launcher-host', '--capabilities')
        self.assertEqual(res.returncode, 0, res.stderr)
        self.assertEqual(res.stdout, b'formats=NBTRLS1,NBTRLS2,NBTRLS3\n')


if __name__ == '__main__':
    unittest.main()
