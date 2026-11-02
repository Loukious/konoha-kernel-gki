#!/usr/bin/env python3
"""Prove the CRC verifier detects a corrupted real extended-only Rust import."""
import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import tarfile
import tempfile

sys.path.insert(0, str(Path(__file__).resolve().parent.parent))
from verify_modules import elf_sections, verify, versioned_imports

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('artifact', type=Path)
p.add_argument('--output', type=Path, required=True)
a = p.parse_args()
with tempfile.TemporaryDirectory(prefix='extended-version-regression-') as tmp:
    module = Path(tmp) / 'rust_binder.ko'
    with tarfile.open(a.artifact / 'gki/modules_staging_dir.tar.gz') as tar:
        matches = [m for m in tar.getmembers() if m.isfile() and m.name.endswith('/rust_binder.ko')]
        assert len(matches) == 1
        module.write_bytes(tar.extractfile(matches[0]).read())
    release = (a.artifact / 'gki/kernel.release').read_text().strip()
    symvers = [a.artifact / 'gki/kernel_aarch64_Module.symvers']
    good = verify([module], symvers, release)[0]
    sections = elf_sections(module)
    names = sections['__version_ext_names'][:-1].split(b'\0')
    index = next(i for i, name in enumerate(names) if len(name) > 55)
    symbol = names[index].decode()
    legacy = subprocess.check_output(['modprobe', '--show-modversions', str(module)], text=True)
    assert symbol not in {s.split()[1] for s in legacy.splitlines()}
    data = module.read_bytes()
    crcs = sections['__version_ext_crcs']
    assert data.count(crcs) == 1
    offset = data.index(crcs) + index * 4
    value = struct.unpack_from('<I', data, offset)[0]
    corrupted = bytearray(data)
    struct.pack_into('<I', corrupted, offset, value ^ 1)
    module.write_bytes(corrupted)
    try:
        verify([module], symvers, release)
    except ValueError as error:
        assert symbol in str(error)
        rejection = str(error)
    else:
        raise AssertionError('Corrupted extended-only CRC was accepted')
    result = {'status': 'PASS', 'original_module_sha256': hashlib.sha256(data).hexdigest(),
              'imports_checked': good['imports_checked'],
              'kmod_classic_imports': len(legacy.splitlines()),
              'extended_only_corruption': {'symbol': symbol, 'rejected': True, 'error': rejection},
              'verifier_sha256': hashlib.sha256((Path(__file__).resolve().parent.parent / 'verify_modules.py').read_bytes()).hexdigest()}
a.output.write_text(json.dumps(result, indent=2) + '\n')
print(f"PASS {good['imports_checked']} imports; extended-only CRC corruption rejected")
