#!/usr/bin/env python3
"""Source-bundle regressions; not a substitute for ARM or hardware validation."""
from pathlib import Path
import json
import fnmatch
import os
import re
import unittest

ROOT = Path(__file__).resolve().parents[2]

def source_matches(pattern):
    # Ignore local evidence/release trees before traversing their dependencies.
    # Match both file and directory names, as Path.rglob did previously.
    for folder, directories, files in os.walk(ROOT):
        directories[:] = [name for name in directories if name not in ('.ai', 'dist')]
        for name in directories + files:
            if fnmatch.fnmatch(name, pattern):
                yield Path(folder) / name

class BundleTests(unittest.TestCase):
    def test_dock_package_absent(self):
        self.assertFalse((ROOT/'dock').exists())
        self.assertFalse(list(source_matches('*tvdock*')))
    def test_legacy_launch_wiring_removed(self):
        for name in ['app/js/core.js','app/js/app.js','app/index.html']:
            with self.subTest(name=name):
                self.assertNotRegex((ROOT/name).read_text(encoding='utf-8'),r'dev\.canopy\.tvdock|openDock|prepareDockCatalog|launchDock|open-dock|dockSlot')
    def test_native_launcher_removed(self):
        ui=(ROOT/'native/src/ui.cpp').read_text(encoding='utf-8')
        self.assertNotIn('page_=="apps"',ui)
        self.assertNotIn('"tab-apps"',ui)
        daemon=(ROOT/'native/src/daemon.cpp').read_text(encoding='utf-8')
        for x in ['op=="apps"','op=="launch"','op=="home"']:self.assertNotIn(x,daemon)
    def test_no_obsolete_app_page_capture(self):
        self.assertFalse((ROOT/'native/verification/ui/apps.png').exists())
        self.assertFalse((ROOT/'native/verification/ui/apps.json').exists())
    def test_identity_stays_native(self):
        meta=json.loads((ROOT/'native/appinfo.json').read_text(encoding='utf-8'))
        self.assertEqual((meta['id'],meta['version'],meta['type'],meta['main']),('dev.canopy.synergywebos','0.4.1','native','synergy-tv'))
    def test_readme_assets_and_active_doc_links(self):
        pages=[ROOT/'README.md',ROOT/'native/README.md',ROOT/'docs/README.md',ROOT/'docs/media/README.md',*list((ROOT/'native/docs').glob('*.md')),ROOT/'spikes/gear-settings/README.md']
        for page in pages:
            for dest in re.findall(r'\]\(([^)\s]+)\)',page.read_text(encoding='utf-8')):
                if dest.startswith(('http:','https:','#','mailto:')):continue
                target=(page.parent/dest.split('#')[0]).resolve()
                with self.subTest(page=page.name,target=dest):self.assertTrue(target.exists())
    def test_no_font_files_redistributed(self):
        for suffix in ['*.ttf','*.otf','*.woff','*.woff2']:
            self.assertFalse(list(source_matches(suffix)))
    def test_spike_not_in_runtime_stage(self):
        cmake=(ROOT/'native/CMakeLists.txt').read_text(encoding='utf-8')
        installs='\n'.join(re.findall(r'install\([^)]*\)',cmake,re.S))
        self.assertNotIn('spikes/',installs)
        self.assertNotIn('probe-tv.sh',installs)

if __name__=='__main__':unittest.main()
