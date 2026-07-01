#!/usr/bin/env python3
import argparse
import bisect
import difflib
import hashlib
import re
import sys

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from capstone.arm64 import ARM64_OP_IMM, ARM64_OP_MEM, ARM64_OP_REG
from elftools.elf.elffile import ELFFile


CONTROL_PREFIXES = (
    "b", "bl", "blr", "br", "cbnz", "cbz", "ret", "tbz", "tbnz",
)


class KoFile:
    def __init__(self, path):
        self.path = path
        self.fh = open(path, "rb")
        self.elf = ELFFile(self.fh)
        self.symbols = self._load_symbols()
        self.addr_to_func = {
            (sym["section"], sym["value"]): name
            for name, sym in self.symbols.items()
        }
        self.section_func_addrs = {}
        self.section_func_names = {}
        for name, sym in self.symbols.items():
            self.section_func_addrs.setdefault(sym["section"], []).append(sym["value"])
            self.section_func_names[(sym["section"], sym["value"])] = name
        for addrs in self.section_func_addrs.values():
            addrs.sort()
        self.reloc_sections = {}
        self.reloc_cache = {}
        self.reloc_offset_cache = {}
        for sec in self.elf.iter_sections():
            if sec.name.startswith(".rela"):
                self.reloc_sections.setdefault(sec["sh_info"], []).append(sec)

    def close(self):
        self.fh.close()

    def _load_symbols(self):
        symbols = {}
        for sec in self.elf.iter_sections():
            if sec.header.sh_type not in ("SHT_SYMTAB", "SHT_DYNSYM"):
                continue
            for sym in sec.iter_symbols():
                name = sym.name
                if not name:
                    continue
                info_type = sym["st_info"]["type"]
                if info_type != "STT_FUNC":
                    continue
                size = sym["st_size"]
                if not size:
                    continue
                symbols[name] = {
                    "value": sym["st_value"],
                    "size": size,
                    "section": sym["st_shndx"],
                }
        return symbols

    def get_function_bytes(self, name):
        if name not in self.symbols:
            raise KeyError(f"{name}: symbol not found in {self.path}")
        sym = self.symbols[name]
        section = self.elf.get_section(sym["section"])
        offset = sym["value"] - section["sh_addr"]
        data = section.data()[offset:offset + sym["size"]]
        return sym["value"], data, sym["section"]

    def relocs_for_section(self, section_idx):
        if section_idx in self.reloc_cache:
            return self.reloc_cache[section_idx]

        relocs = {}
        for sec in self.reloc_sections.get(section_idx, []):
            symtab = self.elf.get_section(sec["sh_link"])
            for rel in sec.iter_relocations():
                sym_idx = rel["r_info_sym"]
                sym = symtab.get_symbol(sym_idx)
                symbol = sym.name
                target_section = None
                target_value = sym["st_value"] + rel["r_addend"]
                if not symbol and sym["st_info"]["type"] == "STT_SECTION":
                    target_section = sym["st_shndx"]
                    section = self.elf.get_section(target_section)
                    symbol = section.name
                    nearest = self.nearest_func(target_section, target_value)
                    if nearest:
                        symbol = nearest
                    elif section.name:
                        symbol = f"{section.name}+0x{target_value:x}"
                if not symbol:
                    symbol = f"<{sym['st_info']['type']}:{sym_idx}>"
                relocs[rel["r_offset"]] = {
                    "type": rel["r_info_type"],
                    "symbol": symbol,
                    "addend": rel["r_addend"],
                    "target_section": target_section,
                    "target_value": target_value,
                }
        self.reloc_cache[section_idx] = relocs
        return relocs

    def relocs_for_range(self, section_idx, start, end):
        relocs = self.relocs_for_section(section_idx)
        if section_idx not in self.reloc_offset_cache:
            self.reloc_offset_cache[section_idx] = sorted(relocs)
        offsets = self.reloc_offset_cache[section_idx]
        left = bisect.bisect_left(offsets, start)
        right = bisect.bisect_left(offsets, end)
        return {off: relocs[off] for off in offsets[left:right]}

    def nearest_func(self, section_idx, addr):
        addrs = self.section_func_addrs.get(section_idx, [])
        pos = bisect.bisect_right(addrs, addr) - 1
        if pos < 0:
            return None
        start = addrs[pos]
        name = self.section_func_names[(section_idx, start)]
        sym = self.symbols[name]
        if start <= addr < start + sym["size"]:
            off = addr - start
            return name if off == 0 else f"{name}+0x{off:x}"
        return None


