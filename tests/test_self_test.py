# SPDX-License-Identifier: MPL-2.0
"""Settings > Self test, the worker's side (src/worker/self_test*.cpp,
docs/self-test.md): the bundled applications installed from the running
slot's release, hidden, signed with the manager key; the report kept and
summed up; their ids refused from a phone."""
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import unittest
from phone_client import SoftwarePhone

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get('NBT_BUILD_DIR', ROOT / 'build'))
PACK = ROOT / 'tools/package_release.py'
PACKAGE = ROOT / 'tools/package.py'
BUNDLED = (('selftest_native', 'manager-selftest-native'),
           ('selftest_guest', 'manager-selftest-guest'),
           ('selftest', 'manager-selftest'))
READY = 'SELFTEST\tready\tmanager-selftest-native,manager-selftest-guest,manager-selftest'


def run(*args):
    return subprocess.run([str(a) for a in args], capture_output=True, check=False)


def checked(*args):
    result = run(*args)
    if result.returncode:
        raise RuntimeError(result.stderr.decode())


def elf(kind, tag):
    data = bytearray(128)
    data[:7] = b'\x7fELF\x01\x01\x01'
    data[16:20] = bytes((kind, 0, 0x28, 0))
    data[-1] = tag
    return bytes(data)


class SelfTestTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.assets = tempfile.TemporaryDirectory()
        cls.path = Path(cls.assets.name)
        for name in ('manager', 'apps'):
            key = cls.path / f'{name}.pem'
            checked('openssl', 'genpkey', '-algorithm', 'RSA', '-pkeyopt', 'rsa_keygen_bits:3072',
                    '-out', key)
            checked('openssl', 'pkey', '-in', key, '-pubout', '-out', cls.path / f'{name}.pub')
        parts = cls.path / 'parts'
        plugin = parts / 'plugin'
        plugin.mkdir(parents=True)
        for index, name in enumerate(('launcher', 'gui', 'worker')):
            (parts / name).write_bytes(elf(2, index))
        (plugin / 'libNbtManager.so').write_bytes(elf(3, 9))
        for name in ('NbtManager.manifest', 'NbtManager.xml', 'NbtManager_ALL_Texts.zip',
                     'NbtManager_ALL_Images.zip'):
            (plugin / name).write_bytes(name.encode())
        program = cls.path / 'program'
        program.write_bytes(elf(2, 42))
        # The bundled applications, signed with the manager key, as
        # tools/bundled_apps.py makes them (here three native programs).
        components = []
        for component, app in BUNDLED:
            output = cls.path / f'{component}.nbt'
            checked('python3', PACKAGE, '--app', app, '--version', 'v2', '--sequence', 2,
                    '--compat', 'nbt_6.5_armv7', '--key', cls.path / 'manager.pem',
                    '--bridge', '1.9', '--bridge-uses', 'show,status', '--output', output,
                    program)
            components += ['--component', f'{component}={output}']
        # A phone's package for a bundled id, signed with the apps key.
        checked('python3', PACKAGE, '--app', 'manager-selftest', '--version', 'v9', '--sequence',
                9, '--compat', 'nbt_6.5_armv7', '--key', cls.path / 'apps.pem', '--output',
                cls.path / 'phone.nbt', program)
        common = ['--compat', 'demo-1', '--key', cls.path / 'manager.pem', '--launcher',
                  parts / 'launcher', '--gui', parts / 'gui', '--worker', parts / 'worker',
                  '--plugin-dir', plugin]
        checked('python3', PACK, '--version', 'v2', '--sequence', 2, '--output',
                cls.path / 'release-bundled', *common, *components)
        checked('python3', PACK, '--version', 'v2', '--sequence', 2, '--output',
                cls.path / 'release-plain', *common)

    @classmethod
    def tearDownClass(cls):
        cls.assets.cleanup()

    def start(self, release):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / 'managed'
        self.root.mkdir(mode=0o700)
        result = run(BUILD / 'manager-setup-probe', self.root, self.path / release,
                     self.path / 'manager.pub', 'demo-1')
        self.assertEqual(result.returncode, 0, result.stderr)
        for part in ('trust/apps', 'apps', 'data'):
            (self.root / part).mkdir(parents=True, exist_ok=True, mode=0o700)
        files = Path(self.tmp.name) / 'disk'
        files.mkdir(mode=0o700)
        self.parent, child = socket.socketpair()
        self.parent.settimeout(10)
        self.mixer_state = Path(self.tmp.name) / 'mixer-state'  # absent: no mixer has run
        env = dict(os.environ, NBT_TEST_APPS_KEY=str(self.path / 'apps.pub'),
                   NBT_TEST_FILES_ROOT=str(files), NBT_TEST_MIXER_STATE=str(self.mixer_state))
        self.worker = subprocess.Popen([BUILD / 'managerd-test', '--control-fd', str(child.fileno()),
                                        '--root', self.root], pass_fds=[child.fileno()], env=env,
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE)
        child.close()
        self.stream = self.parent.makefile('rb')
        self.addCleanup(self.close_worker)
        self.assertEqual(self.command('HELLO 1'), 'HELLO\t1')

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

    def report(self, app, text):
        # As the application writes it: its own, 0600 (a group-writable one is refused).
        path = self.root / 'data' / app / 'selftest.txt'
        path.write_text(text)
        path.chmod(0o600)

    def test_bundled_applications_install_hidden_and_report(self):
        self.start('release-bundled')
        self.assertEqual(self.command('SELFTEST REPORT y y'), 'ERR selftest')
        self.assertEqual(self.command('SELFTEST LAST'), 'SELFTEST\tnone')
        host = self.command('SESSION lo 0').split('\t')[1]
        phone = SoftwarePhone(self.tmp.name)
        phone.pair(self.command, host)
        self.assertEqual(phone.request(host, 'GET', '/v1/selftest'), (200, b'SELFTEST1\tnone\n'))
        self.assertEqual(self.command('CANCEL'), 'OK')
        self.mixer_state.write_text('1 2 0\n')  # before the run: level 1, two gaps earlier
        self.assertEqual(self.command('SELFTEST PREPARE'), READY)
        for _, app in BUNDLED:
            profile = (self.root / 'trust/apps' / f'{app}.conf').read_text()
            self.assertEqual(profile, 'NBTAPP1\ncompat=nbt_6.5_armv7\nsigner=manager\n')
            self.assertTrue((self.root / 'apps' / app / 'bin/app').is_file())
        # Hidden from the lists, not from RUNNING (none runs).
        self.assertEqual(self.command('LIST'), 'APPS\t0')
        self.assertEqual(self.command('RUNNING'), 'RUNNING\t0')
        # What the applications found; the guest wrote nothing, the module
        # another form's report (refused), malformed lines are dropped.
        self.report('manager-selftest-native', 'SELFTEST1\tnative\nbridge\tpass\t2 capabilities\n'
                    'screen\tpass\t58 fps\ndiag\tskip\tno gateway\nBad\tpass\tx\nfiles\tmaybe\tx\n')
        self.report('manager-selftest', 'SELFTEST1\tguest\nscreen\tpass\t60 fps\n')
        self.mixer_state.write_text('1 5 1\n')  # while they played: three gaps, a longer queue
        reply = self.command('SELFTEST REPORT y n').split('\t')
        self.assertEqual(reply[0], 'SELFTEST')
        rows = reply[4:]
        self.assertIn('manager.audio|f|level 1, 3 gaps, 1 queues grown', rows)
        self.assertIn('guest.start|f|no report', rows)
        self.assertIn('module.start|f|no report', rows)
        self.assertIn('you.sound|f|no', rows)
        self.assertIn('native.diag|s|no gateway', rows)
        self.assertIn('manager.runner|f|missing', rows)  # no nbt-runner beside the test worker
        failures = [row for row in rows if row.split('|')[1] == 'f']
        self.assertEqual(rows[:len(failures)], failures)  # failures first
        passed = int(reply[1])
        self.assertEqual(int(reply[2]), len(failures))
        # manager.release, files, space; native start, bridge, screen; you.screen.
        self.assertEqual(passed, 7)
        # The phone reads the kept report as it is (GET /v1/selftest).
        host = self.command('SESSION lo 0').split('\t')[1]
        phone = SoftwarePhone(self.tmp.name)
        phone.pair(self.command, host)
        status, body = phone.request(host, 'GET', '/v1/selftest')
        self.assertEqual((status, body), (200, (self.root / 'state/manager/selftest.txt').read_bytes()))
        self.assertEqual(self.command('CANCEL'), 'OK')
        kept = (self.root / 'state/manager/selftest.txt').read_text().splitlines()
        self.assertTrue(kept[0].startswith('SELFTEST1\t') and kept[0].endswith('\tv2'))
        self.assertIn('native.screen\tp\t58 fps', kept)
        self.assertEqual(self.command('SELFTEST LAST').split('\t'), reply)
        # A second run keeps the same installation and forgets the old reports.
        before = (self.root / 'apps/manager-selftest/bin/app').stat().st_ino
        self.assertEqual(self.command('SELFTEST PREPARE'), READY)
        self.assertEqual((self.root / 'apps/manager-selftest/bin/app').stat().st_ino, before)
        self.assertFalse((self.root / 'data/manager-selftest-native/selftest.txt').exists())
        self.assertFalse((self.root / 'state/manager/bundled.nbt').exists())
        # Queues made longer since this second start, and no gap: the mixer
        # settled, the sound check passes and says how many.
        self.mixer_state.write_text('1 5 3\n')
        again = self.command('SELFTEST REPORT y y').split('\t')
        self.assertFalse([row for row in again[4:] if row.startswith('manager.audio')])
        self.assertIn('manager.audio\tp\tlevel 1, 0 gaps, 2 queues grown',
                      (self.root / 'state/manager/selftest.txt').read_text().splitlines())

    def test_release_without_them_reports_so(self):
        self.start('release-plain')
        self.assertEqual(self.command('SELFTEST PREPARE'), 'SELFTEST\tready\t')
        rows = self.command('SELFTEST REPORT - -').split('\t')[4:]
        for group in ('native', 'guest', 'module'):
            self.assertIn(f'{group}.start|s|not in this release', rows)
        self.assertIn('you.screen|s|not asked', rows)
        self.assertIn('manager.audio|s|no mixer', rows)  # no mixer has run

    def test_commands_are_checked(self):
        self.start('release-bundled')
        for bad in ('SELFTEST', 'SELFTEST REPORT', 'SELFTEST REPORT y', 'SELFTEST REPORT yy',
                    'SELFTEST REPORT x y', 'SELFTEST REPORT y y y', 'SELFTEST OTHER'):
            self.assertEqual(self.command(bad), 'ERR invalid-command', bad)

    def test_a_phone_cannot_install_a_bundled_id(self):
        self.start('release-bundled')
        host = self.command('SESSION lo 0').split('\t')[1]
        phone = SoftwarePhone(self.tmp.name)
        phone.pair(self.command, host)
        status, data = phone.request(host, 'POST', '/v1/upload',
                                     (self.path / 'phone.nbt').read_bytes())
        self.assertLess(status, 500, data)
        status = self.command('STATUS').split('\t')
        self.assertEqual((status[1], status[-1]),
                         ('5', 'Package rejected: not an application package'))
        self.assertFalse((self.root / 'trust/apps/manager-selftest.conf').exists())
        self.assertEqual(self.command('CANCEL'), 'OK')


if __name__ == '__main__':
    unittest.main()
