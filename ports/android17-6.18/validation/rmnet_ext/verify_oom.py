from pathlib import Path
import argparse
import hashlib
import json
import re
import subprocess
import tempfile


def span(text, name):
    match = re.search(r'^(?:static )?[\w *]+(?:\n[\w *]+)?\b' +
                      name + r'\([^;{}]*\)\s*\{', text, re.M)
    if not match:
        raise ValueError('Missing production function: ' + name)
    end = match.end()
    depth = 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}')
        end += 1
    return match.start(), end

parser = argparse.ArgumentParser()
parser.add_argument('--source-root', type=Path)
parser.add_argument('--output', type=Path)
args = parser.parse_args()
handoff = Path(__file__).resolve().parent
source = (args.source_root or handoff / 'private-source') / 'qcom/opensource/datarmnet-ext/shs/rmnet_shs_main.c'
text = source.read_text()
names = ['rmnet_shs_oom_collect', 'rmnet_shs_oom_local', 'DATARMNET2aa4ef6ff9']
functions = []
for name in names:
    start, end = span(text, name)
    functions.append(text[start:end])
with tempfile.TemporaryDirectory(prefix='rmnet-oom-') as directory:
    root = Path(directory)
    (root / 'oom.inc').write_text('\n\n'.join(functions) + '\n')
    command = ['gcc', '-std=gnu11', '-Wall', '-Werror', '-g',
               '-fsanitize=address,undefined', '-fno-sanitize-recover=all',
               '-fno-omit-frame-pointer', '-fno-pie', '-no-pie',
               '-I', str(root), str(handoff / 'tests/rmnet_oom.c'),
               '-o', str(root / 'oom')]
    subprocess.run(command, check=True)
    result = subprocess.run([str(root / 'oom')], text=True,
                            capture_output=True, timeout=30)
    if result.returncode:
        print(result.stdout + result.stderr)
        result.check_returncode()
    report = {'status': 'PASS', 'results': json.loads(result.stdout),
              'functions': names, 'source_sha256': hashlib.sha256(source.read_bytes()).hexdigest(),
              'limits': 'Extracted production functions with modeled queue, CPU and locking services. Reclaim is confined to the current online CPU; no live CPU-hotplug or memory-pressure execution.'}
    print(json.dumps(report))
    if args.output:
        args.output.write_text(json.dumps(report, indent=2) + '\n')
