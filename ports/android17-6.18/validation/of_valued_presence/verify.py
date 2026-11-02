#!/usr/bin/env python3
from pathlib import Path
import hashlib
import importlib.util
import json
import re
import subprocess
import tempfile

HERE = Path(__file__).resolve().parent
RECIPE = HERE.parent.parent
WORK = Path('/home/loukious/Android/konoha-android17-6.18-port-20261031')
ATTEMPT = Path(Path('/tmp/onyx-capture-attempt.txt').read_text().strip())

def function(path, name):
    source = path.read_text()
    start = re.search(r'^(?:static inline )?bool ' + name + r'\(', source, re.M).start()
    brace = source.index('{', start)
    end, depth = brace + 1, 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[start:end]

spec = importlib.util.spec_from_file_location('dt_read', RECIPE / 'build_devicetrees.py')
dt = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dt)
observed = set()
with (ATTEMPT / 'live-618/logcat-all.txt').open(errors='replace') as stream:
    for line in stream:
        match = re.search(r"OF\s*:\s*(\S+): Read of boolean property '([^']+)' with a value", line)
        if match:
            observed.add((match[1], match[2]))
assert len(observed) == 11
fixtures = []
archive = WORK / 'artifacts/20261101T224209314131Z/devicetrees/checked'
for board in ['tuna.dtb', 'tuna7.dtb']:
    nodes = dt.read_fdt(archive / board)
    for node, prop in sorted(observed):
        data = nodes[node][prop]
        assert len(data) > 0
        fixtures.append({'board': board, 'node': node, 'property': prop, 'hex': data.hex()})

original = function(WORK / 'common/drivers/of/property.c', 'of_property_read_bool')
candidate = function(WORK / 'common/include/linux/of.h', 'of_property_present')
test = r'''
#include <assert.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdio.h>
#include <string.h>
struct property { const char *name; int length; unsigned char *data; };
struct device_node { struct property *prop; };
static int warnings;
static struct property *of_find_property(const struct device_node *np,const char *name,int *len) {
 if(!np || !np->prop || strcmp(np->prop->name,name))return NULL;
 if(len)*len=np->prop->length;
 return np->prop;
}
#define pr_warn(...) warnings++
''' + original + '\n' + candidate + r'''
static void check(unsigned char *data,int len) {
 struct property prop={"tested",len,data};struct device_node node={&prop};
 unsigned char copy[1024];assert(len<(int)sizeof(copy));memcpy(copy,data,len);
 warnings=0;assert(of_property_read_bool(&node,"tested"));assert(warnings==(len>0));
 warnings=0;assert(of_property_present(&node,"tested"));assert(!warnings);
 assert(!memcmp(copy,data,len));
 assert(!of_property_present(&node,"missing"));
 assert(!of_property_read_bool(&node,"missing"));
}
int main(void) {
 assert(!of_property_present(NULL,"tested"));assert(!of_property_read_bool(NULL,"tested"));
 unsigned char zero[4]={0};check(zero,4);check(zero,0);check(zero,1);
'''
for index, fixture in enumerate(fixtures):
    data = bytes.fromhex(fixture['hex'])
    test += '{unsigned char data[]={' + ','.join(str(n) for n in data) + '};check(data,sizeof(data));}\n'
test += 'puts("PASS: actual OF helpers preserve presence and property bytes, including zero scalar, while removing valued-boolean warnings");return 0;}\n'
with tempfile.TemporaryDirectory(prefix='onyx-of-presence-') as directory:
    directory = Path(directory)
    source = directory / 'test.c'
    binary = directory / 'test'
    source.write_text(test)
    subprocess.run(['cc', '-std=gnu11', '-Wall', '-Wextra', '-Werror', '-O1', '-g', '-fsanitize=address,undefined', str(source), '-o', str(binary)], check=True)
    result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=15)
    assert result.returncode == 0, result.stderr
report = {'status': 'PASS', 'observed_node_properties': len(observed), 'board_fixtures': len(fixtures),
          'fixtures': fixtures, 'output': result.stdout.strip(),
          'helper_sha256': {'old': hashlib.sha256(original.encode()).hexdigest(), 'new': hashlib.sha256(candidate.encode()).hexdigest()},
          'limits': 'Actual kernel OF query functions with host property lookup adapter and bytes from both final DTBs. Source call-site review and hardware warning absence are separate checks.'}
(HERE / 'validation.json').write_text(json.dumps(report, indent=2) + '\n')
print(json.dumps({k: report[k] for k in ['status', 'observed_node_properties', 'board_fixtures', 'output']}, indent=2))
