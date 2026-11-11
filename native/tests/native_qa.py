#!/usr/bin/env python3
"""Run under Xvfb/WSLg. Real SDL events, raw GLES framebuffer screenshots."""
from __future__ import annotations
import argparse
import json
from pathlib import Path
import subprocess

p = argparse.ArgumentParser()
p.add_argument('--binary', required=True, type=Path)
p.add_argument('--out', required=True, type=Path)
a = p.parse_args()
a.out.mkdir(parents=True, exist_ok=True)
checks = 0

def run(name, *, width=1280, height=720, view='link', events=None):
    report = a.out / f'{name}.json'
    args = [str(a.binary.resolve()), '--width', str(width), '--height', str(height), '--view', view,
            '--frames', '10', '--screenshot', str(a.out / f'{name}.png'), '--report', str(report)]
    if events:
        path = a.out / f'{name}.events.json'
        path.write_text(json.dumps(events))
        args += ['--qa-events', str(path)]
    result = subprocess.run(args, capture_output=True, text=True, timeout=12)
    if result.returncode:
        raise AssertionError(f'{name}: {result.stderr}')
    return json.loads(report.read_text())

def check(ok, label):
    global checks
    checks += 1
    if not ok:
        raise AssertionError(label)

def click(frame, x, y, w=1280):
    return dict(frame=frame, type='click', x=round(x * w / 1920), y=round(y * w / 1920))

for width, height in [(1280, 720), (1920, 1080), (3840, 2160)]:
    s = run(f'connection-{height}', width=width, height=height)
    check(s['view'] == 'link', f'{height} render')
for view in ['input', 'handoff', 'diagnostics']:
    s = run(view, view=view)
    check(s['view'] == view, f'{view} panel')
s = run('remote-navigation', events=[dict(frame=1,type='key',scan=79),dict(frame=2,type='key',scan=40)])
check(s['view'] == 'input', 'Dpad + OK switches view')
s = run('remote-back', view='input', events=[dict(frame=1,type='key',scan=41)])
check(s['view'] == 'link' and s['focus'] == 'tab-link', 'Back restores link focus')
for width, height in [(1280, 720), (3840, 2160)]:
    s = run(f'edit-{height}',width=width,height=height,events=[click(1,200,500,width),dict(frame=2,type='text',text='127.0.0.1'),click(3,1160,740,width)])
    check(s['draft']['server'] == '127.0.0.1' and not s['editing'], f'{height} pointer mapped + edit apply')
s = run('edit-cancel',events=[click(1,200,500),dict(frame=2,type='text',text='bad'),dict(frame=3,type='key',scan=41)])
check(s['draft']['server'] == '' and not s['editing'] and s['focus'] == 'server', 'cancel preserves draft + restores focus')
s = run('on-screen-keyboard',events=[click(1,200,500),dict(frame=2,type='key',scan=40)])
check(s['editing'] == 'server', 'remote keyboard stays open')
for args in [['--webos'],['--view','apps'],['--view','missing'],['--font','/missing/font.ttf','--frames','1'],['{"@system_native_app":true,"nid":"dev.canopy.synergywebos"}']]:
    result = subprocess.run([str(a.binary.resolve()),*args],capture_output=True,timeout=8)
    check(result.returncode != 0, 'invalid launch/runtime arguments rejected')
(a.out / 'SUMMARY.json').write_text(json.dumps({'assertions':checks,'renderer':'actual SDL/GLES2 framebuffer','tv_verified':False},indent=2)+'\n')
print(f'PASS: {checks} native UI checks; 720p/1080p/4K rendering, pointer mapping, Dpad, edit/apply/cancel; no TV claim')
