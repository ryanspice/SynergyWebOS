#!/usr/bin/env python3
"""Plan/apply/rollback the prototype cleanup. Run on TV as root, never implicitly."""
import argparse, hashlib, json, os, pathlib, shutil, stat, sys, tempfile

APP = pathlib.Path('/media/developer/apps/usr/palm/applications/com.canopy.orbishome')
KEYS = pathlib.Path('/home/root/.config/lginputhook/keybinds.json')
COMMAND = "luna-send -n 1 -f luna://com.webos.applicationManager/launch '{\"id\":\"com.canopy.orbishome\"}'"
TARGETS = [KEYS, APP/'index.html', APP/'src/dock.js', APP/'src/dock.css']
def digest(data): return hashlib.sha256(data).hexdigest()
def read(path): return path.read_bytes() if path.is_file() and not path.is_symlink() else None
def atomic(path, data, mode):
    fd, name = tempfile.mkstemp(prefix='.home-clean-', dir=str(path.parent))
    try:
        with os.fdopen(fd, 'wb') as f: f.write(data); f.flush(); os.fsync(f.fileno())
        os.chmod(name, mode); os.replace(name, str(path))
    finally:
        if os.path.exists(name): os.unlink(name)
def main():
    p = argparse.ArgumentParser(); p.add_argument('action', choices=['plan','apply','rollback']); p.add_argument('--receipt', required=True, help='new private absolute directory outside app'); p.add_argument('--original-index', help='explicitly reviewed original index backup'); p.add_argument('--dock-js-sha256'); p.add_argument('--dock-css-sha256'); a=p.parse_args()
    receipt=pathlib.Path(a.receipt)
    if not receipt.is_absolute() or str(receipt).startswith(str(APP)+'/'): raise ValueError('receipt must be absolute and outside app')
    if a.action == 'rollback':
        m=json.loads((receipt/'manifest.json').read_text())
        for row in m:
            path=pathlib.Path(row['path']); current=read(path)
            if path not in TARGETS or (digest(current) if current is not None else None) != row['after']:
                raise ValueError('rollback blocked by changed target: '+str(path))
            if row['before'] is not None and digest((receipt/row['file']).read_bytes()) != row['before']:
                raise ValueError('backup hash mismatch: '+str(path))
        for row in m:
            path=pathlib.Path(row['path'])
            if row['before'] is None:
                if path.exists(): path.unlink()
            else: atomic(path,(receipt/row['file']).read_bytes(),row['mode'])
        print('Rollback complete; verify hook and app.'); return
    if not a.original_index: raise ValueError('--original-index required; review before choosing backup')
    original=pathlib.Path(a.original_index)
    if original.parent != APP or not original.name.startswith('index.html.bak-'): raise ValueError('expected installed index.html.bak-*')
    old=read(original); current=read(APP/'index.html'); raw=read(KEYS)
    if old is None or current is None or raw is None: raise ValueError('missing index, backup, or bindings')
    if b'src/dock.js' in old or b'src/dock.css' in old or b'orbis-dock' in old: raise ValueError('backup still contains dock')
    # Restore only if removing exactly our known additions yields the chosen original.
    import re
    stripped=re.sub(rb'(?m)^\s*<link rel="stylesheet" href="src/dock.css">\s*\r?\n',b'',current)
    stripped=re.sub(rb'(?ms)^  <div id="orbis-dock".*?</div>\s*</div>\s*\r?\n  <div class="orbis-dock-peek"[^\n]*\n',b'',stripped)
    stripped=re.sub(rb'(?m)^\s*<script src="src/dock.js"></script>\s*\r?\n',b'',stripped)
    if stripped != old: raise ValueError('index differs beyond recognized additions; manual review required')
    data=json.loads(raw.decode('utf-8'))
    if not isinstance(data,dict): raise ValueError('unexpected keybind format')
    owned=[]
    for key in ('125','773','774'):
        entry=data.get(key)
        if isinstance(entry,dict) and entry.get('action')=='exec' and entry.get('command')==COMMAND:
            owned.append(key); del data[key]
        elif key in data: raise ValueError('foreign binding on '+key+'; review required')
    newkeys=(json.dumps(data,indent=2)+'\n').encode() if owned else raw
    for path, expected in ((TARGETS[2], a.dock_js_sha256), (TARGETS[3], a.dock_css_sha256)):
        contents=read(path)
        if contents is None or not expected or digest(contents) != expected.lower():
            raise ValueError('dock file missing or reviewed SHA-256 mismatch: '+str(path))
    changes=[newkeys, old, None, None]
    for path in TARGETS:
        if path.is_symlink(): raise ValueError('symlink target refused: '+str(path))
    print('bindings removed:', ','.join(owned) or 'none', '; index backup:', original)
    for path, value in zip(TARGETS,changes): print(path, '=>', digest(value) if value is not None else 'absent')
    if a.action == 'plan': return
    receipt.mkdir(mode=0o700, parents=False, exist_ok=False)
    rows=[]
    for i,(path,value) in enumerate(zip(TARGETS,changes)):
        before=read(path); mode=stat.S_IMODE(path.stat().st_mode) if before is not None else 0o644
        name=str(i)+'.data'
        if before is not None: (receipt/name).write_bytes(before); os.chmod(receipt/name,0o600)
        rows.append(dict(path=str(path),file=name,before=digest(before) if before is not None else None,after=digest(value) if value is not None else None,mode=mode))
    (receipt/'manifest.json').write_text(json.dumps(rows,indent=2)+'\n'); os.chmod(receipt/'manifest.json',0o600)
    # Verify target snapshots again before writes; receipt supports manual recovery on partial failure.
    for row in rows:
        now=read(pathlib.Path(row['path']))
        if (digest(now) if now is not None else None)!=row['before']: raise ValueError('target changed during planning')
    for row,value in zip(rows,changes):
        path=pathlib.Path(row['path'])
        if value is None:
            if path.exists(): path.unlink()
        else: atomic(path,value,row['mode'])
    print('Applied; receipt:', receipt)
if __name__ == '__main__':
    try: main()
    except (OSError, ValueError, json.JSONDecodeError) as e: print('REFUSED/FAILED:',e,file=sys.stderr); sys.exit(1)
