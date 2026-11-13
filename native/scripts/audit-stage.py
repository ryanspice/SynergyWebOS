#!/usr/bin/env python3
"""Gate a target stage before ares-package. No installation or external calls."""
from __future__ import annotations
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct
import stat
import subprocess

parser = argparse.ArgumentParser()
parser.add_argument('stage', type=Path)
args = parser.parse_args()
if args.stage.is_symlink():
    raise SystemExit('Symlink stage root not allowed')
root = args.stage.resolve(strict=True)
meta = json.loads((root / 'appinfo.json').read_text())
if meta.get('type') != 'native' or meta.get('main') != 'synergy-tv' or meta.get('id') != 'dev.canopy.synergywebos' or meta.get('version') != '0.4.1':
    raise SystemExit('Unexpected application identity')
forbidden = re.compile(r'^(?:ld-|ld-linux|libc[.-]|libpthread[.-]|libdl[.-]|libm[.-]|librt[.-]|libEGL|libGLES|libwayland|libgbm)')
allowed = re.compile(r'^lib(?:json-c|stdc\+\+|gcc_s|SDL2[^/]*|freetype|png[^/]*|z|brotli[^/]*|bz2|harfbuzz)\.so(?:\.[0-9]+)*$')
required = {'synergy-tv', 'synergy-daemon', 'synergy-tvctl', 'appinfo.json', 'icon.png', 'icon-large.png', 'scripts/provision-root.sh'}
# Exact notice names are a structural contract, not verified legal provenance.
notice_names = {
    'SDL2': 'licenses/SDL2-LICENSE.txt',
    'json-c': 'licenses/json-c-COPYING',
    'z': 'licenses/zlib-README',
}
base_notices = {'LICENSE', 'NOTICE.md', 'FOUNDATION-LICENSE'}
required |= base_notices
required_notices = set(base_notices)
for library in (root / 'lib').glob('*'):
    match = re.fullmatch(r'lib(SDL2)(?:-[0-9]+(?:\.[0-9]+)*)?\.so(?:\.[0-9]+)*|lib(json-c|z)\.so(?:\.[0-9]+)*', library.name)
    if library.is_file() and allowed.fullmatch(library.name):
        if not match:
            raise SystemExit(f'Runtime family requires explicit reviewed notice policy: {library.name}')
        required_notices.add(notice_names[match[1] or match[2]])
required |= required_notices
report = {'schema': 1, 'version': '0.4.1', 'target': 'ARM32 little-endian', 'tv_verified': False, 'executables': {}, 'runtime_libraries': []}
executables = {'synergy-tv', 'synergy-daemon', 'synergy-tvctl', 'scripts/provision-root.sh'}
if stat.S_IMODE(root.stat().st_mode) != 0o755:
    raise SystemExit('Stage directory mode must be 0755; use a POSIX staging filesystem')
files = []
for p in sorted(root.rglob('*')):
    if p.is_symlink():
        raise SystemExit(f'Symlink not allowed in public stage: {p}')
    if p.is_dir():
        if stat.S_IMODE(p.stat().st_mode) != 0o755:
            raise SystemExit(f'Stage directory mode must be 0755: {p.relative_to(root)}')
        continue
    if not p.is_file():
        continue
    rel = p.relative_to(root).as_posix()
    if any(c.isspace() for c in rel) or '\\' in rel:
        raise SystemExit(f'Unsafe manifest filename: {rel}')
    if rel in ('STAGE-SHA256SUMS.txt', 'STAGE-AUDIT.json'):
        continue
    expected_mode = 0o755 if rel in executables else 0o644
    if stat.S_IMODE(p.stat().st_mode) != expected_mode:
        raise SystemExit(f'Unexpected stage mode: {rel}; expected {expected_mode:04o}')
    if rel.startswith('lib/'):
        if p.parent != root / 'lib' or forbidden.match(p.name) or not allowed.fullmatch(p.name):
            raise SystemExit(f'Unapproved target runtime library: {rel}')
    elif rel not in required:
        raise SystemExit(f'Unexpected stage file: {rel}')
    data = p.read_bytes()
    if rel in required_notices and not data.strip():
        raise SystemExit(f'Empty required notice: {rel}')
    if rel in ('synergy-tv', 'synergy-daemon', 'synergy-tvctl') or rel.startswith('lib/'):
        if len(data) < 20 or data[:6] != b'\x7fELF\x01\x01' or struct.unpack_from('<H', data, 18)[0] != 40:
            raise SystemExit(f'Not an ARM32 little-endian ELF: {rel}')
        details = subprocess.check_output(['readelf', '-d', str(p)], text=True)
        versions = subprocess.check_output(['readelf', '--version-info', str(p)], text=True)
        if rel.startswith('lib/'):
            report['runtime_libraries'].append(rel)
        else:
            report['executables'][rel] = {'needed': re.findall(r'Shared library: \[(.*?)\]', details), 'symbol_versions': sorted(set(re.findall(r'(?:GLIBC|GLIBCXX|CXXABI|OPENSSL)_[0-9.]+', versions)))}
        if rel in required and not (p.stat().st_mode & 0o111):
            raise SystemExit(f'Executable mode missing: {rel}')
    files.append((rel, hashlib.sha256(data).hexdigest()))
missing = required - {name for name, _ in files}
if missing:
    raise SystemExit(f'Missing stage files: {sorted(missing)}')
report['notices'] = {name: digest for name, digest in files if name in required_notices}
# The audit records dependencies, not their availability/ABI on an unseen TV.
(root / 'STAGE-AUDIT.json').write_text(json.dumps(report, indent=2) + '\n')
files.append(('STAGE-AUDIT.json', hashlib.sha256((root / 'STAGE-AUDIT.json').read_bytes()).hexdigest()))
(root / 'STAGE-SHA256SUMS.txt').write_text(''.join(f'{digest}  {name}\n' for name, digest in sorted(files)))
print('PASS: ARM identity/files audited. Dependency availability, symbol ABI, jail access, and TV input still require target verification.')
