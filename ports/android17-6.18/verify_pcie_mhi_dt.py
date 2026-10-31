#!/usr/bin/env python3
"""Check native Tuna register sequences against its pinned register windows."""
import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('workspace', type=Path)
    ap.add_argument('--output', type=Path)
    a = ap.parse_args()
    repo = a.workspace / 'baseline/kernel/xiaomi/sm8735-devicetrees'
    revision = '5b5bf9e644d3be9d4517bbf43c3073bccd84992e'
    files = {}
    text = {}
    for path in ['qcom/tuna-pcie.dtsi', 'qcom/wlan/tuna-kiwi-cnss.dtsi']:
        blob = subprocess.check_output(['git', '-C', str(repo), 'rev-parse', revision + ':' + path], text=True).strip()
        data = subprocess.check_output(['git', '-C', str(repo), 'cat-file', 'blob', blob])
        assert (repo / path).read_bytes() == data
        files[path] = {'blob': blob, 'sha256': hashlib.sha256(data).hexdigest()}
        text[path] = re.sub(r'/\*.*?\*/', '', data.decode(), flags=re.S)
    pci = text['qcom/tuna-pcie.dtsi']

    def values(prop):
        match = re.search(re.escape(prop) + r'\s*=([^;]+);', pci)
        assert match, prop
        return [int(n, 0) for n in re.findall(r'0x[\da-fA-F]+|\b\d+\b', match.group(1))]

    reg = values('reg')
    names = re.findall(r'"([^"]+)"', re.search(r'reg-names\s*=([^;]+);', pci).group(1))
    assert len(reg) == 2 * len(names)
    windows = {name: {'start': reg[2*i], 'size': reg[2*i+1]} for i, name in enumerate(names)}

    def valid(name, offset, span=4):
        size = windows[name]['size']
        assert offset % 4 == 0 and offset <= size and span <= size - offset, (name, offset, span)

    phy = values('qcom,phy-sequence')
    assert len(phy) % 3 == 0
    for i in range(0, len(phy), 3):
        valid('phy', phy[i])
    seq = values('qcom,pcie-sm-seq')
    branch = values('qcom,pcie-sm-branch-seq')
    valid('pcie_sm', 0, len(seq)*4)
    valid('pcie_sm', values('qcom,pcie-sm-branch-offset')[0], len(branch)*4)
    valid('pcie_sm', values('qcom,pcie-sm-start-offset')[0])
    debug = values('qcom,pcie-sm-debug')
    for offset in debug:
        valid('pcie_sm', offset)
    valid('parf', values('qcom,pcie-clkreq-offset')[0])
    assert 'msi-map' in pci and 'qcom,pci-msm' in pci
    assert '&pcie0' in text['qcom/wlan/tuna-kiwi-cnss.dtsi']
    result = {'project': str(repo.relative_to(a.workspace)), 'revision': revision, 'files': files,
              'windows': windows, 'phy_tuples': len(phy)//3, 'state_manager_words': len(seq),
              'branch_words': len(branch), 'debug_offsets': len(debug), 'native_sequences_fit_windows': True,
              'tuna_uses_its_msi_map': True, 'wlan_enables_pcie0': True}
    out = json.dumps(result, indent=2) + '\n'
    print(out, end='')
    if a.output:
        a.output.write_text(out)


if __name__ == '__main__':
    main()
