#!/usr/bin/env python3
"""Compare native and diagnostic loader behavior with real concurrent children."""
import collections
import json
import os
from pathlib import Path
import subprocess
import tempfile

ROM = Path('/home/loukious/Android/custom-rom')
SERIAL = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031/packaging/serial-module-loaders-20261101-v2')
MOCK = '''#!/usr/bin/python3
import fcntl, json, os, sys, time
from pathlib import Path
p=Path(os.environ['MOCK_STATE'])
def event(kind):
 with (p/'lock').open('a') as lock:
  fcntl.flock(lock,fcntl.LOCK_EX)
  with (p/'calls').open('a') as out:
   out.write(json.dumps({'kind':kind,'args':sys.argv[1:]})+'\\n')
event('begin')
time.sleep(0.06)
event('end')
sys.exit(1 if not sys.argv[-1] or sys.argv[-1] in ['first_fail','bad'] else 0)
'''
cases = [
    ('no-list', {}, 'legacy'),
    ('parallel-failure', {'vendor_dlkm': {'modules.load': 'alpha\nslow\nbad\nbeta\n'}}, 'legacy'),
    ('fallback', {'vendor_dlkm': {'modules.load': 'first_fail\nunused\n'}, 'vendor': {'modules.load': 'alpha\nbeta\n'}}, 'legacy'),
    ('blocklists', {'vendor_dlkm': {'modules.load': 'alpha\nalpha\nblocked\naudio\nbeta\n', 'modules.blocklist': 'blocklist blocked\n', 'modules.audio.ar.blocklist': 'blocklist audio\n'}}, 'audioreach'),
    ('system-failure', {'system_dlkm': {'modules.load': 'alpha\nslow\nbad\nbeta\n'}}, 'legacy'),
    ('system-blocklist', {'system_dlkm': {'modules.load': 'alpha\nblock_me\nbeta\n'}, 'vendor_dlkm': {'system_dlkm.modules.blocklist': 'blocklist block-me\n'}}, 'legacy'),
]
reports = []
for case, trees, audio in cases:
    script_name = 'system_dlkm_modprobe.sh' if case.startswith('system-') else 'vendor_modprobe.sh'
    runs = []
    with tempfile.TemporaryDirectory(prefix='onyx-serial-test-') as temp:
        root = Path(temp)
        for partition, files in trees.items():
            directory = root / partition / 'lib/modules'
            directory.mkdir(parents=True, exist_ok=True)
            for name, data in files.items():
                (directory / name).write_text(data)
        for variant, source in [('native', ROM / 'vendor/xiaomi/onyx/proprietary/vendor/bin' / script_name), ('serial', SERIAL / script_name)]:
            state = root / variant; state.mkdir()
            mock = state / 'modprobe'; mock.write_text(MOCK); mock.chmod(0o755)
            text = source.read_text()
            for partition in ['vendor_dlkm', 'system_dlkm', 'vendor']:
                text = text.replace('/' + partition + '/lib/modules', str(root / partition / 'lib/modules'))
            text = text.replace('/vendor/bin/modprobe', str(mock))
            script = state / 'loader.sh'
            script.write_text('getprop() { echo ' + audio + '; }\nsetprop() { echo "$*" >> "' + str(state / 'props') + '"; }\n' + text)
            result = subprocess.run(['bash', str(script)], env=dict(os.environ, MOCK_STATE=str(state)), capture_output=True, text=True, timeout=15)
            events = [json.loads(line) for line in (state / 'calls').read_text().splitlines()] if (state / 'calls').exists() else []
            active = peak = 0
            for event in events:
                active += 1 if event['kind'] == 'begin' else -1
                peak = max(peak, active)
                assert active >= 0
            assert active == 0
            calls = [event['args'] for event in events if event['kind'] == 'begin']
            props = (state / 'props').read_text() if (state / 'props').exists() else ''
            normalized = [tuple(arg.replace(str(root), '$ROOT') for arg in args) for args in calls]
            runs.append({'variant': variant, 'returncode': result.returncode, 'calls': normalized, 'props': props, 'peak': peak})
            if variant == 'serial':
                assert peak <= 1, case
                assert result.stderr.count('Konoha serial: begin ') == len(calls)
                assert result.stderr.count('Konoha serial: end ') == len(calls)
        native, serial = runs
        assert native['returncode'] == serial['returncode'], case
        assert native['props'] == serial['props'], case
        assert collections.Counter(native['calls']) == collections.Counter(serial['calls']), case
        if case in ['parallel-failure', 'system-failure']:
            assert native['peak'] > 1 and serial['peak'] == 1
        if case == 'blocklists':
            assert [call[-1] for call in serial['calls']] == ['alpha', 'beta']
        if case == 'fallback':
            assert [call[-1] for call in serial['calls']] == ['first_fail', 'alpha', 'beta']
        if case == 'system-blocklist':
            assert [call[-1] for call in serial['calls']] == ['alpha', 'beta']
        reports.append({'case': case, 'status': 'PASS', 'runs': runs})
        print('PASS', case)
Path(__file__).with_name('loader-validation.json').write_text(json.dumps({'status': 'PASS', 'cases': reports}, indent=2) + '\n')