def operand_kind(op):
    if op.type == ARM64_OP_REG:
        return "reg"
    if op.type == ARM64_OP_IMM:
        return "imm"
    if op.type == ARM64_OP_MEM:
        return "mem"
    return str(op.type)


def normalize_insn(insn, reloc=None):
    mnemonic = insn.mnemonic
    groups = []
    for op in insn.operands:
        groups.append(operand_kind(op))

    reloc_suffix = ""
    if reloc:
        reloc_suffix = f" <{reloc['symbol']}+{reloc['addend']}>"

    # Preserve branch/call/control shape but not absolute addresses or
    # compiler-specific register allocation details in the first pass.
    if mnemonic.startswith(CONTROL_PREFIXES):
        return f"{mnemonic} " + ",".join(groups) + reloc_suffix

    op_text = re.sub(r"#-?0x[0-9a-f]+|#-?[0-9]+", "#imm", insn.op_str)
    op_text = re.sub(r"0x[0-9a-f]+", "addr", op_text)
    return (f"{mnemonic} {op_text}".strip() + reloc_suffix).strip()


def disassemble(data, base):
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    md.detail = True
    return list(md.disasm(data, base))


def call_target_name(ko, section_idx, insn, reloc):
    if reloc:
        return reloc["symbol"]
    if insn.mnemonic == "bl" and insn.operands and insn.operands[0].type == ARM64_OP_IMM:
        direct = ko.addr_to_func.get((section_idx, insn.operands[0].imm))
        if direct:
            return direct
        nearest = ko.nearest_func(section_idx, insn.operands[0].imm)
        if nearest:
            return nearest
        return f"<addr:0x{insn.operands[0].imm:x}>"
    return "<indirect>"


def summarize(ko, func):
    base, data, section_idx = ko.get_function_bytes(func)
    func_relocs = ko.relocs_for_range(section_idx, base, base + len(data))
    insns = disassemble(data, base)
    normalized = [
        normalize_insn(i, func_relocs.get(i.address))
        for i in insns
    ]
    calls = [i for i in insns if i.mnemonic in ("bl", "blr")]
    call_targets = [
        call_target_name(ko, section_idx, i, func_relocs.get(i.address))
        for i in calls
    ]
    branches = [i for i in insns if i.mnemonic.startswith(("b.", "cb", "tb")) or i.mnemonic == "b"]
    return {
        "path": ko.path,
        "func": func,
        "base": base,
        "size": len(data),
        "raw_sha256": hashlib.sha256(data).hexdigest(),
        "norm_sha256": hashlib.sha256("\n".join(normalized).encode()).hexdigest(),
        "insns": insns,
        "normalized": normalized,
        "call_count": len(calls),
        "call_targets": call_targets,
        "branch_count": len(branches),
        "relocs": func_relocs,
    }


def print_compare(left, right, context):
    print(f"== {left['func']} ==")
    print(f"left:  {left['path']}")
    print(f"right: {right['path']}")
    print(f"size:  {left['size']} vs {right['size']}")
    print(f"insn:  {len(left['insns'])} vs {len(right['insns'])}")
    print(f"calls: {left['call_count']} vs {right['call_count']}")
    print(f"brs:   {left['branch_count']} vs {right['branch_count']}")
    print(f"reloc: {len(left['relocs'])} vs {len(right['relocs'])}")
    print(f"raw:   {left['raw_sha256'][:16]} vs {right['raw_sha256'][:16]}")
    print(f"norm:  {left['norm_sha256'][:16]} vs {right['norm_sha256'][:16]}")

    if left["normalized"] == right["normalized"]:
        print("normalized: identical")
    else:
        print("normalized: different")

    if left["call_targets"] == right["call_targets"]:
        print("calls-by-reloc: identical")
    else:
        print("calls-by-reloc: different")
        max_calls = max(len(left["call_targets"]), len(right["call_targets"]))
        for idx in range(max_calls):
            a = left["call_targets"][idx] if idx < len(left["call_targets"]) else "<missing>"
            b = right["call_targets"][idx] if idx < len(right["call_targets"]) else "<missing>"
            if a != b:
                print(f"first-call-diff-index: {idx}")
                start = max(0, idx - context)
                end = min(max_calls, idx + context + 1)
                for call_idx in range(start, end):
                    ca = left["call_targets"][call_idx] if call_idx < len(left["call_targets"]) else "<missing>"
                    cb = right["call_targets"][call_idx] if call_idx < len(right["call_targets"]) else "<missing>"
                    marker = "!" if ca != cb else " "
                    print(f"{marker} call {call_idx:03d} L {ca}")
                    print(f"{marker} call {call_idx:03d} R {cb}")
                break

    if left["normalized"] == right["normalized"]:
        return

    max_len = max(len(left["normalized"]), len(right["normalized"]))
    first = None
    for idx in range(max_len):
        a = left["normalized"][idx] if idx < len(left["normalized"]) else "<missing>"
        b = right["normalized"][idx] if idx < len(right["normalized"]) else "<missing>"
        if a != b:
            first = idx
            break
    if first is None:
        return

    start = max(0, first - context)
    end = min(max_len, first + context + 1)
    print(f"first-diff-index: {first}")
    for idx in range(start, end):
        a = left["normalized"][idx] if idx < len(left["normalized"]) else "<missing>"
        b = right["normalized"][idx] if idx < len(right["normalized"]) else "<missing>"
        marker = "!" if a != b else " "
        print(f"{marker} {idx:04d} L {a}")
        print(f"{marker} {idx:04d} R {b}")


