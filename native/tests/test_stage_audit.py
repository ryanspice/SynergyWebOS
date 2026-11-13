"""Isolated structural fixtures; synthetic notice text conveys no provenance."""
import contextlib
import hashlib
import io
import json
import os
from pathlib import Path
import runpy
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[2]
AUDIT = ROOT / 'native/scripts/audit-stage.py'
FIXTURES = Path(os.environ.get('SYNERGY_TEST_POSIX_ROOT', str(ROOT / '.ai/evidence/stage-audit-fix-20261112/fixtures')))

class StageAuditTests(unittest.TestCase):
    def setUp(self):
        FIXTURES.mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=FIXTURES)
        self.addCleanup(self.temp.cleanup)
        self.stage = Path(self.temp.name)
        self.stage.chmod(0o755)
        files = {'appinfo.json': json.dumps(dict(type='native', main='synergy-tv', id='dev.canopy.synergywebos', version='0.4.1')).encode(),
                 'icon.png': b'fixture', 'icon-large.png': b'fixture', 'scripts/provision-root.sh': b'fixture'}
        elf = bytearray(20)
        elf[:6] = b'\x7fELF\x01\x01'
        struct.pack_into('<H', elf, 18, 40)
        for name in ('synergy-tv', 'synergy-daemon', 'synergy-tvctl', 'lib/libSDL2-2.0.so.0', 'lib/libjson-c.so.5', 'lib/libz.so.1'):
            files[name] = elf
        for name in ('LICENSE', 'NOTICE.md', 'FOUNDATION-LICENSE', 'licenses/SDL2-LICENSE.txt', 'licenses/json-c-COPYING', 'licenses/zlib-README'):
            files[name] = b'SYNTHETIC TEST NOTICE\n'
        for name, data in files.items():
            p = self.stage / name
            p.parent.mkdir(parents=True, exist_ok=True)
            p.parent.chmod(0o755)
            p.write_bytes(data)
            p.chmod(0o755 if name in ('synergy-tv','synergy-daemon','synergy-tvctl','scripts/provision-root.sh') else 0o644)

    def audit(self):
        with patch.object(sys, 'argv', [str(AUDIT), str(self.stage)]), patch('subprocess.check_output', return_value=''), contextlib.redirect_stdout(io.StringIO()):
            runpy.run_path(str(AUDIT), run_name='__main__')

    def test_complete_and_hashes(self):
        self.audit()
        report = json.loads((self.stage / 'STAGE-AUDIT.json').read_text())
        self.assertEqual(len(report['notices']), 6)
        manifest = (self.stage / 'STAGE-SHA256SUMS.txt').read_text()
        for name, digest in report['notices'].items():
            self.assertEqual(digest, hashlib.sha256((self.stage / name).read_bytes()).hexdigest())
            self.assertIn(f'{digest}  {name}\n', manifest)

    def test_missing_and_empty_notices(self):
        for name in ('LICENSE', 'NOTICE.md', 'FOUNDATION-LICENSE', 'licenses/SDL2-LICENSE.txt', 'licenses/json-c-COPYING', 'licenses/zlib-README'):
            p = self.stage / name
            original = p.read_bytes()
            for data in (None, b'', b' \n'):
                with self.subTest(name=name, data=data):
                    if data is None:
                        p.unlink()
                    else:
                        p.write_bytes(data)
                    with self.assertRaises(SystemExit):
                        self.audit()
                    p.write_bytes(original)

    def test_unexpected_notice(self):
        (self.stage / 'licenses/unknown.txt').write_text('fixture')
        with self.assertRaisesRegex(SystemExit, 'Unexpected stage file'):
            self.audit()

    def test_other_runtime_fails_closed(self):
        (self.stage / 'lib/libfreetype.so.6').write_bytes((self.stage / 'lib/libz.so.1').read_bytes())
        with self.assertRaisesRegex(SystemExit, 'explicit reviewed notice policy'):
            self.audit()

    def test_nested_library_rejected(self):
        nested = self.stage / 'lib/nested/libz.so.1'
        nested.parent.mkdir()
        nested.write_bytes((self.stage / 'lib/libz.so.1').read_bytes())
        with self.assertRaisesRegex(SystemExit, 'Unapproved target runtime library'):
            self.audit()

    def test_symlink(self):
        p = self.stage / 'licenses/link'
        try:
            p.symlink_to(self.stage / 'LICENSE')
        except OSError as error:
            self.skipTest(f'Symlink creation unavailable: {error}')
        with self.assertRaisesRegex(SystemExit, 'Symlink'):
            self.audit()

    def test_unsafe_modes(self):
        for name in ('synergy-tv','scripts/provision-root.sh','appinfo.json','lib/libz.so.1'):
            path=self.stage/name
            original=path.stat().st_mode & 0o7777
            for mode in (0o777,0o6755,0o666):
                with self.subTest(name=name,mode=mode):
                    path.chmod(mode)
                    with self.assertRaisesRegex(SystemExit,'Unexpected stage mode'):
                        self.audit()
                    path.chmod(original)
        (self.stage/'lib').chmod(0o777)
        with self.assertRaisesRegex(SystemExit,'directory mode'):
            self.audit()

if __name__ == '__main__':
    unittest.main()
