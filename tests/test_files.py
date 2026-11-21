# SPDX-License-Identifier: MPL-2.0
"""Applications' files from a paired phone (capability `files`), against the
real host worker and a temporary files root (docs/files-bridge.md)."""
import hashlib
import os
from pathlib import Path
import socket
import subprocess
import tempfile
import time
import unittest
import zipfile
from phone_client import SoftwarePhone

ROOT = Path(__file__).resolve().parents[1]
BUILD = Path(os.environ.get('NBT_BUILD_DIR', ROOT / 'build'))
MIB = 1024 * 1024
POLICY = 'sfc,smc|1|2'  # .sfc and .smc, at most 1 MiB each and 2 MiB in all


def record(bridge):
    return ('NBTSTATE%d\nsequence=%020d\nhighwater=%020d\nversion=v1\nsha256=%s\n%s'
            % (4 if 'bridge-files' in bridge else 2, 1, 1, '0' * 64, bridge))


class PhoneFilesTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name) / 'managed'
        self.files = Path(self.tmp.name) / 'disk/nbt-manager'
        self.files.parent.mkdir()
        for part in ('state/manager', 'trust/apps', 'apps', 'data'):
            (self.root / part).mkdir(parents=True, exist_ok=True, mode=0o700)
        self.install('snes', 'bridge=1.5\nbridge-uses=show,files\nbridge-optional=\n'
                             f'bridge-files={POLICY}\n')
        self.install('plain', 'bridge=1.5\nbridge-uses=show\nbridge-optional=\n')
        self.install('psx', 'bridge=1.5\nbridge-uses=show,files\nbridge-optional=\n'
                            'bridge-files=cue,bin,chd|1|4|folders=cue\n')
        self.install('arcade', 'bridge=1.5\nbridge-uses=show,files\nbridge-optional=\n'
                               'bridge-files=zip,7z|1|4\n')
        for path in [self.root, *self.root.rglob('*')]:
            path.chmod(0o700 if path.is_dir() else 0o600)
        self.parent, child = socket.socketpair()
        self.parent.settimeout(5)
        self.mounts = Path(self.tmp.name) / 'mnt'
        self.mounts.mkdir()
        self.worker = subprocess.Popen([BUILD / 'managerd-test', '--control-fd', str(child.fileno()),
                                        '--root', self.root], pass_fds=[child.fileno()],
                                       stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                                       env=dict(os.environ, NBT_TEST_FILES_ROOT=str(self.files),
                                                NBT_TEST_USB_MOUNTS=str(self.mounts)))
        child.close()
        self.stream = self.parent.makefile('rb')
        self.addCleanup(self.close_worker)
        self.assertEqual(self.command('HELLO 1'), 'HELLO\t1')
        self.host = self.command('SESSION lo 0').split('\t')[1]
        self.phone = SoftwarePhone(self.tmp.name)

    def install(self, app, bridge):
        (self.root / f'trust/apps/{app}.conf').write_text('NBTAPP1\ncompat=demo-1\n')
        (self.root / f'state/{app}').mkdir(mode=0o700)
        (self.root / f'state/{app}/installed').write_text(record(bridge))

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

    def post(self, path, body):
        return self.phone.request(self.host, 'POST', path, body)

    def send(self, app, name, data, announced=None, digest=None):
        status, reply = self.post('/v1/files/put', f'{app}\t{name}\t{len(data) if announced is None else announced}'
                                  f'\t{digest or hashlib.sha256(data).hexdigest()}\n'.encode())
        if status != 200:
            return status, reply
        return self.post('/v1/files/data', data)

    def listing(self, app='snes', after=''):
        status, reply = self.post('/v1/files/list', f'{app}\t{after}\n'.encode())
        self.assertEqual(status, 200, reply)
        lines = reply.decode().splitlines()
        return lines[0].split('\t'), [line.split('\t') for line in lines[1:]]

    def stored(self):
        folder = self.files / 'files/snes'
        return sorted(p.name for p in folder.iterdir()) if folder.exists() else []

    def test_only_a_paired_phone_reaches_the_files(self):
        self.assertEqual(self.post('/v1/files/list', b'snes\t\n')[0], 403)
        self.assertEqual(self.send('snes', 'game.sfc', b'x')[0], 403)
        self.assertFalse(self.files.exists())

    def test_a_file_is_announced_then_sent_and_listed(self):
        self.phone.pair(self.command, self.host)
        header, rows = self.listing()
        self.assertEqual(header[:5], ['FILES', '0', '0', str(2 * MIB), str(MIB)])
        self.assertEqual(header[6:], ['0', 'sfc,smc'])
        game = os.urandom(300 * 1024)
        self.assertEqual(self.send('snes', 'Super Game (E).SFC', game), (200, b'OK\n'))
        self.assertEqual((self.files / 'files/snes/Super Game (E).SFC').read_bytes(), game)
        self.assertEqual(oct((self.files / 'files/snes').stat().st_mode & 0o777), '0o700')
        header, rows = self.listing()
        self.assertEqual(header[1:3], ['1', str(len(game))])
        self.assertEqual(rows, [['Super Game (E).SFC', str(len(game))]])
        # Sent again under the same name: replaced, still one file.
        self.assertEqual(self.send('snes', 'Super Game (E).SFC', b'v2'), (200, b'OK\n'))
        self.assertEqual(self.listing()[0][1:3], ['1', '2'])
        self.assertEqual(self.post('/v1/files/remove', b'snes\tSuper Game (E).SFC\n'), (200, b'OK\n'))
        self.assertEqual(self.stored(), [])
        self.assertEqual(self.post('/v1/files/remove', b'snes\tSuper Game (E).SFC\n'),
                         (404, b'ERR missing\n'))

    def test_the_policy_quota_and_names_are_enforced(self):
        self.phone.pair(self.command, self.host)
        self.assertEqual(self.send('snes', 'game.zip', b'x'), (400, b'ERR type\n'))
        self.assertEqual(self.send('snes', 'big.sfc', b'x', announced=MIB + 1), (413, b'ERR size\n'))
        self.assertEqual(self.send('snes', 'huge.sfc', b'x', announced=65 * MIB),
                         (413, b'ERR phone-size\n'))
        for name in ('../escape.sfc', 'a/b.sfc', '.hidden.sfc', 'tab\there.sfc'):
            self.assertEqual(self.send('snes', name, b'x')[0], 400, name)
        self.assertEqual(self.send('plain', 'game.sfc', b'x'), (404, b'ERR app\n'))
        self.assertEqual(self.send('manager', 'game.sfc', b'x')[0], 400)
        self.assertEqual(self.send('missing', 'game.sfc', b'x'), (404, b'ERR app\n'))
        part = b'\x55' * (900 * 1024)
        self.assertEqual(self.send('snes', 'one.sfc', part), (200, b'OK\n'))
        self.assertEqual(self.send('snes', 'two.smc', part), (200, b'OK\n'))
        self.assertEqual(self.send('snes', 'three.sfc', part), (413, b'ERR quota\n'))
        # Replacing a file counts what it replaces.
        self.assertEqual(self.send('snes', 'two.smc', part), (200, b'OK\n'))
        self.assertEqual(self.stored(), ['one.sfc', 'two.smc'])
        self.assertFalse(any((self.files / 'staging').iterdir()))

    def test_utf8_names_are_stored_and_listed(self):
        self.phone.pair(self.command, self.host)
        self.assertEqual(self.send('snes', 'Pokémon ポケモン.sfc', b'x'), (200, b'OK\n'))
        _, rows = self.listing()
        self.assertEqual([row[0] for row in rows], ['Pokémon ポケモン.sfc'])
        self.assertEqual(self.stored(), ['Pokémon ポケモン.sfc'])
        page = self.command('FILES snes first').split('\t')
        self.assertEqual(page[6:8], ['Pokémon ポケモン.sfc', '1'])
        # Latin-1, a C1 control: not UTF-8 characters a name may hold.
        for name in (b'caf\xe9.sfc', b'c1\xc2\x85.sfc'):
            status, reply = self.post('/v1/files/put', b'snes\t' + name + b'\t1\t' +
                                      hashlib.sha256(b'x').hexdigest().encode() + b'\n')
            self.assertEqual(status, 400, (name, reply))

    def test_the_bytes_must_be_the_ones_announced(self):
        self.phone.pair(self.command, self.host)
        # Data without an announcement, or of another length: refused at the header.
        self.assertEqual(self.post('/v1/files/data', b'abc')[0], 403)
        status, _ = self.post('/v1/files/put', b'snes\tgame.sfc\t3\t' + hashlib.sha256(b'abc').hexdigest().encode() + b'\n')
        self.assertEqual(status, 200)
        self.assertEqual(self.post('/v1/files/data', b'abcd')[0], 403)
        # The announced digest is the one stored: other bytes of that length are not.
        self.assertEqual(self.post('/v1/files/data', b'abd'), (400, b'ERR digest\n'))
        self.assertEqual(self.stored(), [])
        self.assertFalse(any((self.files / 'staging').iterdir()))
        # A ticket is used once.
        self.assertEqual(self.post('/v1/files/data', b'abc')[0], 403)
        # Another phone cannot use this phone's ticket.
        self.post('/v1/files/put', b'snes\tgame.sfc\t3\t' + hashlib.sha256(b'abc').hexdigest().encode() + b'\n')
        other = SoftwarePhone(self.tmp.name, 'Other phone')
        other.pair(self.command, self.host)
        self.assertEqual(other.request(self.host, 'POST', '/v1/files/data', b'abc')[0], 403)
        self.assertEqual(self.post('/v1/files/data', b'abc'), (200, b'OK\n'))

    def test_listing_pages_in_name_order(self):
        self.phone.pair(self.command, self.host)
        names = [f'game{i:02d}.sfc' for i in range(25)]
        for name in reversed(names):
            self.assertEqual(self.send('snes', name, name.encode())[0], 200)
        header, rows = self.listing()
        self.assertEqual((header[1], header[6], len(rows)), ('25', '1', 20))
        self.assertEqual([row[0] for row in rows], names[:20])
        header, rows = self.listing(after=names[19])
        self.assertEqual((header[6], [row[0] for row in rows]), ('0', names[20:]))
        # Anything else in the folder is not a stored file.
        (self.files / 'files/snes/sub.sfc').mkdir()
        (self.files / 'files/snes/link.sfc').symlink_to(self.files / 'files/snes/game00.sfc')
        self.assertEqual(self.listing()[0][1], '25')

    def test_an_unsafe_files_root_is_refused(self):
        self.phone.pair(self.command, self.host)
        self.files.mkdir(mode=0o755)
        self.assertEqual(self.post('/v1/files/list', b'snes\t\n'), (503, b'ERR unavailable\n'))
        self.files.rmdir()
        self.files.symlink_to(Path(self.tmp.name) / 'managed')
        self.assertEqual(self.send('snes', 'game.sfc', b'x'), (503, b'ERR unavailable\n'))


    # --- On the car: the GUI's FILES commands and the USB import ---

    def stick(self, files):
        for relative, data in files.items():
            path = self.mounts / relative
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data)

    def usb(self, offset=0, until=None):
        deadline = time.monotonic() + 10
        while True:
            fields = self.command(f'FILES USB snes {offset}').split('\t')
            self.assertEqual(fields[0], 'USB', fields)
            if until is None or fields[1] in until or time.monotonic() > deadline:
                return fields
            time.sleep(0.05)

    def test_a_stick_is_scanned_and_imported_on_the_car(self):
        one, two = os.urandom(100 * 1024), os.urandom(50 * 1024)
        # Three levels: the stick's top and two levels of folders.
        self.stick({'umass01200t7/ROMS/Game One.sfc': one,
                    'umass01200t7/ROMS/nested/Two.SMC': two,
                    'umass01200t7/ROMS/nested/a/too-deep.sfc': b'deep',
                    'umass01200t7/.Trashes/hidden.sfc': b'hidden',
                    'umass01200t7/ROMS/readme.txt': b'text',
                    'umass01200t7/ROMS/Jeu été.sfc': b'fr',  # UTF-8: kept
                    'other/not-a-stick.sfc': b'other'})
        # A byte that is no UTF-8 character becomes '_'.
        with open(os.fsencode(self.mounts / 'umass01200t7/ROMS') + b'/Bad\xff.sfc', 'wb') as f:
            f.write(b'bad')
        (self.mounts / 'umass01200t7/ROMS/link.sfc').symlink_to(self.mounts / 'other/not-a-stick.sfc')
        (self.mounts / 'umass01200t7/ROMS/linked').symlink_to(self.mounts / 'other')
        self.assertEqual(self.command('FILES IMPORT snes all'), 'ERR invalid-command')  # no scan yet
        self.assertEqual(self.command('FILES SCAN snes'), 'OK')
        fields = self.usb(until=('scanned', 'failed'))
        self.assertEqual(fields[1:3], ['scanned', '-'], fields)
        names = fields[7::3]
        self.assertEqual(names, ['Bad_.sfc', 'Game One.sfc', 'Jeu été.sfc', 'Two.SMC'])
        self.assertEqual(fields[6:8], ['4', 'Bad_.sfc'])
        self.assertEqual(self.command('FILES IMPORT snes all'), 'OK')
        fields = self.usb(until=('imported', 'failed'))
        self.assertEqual(fields[1:6], ['imported', '-', '4', '4', '100'], fields)
        self.assertEqual(fields[9::3], ['1', '1', '1', '1'])
        self.assertEqual((self.files / 'files/snes/Game One.sfc').read_bytes(), one)
        self.assertEqual((self.files / 'files/snes/Two.SMC').read_bytes(), two)
        self.assertFalse(any((self.files / 'staging').iterdir()))
        # The stored files, by page, and one deleted by its index.
        page = self.command('FILES snes first').split('\t')
        self.assertEqual(page[1:3], ['4', str(len(one) + len(two) + 2 + 3)])
        self.assertEqual(page[6::2], ['Bad_.sfc', 'Game One.sfc', 'Jeu été.sfc', 'Two.SMC'])
        self.assertEqual((self.files / 'files/snes/Jeu été.sfc').read_bytes(), b'fr')
        self.assertEqual(self.command('FILES DELETE snes 2'), 'OK')
        self.assertEqual(self.stored(), ['Bad_.sfc', 'Game One.sfc', 'Two.SMC'])
        self.assertEqual(self.command('FILES DELETE snes 9'), 'ERR invalid-command')
        self.assertEqual(self.command('FILES DELETE plain 0'), 'ERR invalid-command')
        self.assertEqual(self.command('FILES SCAN plain'), 'ERR app')
        self.assertEqual(self.command('FILES snes last'), 'ERR invalid-command')

    def test_the_stored_files_page_through(self):
        self.phone.pair(self.command, self.host)
        names = [f'game{i:02d}.sfc' for i in range(15)]
        for name in names:
            self.assertEqual(self.send('snes', name, b'x')[0], 200)
        first = self.command('FILES snes first').split('\t')
        self.assertEqual((first[1], first[5], first[6::2]), ('15', '1', names[:12]))
        second = self.command('FILES snes next').split('\t')
        self.assertEqual((second[5], second[6::2]), ('0', names[12:]))
        self.assertEqual(self.command('FILES DELETE snes 0'), 'OK')
        self.assertNotIn(names[12], self.stored())

    def test_an_import_stops_at_the_quota(self):
        part = b'\x11' * (900 * 1024)
        self.stick({f'umass0/rom{i}.sfc': part for i in range(3)})
        self.assertEqual(self.command('FILES SCAN snes'), 'OK')
        self.assertEqual(self.usb(until=('scanned', 'failed'))[6], '3')
        self.assertEqual(self.command('FILES IMPORT snes 2'), 'OK')  # one of them
        self.assertEqual(self.usb(until=('imported', 'failed'))[1:5], ['imported', '-', '1', '1'])
        self.assertEqual(self.stored(), ['rom2.sfc'])
        self.assertEqual(self.command('FILES IMPORT snes all'), 'OK')
        fields = self.usb(until=('imported', 'failed'))
        self.assertEqual(fields[1:5], ['failed', 'quota', '1', '2'], fields)
        self.assertEqual(self.stored(), ['rom0.sfc', 'rom2.sfc'])
        self.assertFalse(any((self.files / 'staging').iterdir()))

    def usb_of(self, app, until):
        deadline = time.monotonic() + 10
        while True:
            fields = self.command(f'FILES USB {app} 0').split('\t')
            if fields[1] in until or time.monotonic() > deadline:
                return fields
            time.sleep(0.05)

    def zip(self, relative, entries):
        path = self.mounts / relative
        path.parent.mkdir(parents=True, exist_ok=True)
        with zipfile.ZipFile(path, 'w', zipfile.ZIP_DEFLATED) as archive:
            for name, data in entries.items():
                archive.writestr(name, data)

    def test_games_of_several_files_arrive_as_folders(self):
        # dev122: a PlayStation game is a folder or an archive (cue and bins),
        # for an application declaring `|folders`; Snes9x-like ones are not.
        cue, track = b'FILE "Tekken 3 (Track 1).bin" BINARY\n', os.urandom(200 * 1024)
        crash = os.urandom(150 * 1024)
        self.stick({'umass0/PS1/Tekken 3/Tekken 3.cue': cue,
                    'umass0/PS1/Tekken 3/Tekken 3 (Track 1).bin': track,
                    'umass0/PS1/Tekken 3/readme.txt': b'text'})
        self.zip('umass0/PS1/Crash Bandicoot.zip', {'Crash/Crash.cue': b'cue', 'Crash/Crash.bin': crash,
                                                   'Crash/notes.txt': b'x'})
        self.zip('umass0/PS1/Empty.zip', {'notes.txt': b'nothing to play'})
        # Single-file games (no cue sheet) stay single files, in a folder or a zip.
        self.stick({'umass0/CHD/Ridge Racer.chd': b'r' * 10, 'umass0/CHD/Wipeout.chd': b'w' * 20})
        self.zip('umass0/Spyro.zip', {'Spyro.chd': b's' * 30})
        self.assertEqual(self.command('FILES SCAN psx'), 'OK')
        fields = self.usb_of('psx', ('scanned', 'failed'))
        self.assertEqual(fields[6:7] + fields[7::3],
                         ['5', 'Crash Bandicoot/', 'Ridge Racer.chd', 'Spyro.zip', 'Tekken 3/',
                          'Wipeout.chd'], fields)
        self.assertEqual(fields[8::3], [str(3 + len(crash)), '10', '30', str(len(cue) + len(track)), '20'])
        self.assertEqual(self.command('FILES IMPORT psx all'), 'OK')
        fields = self.usb_of('psx', ('imported', 'failed'))
        self.assertEqual(fields[1:3], ['imported', '-'], fields)
        games = self.files / 'files/psx'
        self.assertEqual(sorted(p.name for p in (games / 'Tekken 3').iterdir()),
                         ['Tekken 3 (Track 1).bin', 'Tekken 3.cue'])
        self.assertEqual((games / 'Tekken 3/Tekken 3 (Track 1).bin').read_bytes(), track)
        self.assertEqual((games / 'Crash Bandicoot/Crash.bin').read_bytes(), crash)
        self.assertEqual(sorted(p.name for p in (games / 'Crash Bandicoot').iterdir()),
                         ['Crash.bin', 'Crash.cue'])
        self.assertEqual((games / 'Spyro.chd').read_bytes(), b's' * 30)
        for single in ('Ridge Racer.chd', 'Spyro.chd', 'Wipeout.chd'):
            (games / single).unlink()
        self.assertFalse(any((self.files / 'staging').iterdir()))
        # Listed as one entry each, the files counted; deleted whole.
        page = self.command('FILES psx first').split('\t')
        self.assertEqual(page[1], '4')
        self.assertEqual(page[6::2], ['Crash Bandicoot/', 'Tekken 3/'])
        self.phone.pair(self.command, self.host)
        header, rows = self.listing('psx')
        self.assertEqual([row[0] for row in rows], ['Crash Bandicoot/', 'Tekken 3/'])
        self.assertEqual(self.listing('psx', after='Crash Bandicoot/')[1][0][0], 'Tekken 3/')
        self.assertEqual(self.post('/v1/files/remove', b'psx\tTekken 3/\n'), (200, b'OK\n'))
        self.assertFalse((games / 'Tekken 3').exists())
        self.assertEqual(self.post('/v1/files/remove', b'psx\tTekken 3/\n'), (404, b'ERR missing\n'))
        self.assertEqual(self.post('/v1/files/remove', b'psx\t../x/\n')[0], 400)
        self.assertEqual(self.command('FILES DELETE psx 0'), 'OK')
        self.assertEqual(list(games.iterdir()), [])
        # Without `|folders`, an archive's files are stored one by one.
        self.zip('umass0/SNES/Mario.zip', {'Super Mario World.sfc': b'\x01' * 1000, 'a.txt': b'x'})
        self.assertEqual(self.command('FILES SCAN snes'), 'OK')
        fields = self.usb(until=('scanned', 'failed'))
        self.assertIn('Mario.zip', fields[7::3])
        self.assertEqual(self.command('FILES IMPORT snes all'), 'OK')
        self.assertEqual(self.usb(until=('imported', 'failed'))[1], 'imported')
        self.assertEqual(self.stored(), ['Super Mario World.sfc'])

    def test_archives_an_application_declares_are_stored_as_they_are(self):
        # An emulator that reads archives itself (arcade romsets: one zip per
        # game, its ROM files of no declared type) gets each archive whole.
        self.zip('umass0/Arcade/mslug.zip', {'201-p1.p1': b'\x01' * 4000, '201-s1.s1': b'\x02' * 300})
        self.zip('umass0/neogeo.zip', {'sp-s2.sp1': b'\x03' * 500})
        self.stick({'umass0/Arcade/kof98.7z': b'7z\xbc\xaf\x27\x1c' + b'\x04' * 100})
        archives = {name: (self.mounts / 'umass0' / path).read_bytes() for name, path in
                    (('mslug.zip', 'Arcade/mslug.zip'), ('neogeo.zip', 'neogeo.zip'),
                     ('kof98.7z', 'Arcade/kof98.7z'))}
        self.assertEqual(self.command('FILES SCAN arcade'), 'OK')
        fields = self.usb_of('arcade', ('scanned', 'failed'))
        self.assertEqual(fields[6:7] + fields[7::3], ['3', 'kof98.7z', 'mslug.zip', 'neogeo.zip'], fields)
        self.assertEqual(fields[8::3], [str(len(archives[n])) for n in ('kof98.7z', 'mslug.zip', 'neogeo.zip')])
        self.assertEqual(self.command('FILES IMPORT arcade all'), 'OK')
        self.assertEqual(self.usb_of('arcade', ('imported', 'failed'))[1:3], ['imported', '-'])
        games = self.files / 'files/arcade'
        self.assertEqual(sorted(p.name for p in games.iterdir()), sorted(archives))
        for name, data in archives.items():
            self.assertEqual((games / name).read_bytes(), data)
        # From the phone too.
        self.phone.pair(self.command, self.host)
        self.assertEqual(self.send('arcade', 'sf2.zip', b'PK\5\6' + bytes(18)), (200, b'OK\n'))
        self.assertTrue((games / 'sf2.zip').is_file())

    def test_a_game_folder_counts_against_the_quota(self):
        self.stick({f'umass0/Big/disc{i}.bin': b'\x22' * (900 * 1024) for i in range(5)})
        self.stick({'umass0/Big/Big.cue': b'cue'})
        self.assertEqual(self.command('FILES SCAN psx'), 'OK')
        self.assertEqual(self.usb_of('psx', ('scanned', 'failed'))[7], 'Big/')
        self.assertEqual(self.command('FILES IMPORT psx all'), 'OK')
        fields = self.usb_of('psx', ('imported', 'failed'))
        self.assertEqual(fields[1:3], ['failed', 'quota'], fields)
        self.assertFalse((self.files / 'files/psx/Big').exists())
        self.assertFalse(any((self.files / 'staging').iterdir()))

    def test_no_stick(self):
        self.assertEqual(self.command('FILES SCAN snes'), 'OK')
        self.assertEqual(self.usb(until=('scanned', 'failed'))[1:3], ['failed', 'no-usb'])


if __name__ == '__main__':
    unittest.main()