def function_similarity(left_ko, right_ko, func):
    left = summarize(left_ko, func)
    right = summarize(right_ko, func)
    norm_ratio = difflib.SequenceMatcher(
        None, left["normalized"], right["normalized"], autojunk=False
    ).ratio()
    call_ratio = difflib.SequenceMatcher(
        None, left["call_targets"], right["call_targets"], autojunk=False
    ).ratio()
    size_delta = right["size"] - left["size"]
    insn_delta = len(right["insns"]) - len(left["insns"])
    call_delta = right["call_count"] - left["call_count"]
    return {
        "func": func,
        "norm_ratio": norm_ratio,
        "call_ratio": call_ratio,
        "left_size": left["size"],
        "right_size": right["size"],
        "size_delta": size_delta,
        "insn_delta": insn_delta,
        "call_delta": call_delta,
        "left_raw": left["raw_sha256"],
        "right_raw": right["raw_sha256"],
        "left_norm": left["norm_sha256"],
        "right_norm": right["norm_sha256"],
    }


def print_rank(left_ko, right_ko, limit, min_insns):
    shared = sorted(set(left_ko.symbols) & set(right_ko.symbols))
    rows = []
    for func in shared:
        try:
            left = left_ko.symbols[func]
            right = right_ko.symbols[func]
            if left["size"] < min_insns * 4 and right["size"] < min_insns * 4:
                continue
            rows.append(function_similarity(left_ko, right_ko, func))
        except Exception as exc:
            print(f"{func}: {exc}", file=sys.stderr)

    rows.sort(key=lambda r: (r["norm_ratio"], r["call_ratio"], -abs(r["size_delta"])))
    print("norm%  call%  L-size  R-size  d-size  d-insn  d-call  function")
    for row in rows[:limit]:
        print(
            f"{row['norm_ratio'] * 100:5.1f}  "
            f"{row['call_ratio'] * 100:5.1f}  "
            f"{row['left_size']:6d}  {row['right_size']:6d}  "
            f"{row['size_delta']:6d}  {row['insn_delta']:6d}  "
            f"{row['call_delta']:6d}  {row['func']}"
        )


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("left")
    parser.add_argument("right")
    parser.add_argument("functions", nargs="*")
    parser.add_argument("--context", type=int, default=8)
    parser.add_argument("--rank", action="store_true")
    parser.add_argument("--limit", type=int, default=40)
    parser.add_argument("--min-insns", type=int, default=20)
    args = parser.parse_args()

    failed = 0
    left_ko = KoFile(args.left)
    right_ko = KoFile(args.right)
    try:
        if args.rank:
            print_rank(left_ko, right_ko, args.limit, args.min_insns)
        else:
            if not args.functions:
                parser.error("functions are required unless --rank is used")
            for func in args.functions:
                try:
                    left = summarize(left_ko, func)
                    right = summarize(right_ko, func)
                    print_compare(left, right, args.context)
                    print()
                except Exception as exc:
                    failed = 1
                    print(f"{func}: {exc}", file=sys.stderr)
    finally:
        left_ko.close()
        right_ko.close()
    return failed


if __name__ == "__main__":
    raise SystemExit(main())
