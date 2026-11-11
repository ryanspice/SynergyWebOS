#!/usr/bin/env python3
"""Collect a read-only gear-menu probe over already-authorized SSH; stdlib only.

The shell payload never writes on the TV. Output is filtered locally; no serial,
account data, app titles, raw launch params or remote source code are retained.
"""
from __future__ import annotations
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import subprocess
import sys

MAX_BYTES = 262144
SECTIONS = {'identity', 'system-info', 'focused-window', 'foreground-window',
            'foreground-app', 'key-filter-config', 'files', 'complete'}
ALLOWED_KEYS = {'returnValue', 'errorCode', 'errorText', 'modelName', 'firmwareVersion',
                'sdkVersion', 'appId', 'id', 'windowType', 'displayId', 'file', 'handler',
                'configs', 'windows', 'focusedWindowInfo', 'foregroundWindowInfo',
                'foregroundAppInfo', 'focusedWindow', 'foregroundWindow', 'appInfo',
                'com.webos.surfacemanager.keyFilters',
                'com.webos.surfacemanager.keyFiltersCpp'}
MARKER = re.compile(r'^===SYNERGY-GEAR-PROBE:([a-z-]+)===$', re.M)


def clean_json(value, depth=0):
    if depth > 8:
        return '[depth-limited]'
    if isinstance(value, dict):
        return {k: ('[redacted: service denied or failed; see errorCode]' if k == 'errorText' else clean_json(v, depth + 1))
                for k, v in value.items() if k in ALLOWED_KEYS}
    if isinstance(value, list):
        return [clean_json(v, depth + 1) for v in value[:128]]
    if isinstance(value, str):
        # No raw service error text: errors can contain command/launch parameters.
        return value[:512]
    return value if value is None or isinstance(value, (bool, int, float)) else None


def parse_probe(data: bytes) -> dict:
    if len(data) > MAX_BYTES:
        raise ValueError('Probe exceeds the output limit')
    text = data.decode('utf-8', errors='strict')
    matches = list(MARKER.finditer(text))
    sections = {}
    for i, marker in enumerate(matches):
        key = marker.group(1)
        if key not in SECTIONS or key in sections:
            raise ValueError('Unexpected or duplicate probe section')
        end = matches[i + 1].start() if i + 1 < len(matches) else len(text)
        body = text[marker.end():end].strip()
        if key in {'identity', 'complete'}:
            allowed = {'uid', 'kernel', 'architecture', 'probe_schema', 'READ_ONLY',
                       'TV_MENU_INTEGRATION_VERIFIED'}
            sections[key] = {k: v[:160] for line in body.splitlines()
                             if '=' in line for k, v in [line.split('=', 1)] if k in allowed}
        elif key == 'files':
            # Retain ONLY bounded metadata rows. The probe never exports file bodies.
            rows = []
            for line in body.splitlines()[:600]:
                parts = line.split('\t')
                if parts[0] in {'DIRECTORY', 'ABSENT'} and len(parts) == 2:
                    if parts[1].startswith(('/usr/', '/media/system/')):
                        rows.append({'kind': parts[0].lower(), 'path': parts[1][:512]})
                elif parts[0] == 'FILE' and len(parts) == 4:
                    size, digest, path = parts[1:]
                    if size.strip().isdigit() and path.startswith(('/usr/', '/media/system/')):
                        rows.append({'kind': 'file', 'bytes': int(size), 'sha256': digest[:64], 'path': path[:512]})
            sections[key] = {'partial_inventory': True, 'entries': rows}
        else:
            # Extract one bounded JSON object, dropping all preamble/trailing output.
            start = body.find('{')
            try:
                result, _ = json.JSONDecoder().raw_decode(body[start:]) if start >= 0 else (None, 0)
            except json.JSONDecodeError:
                result = None
            if isinstance(result, dict):
                result = clean_json(result)
                if 'errorText' in result:
                    result['errorText'] = '[redacted: service denied or failed; see errorCode]'
                sections[key] = {'parsed': True, 'response': result}
            else:
                sections[key] = {'parsed': False, 'status': 'unavailable-denied-timeout-or-non-json'}
    if sections.get('complete', {}).get('READ_ONLY') != '1' or set(sections) != SECTIONS:
        raise ValueError('Incomplete probe; no success report will be written')
    return {'schema': 1, 'tv_menu_integration_verified': False, 'sections': sections}


def ssh_args(host: str, port: int) -> list[str]:
    # Deliberately use an existing SSH alias/user@address; never auto-enroll a host key.
    if not re.fullmatch(r'[A-Za-z0-9_][A-Za-z0-9_.@:\[\]-]{0,253}', host):
        raise ValueError('Use a plain existing SSH host alias or user@address')
    if not 1 <= port <= 65535:
        raise ValueError('Port must be 1..65535')
    return ['ssh', '-T', '-o', 'BatchMode=yes', '-o', 'StrictHostKeyChecking=yes',
            '-o', 'ConnectTimeout=5', '-p', str(port), '--', host, 'sh', '-s']


def collect(host: str, port: int, phase: str) -> dict:
    payload = Path(__file__).with_name('probe-tv.sh').read_bytes()
    result = subprocess.run(ssh_args(host, port), input=payload, capture_output=True, timeout=40)
    if result.returncode:
        # Do not log possibly sensitive server banner or authentication output.
        raise RuntimeError(f'SSH/probe exited {result.returncode}; check the existing SSH alias and authorized key. Host keys are never bypassed.')
    report = parse_probe(result.stdout)
    report.update(phase=phase, collected_at=dt.datetime.now(dt.timezone.utc).isoformat(),
                  probe_sha256=hashlib.sha256(payload).hexdigest())
    return report


def main(argv=None) -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--host', required=True, help='Existing authorized SSH alias; no password/key is stored')
    parser.add_argument('--port', type=int, default=22)
    parser.add_argument('--phase', required=True, choices=['baseline', 'gear', 'all-settings'])
    parser.add_argument('--out', type=Path, required=True, help='New local JSON file; never overwritten')
    args = parser.parse_args(argv)
    if args.out.exists():
        raise ValueError('Choose a fresh output filename; existing evidence is never overwritten')
    if not args.out.parent.is_dir():
        raise ValueError('Create the local output directory first')
    report = collect(args.host, args.port, args.phase)
    with args.out.open('x', encoding='utf-8') as handle:
        json.dump(report, handle, indent=2)
        handle.write('\n')
    print(f'Collected {args.phase} probe to {args.out}. TV settings and files were not changed. Review before sharing.')
    return 0


if __name__ == '__main__':
    try:
        raise SystemExit(main())
    except (ValueError, RuntimeError, OSError, UnicodeError, subprocess.TimeoutExpired) as exc:
        print(f'gear-settings probe: {exc}', file=sys.stderr)
        raise SystemExit(2)
