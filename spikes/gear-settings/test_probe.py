from pathlib import Path
import json
import re
import subprocess
import tempfile
import unittest
from unittest import mock
import collect


def fixture():
    text = ''
    for key in sorted(collect.SECTIONS):
        text += f'\n===SYNERGY-GEAR-PROBE:{key}===\n'
        if key == 'identity':
            text += 'uid=0\narchitecture=armv7l\nprobe_schema=1\nserial=SECRET\n'
        elif key == 'complete':
            text += 'READ_ONLY=1\nTV_MENU_INTEGRATION_VERIFIED=0\n'
        elif key == 'files':
            text += 'FILE\t123\t' + 'a'*64 + '\t/usr/palm/applications/com.webos.app.quicksettings/appinfo.json\n'
            text += 'ABSENT\t/usr/lib/qt5/qml/KeyFilters\n'
        else:
            text += json.dumps({'returnValue': True, 'appId':'com.webos.app.quicksettings', 'token':'SECRET',
                                'params':{'password':'SECRET'}, 'title':'SECRET'})+'\n'
    return text.encode()


class ProbeTests(unittest.TestCase):
    def test_valid_report(self):
        report = collect.parse_probe(fixture())
        self.assertFalse(report['tv_menu_integration_verified'])
        self.assertEqual(report['sections']['identity']['architecture'], 'armv7l')
    def test_private_fields_removed(self):
        self.assertNotIn('SECRET', json.dumps(collect.parse_probe(fixture())))
    def test_incomplete_rejected(self):
        with self.assertRaises(ValueError): collect.parse_probe(b'===SYNERGY-GEAR-PROBE:identity===\nuid=0')
    def test_duplicate_rejected(self):
        with self.assertRaises(ValueError): collect.parse_probe(fixture()+b'\n===SYNERGY-GEAR-PROBE:identity===\nuid=0')
    def test_size_limit(self):
        with self.assertRaises(ValueError): collect.parse_probe(b'x'*(collect.MAX_BYTES+1))
    def test_destination_validation(self):
        for host in ['-oProxyCommand=bad','user@host;whoami','a\nwhoami','user@host bad','$(command)']:
            with self.assertRaises(ValueError): collect.ssh_args(host,22)
        self.assertIn('StrictHostKeyChecking=yes',collect.ssh_args('lg-root',22))
        self.assertIn('BatchMode=yes',collect.ssh_args('root@192.168.1.2',22))
    def test_denial_is_evidence_not_write_request(self):
        data=fixture().replace(b'"returnValue": true',b'"returnValue": false, "errorText":"SECRET", "errorCode": -1')
        self.assertNotIn('SECRET',json.dumps(collect.parse_probe(data)))
    def test_collect_uses_fixed_stdin_script(self):
        with mock.patch('collect.subprocess.run',return_value=subprocess.CompletedProcess([],0,fixture(),b'')) as run:
            report=collect.collect('lg-root',22,'gear')
        self.assertEqual(run.call_args.kwargs['input'],Path(__file__).with_name('probe-tv.sh').read_bytes())
        self.assertEqual(report['phase'],'gear')
    def test_no_overwrite(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'probe.json';p.write_text('keep')
            with self.assertRaises(ValueError): collect.main(['--host','lg-root','--phase','gear','--out',str(p)])
            self.assertEqual(p.read_text(),'keep')
    def test_read_only_service_allowlist(self):
        text=Path(__file__).with_name('probe-tv.sh').read_text()
        calls=re.findall(r'^read_service \S+ (luna://\S+)',text,re.M)
        self.assertEqual(len(calls),5)
        self.assertTrue(all('/get' in uri for uri in calls))
        for command in ['setSystemSettings','setConfigs','mount --','rm -','kill ','chmod ','/launch ']:
            self.assertNotIn(command,text)
    def test_shell_syntax(self):
        subprocess.run(['sh','-n',str(Path(__file__).with_name('probe-tv.sh'))],check=True)

if __name__=='__main__':unittest.main()
