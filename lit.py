"""Sweep a function's ARM64 code and print every string literal it references.

Literal resolution: adrp+ldr gives a metadata-usage slot -> R_AARCH64_RELATIVE
relocation addend -> Il2CppDumper ScriptString entry (literal slot -> value).

usage: python lit.py <rva_hex> <insn_count>
"""
import json
import os
import re
import struct
import sys

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

HERE = os.path.dirname(os.path.abspath(__file__))
SO = os.environ.get("ARM_SO", os.path.join(HERE, "libil2cpp_intl.so"))
DUMP = os.environ.get("ARM_DUMP", "dumpout-intl")

data = open(SO, "rb").read()
e_phoff, = struct.unpack_from("<Q", data, 0x20)
e_phentsize, e_phnum = struct.unpack_from("<HH", data, 0x36)
SEGS = []
for i in range(e_phnum):
    o = e_phoff + i * e_phentsize
    p_type, = struct.unpack_from("<I", data, o)
    p_offset, p_vaddr, _pa, p_filesz, _m = struct.unpack_from("<QQQQQ", data, o + 8)
    if p_type == 1:
        SEGS.append((p_vaddr, p_offset, p_filesz))


def foff(va):
    for v, o, s in SEGS:
        if v <= va < v + s:
            return o + (va - v)
    return None


def load_index():
    cache = os.path.join(HERE, DUMP, "_lit_index.json")
    if os.path.exists(cache):
        j = json.load(open(cache, encoding="utf8"))
        return ({int(k): v for k, v in j["s"].items()},
                {int(k): v for k, v in j["r"].items()})
    raw = open(os.path.join(HERE, DUMP, "script.json"), "rb").read()
    m = raw.find(b'"ScriptString"')
    end = raw.find(b"\n  ],", m)
    seg = raw[m:end].decode("utf8", "replace")
    strings = {}
    for a, v in re.findall(r'"Address":\s*(\d+),\s*"Value":\s*"((?:[^"\\]|\\.)*)"', seg):
        try:
            strings[int(a)] = json.loads('"' + v + '"')
        except Exception:
            strings[int(a)] = v
    relocs_p = os.path.join(HERE, DUMP, "_relocs.json")
    if os.path.exists(relocs_p):
        relocs = {int(k): v for k, v in json.load(open(relocs_p)).items()}
    else:
        # build from .rela.dyn inline
        dyn = None
        for i in range(e_phnum):
            o = e_phoff + i * e_phentsize
            p_type, = struct.unpack_from("<I", data, o)
            if p_type == 2:
                dyn = struct.unpack_from("<QQQ", data, o + 8)
        v, o, s = dyn
        rela = relasz = relaent = 0
        for i in range(0, s, 16):
            tag, val = struct.unpack_from("<QQ", data, o + i)
            if tag == 0:
                break
            if tag == 7:
                rela = val
            elif tag == 8:
                relasz = val
            elif tag == 9:
                relaent = val
        ro = foff(rela)
        relocs = {}
        for i in range(0, relasz, relaent):
            r_off, r_info, r_add = struct.unpack_from("<QQq", data, ro + i)
            if (r_info & 0xFFFFFFFF) == 1027:
                relocs[r_off] = r_add
        json.dump({str(k): v for k, v in relocs.items()}, open(relocs_p, "w"))
    json.dump({"s": {str(k): v for k, v in strings.items()},
               "r": {str(k): v for k, v in relocs.items()}},
              open(cache, "w", encoding="utf8"))
    return strings, relocs


STRINGS, RELOCS = load_index()
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)


def sweep(rva, count):
    off = foff(rva)
    regs = {}
    for i in md.disasm(data[off: off + count * 4 + 32], rva):
        ops = i.op_str
        mnem = i.mnemonic
        if mnem == "adrp" and ", #" in ops:
            r, imm = ops.split(", #")
            regs[r.strip()] = int(imm, 0)  # capstone gives absolute page
        elif mnem == "add" and ops.count(",") == 2 and "#" in ops:
            r, base, imm = ops.split(",")
            base = base.strip()
            if base in regs:
                try:
                    regs[r.strip()] = regs[base] + int(imm.split("#")[1], 0)
                except ValueError:
                    pass
        elif mnem == "ldr" and "[" in ops:
            rest = ops.split(",", 1)[1]
            inner = rest[rest.find("[") + 1: rest.find("]")]
            parts = [x.strip() for x in inner.split(",")]
            base = parts[0]
            imm = 0
            if len(parts) > 1:
                try:
                    imm = int(parts[1].lstrip("#"), 0)
                except ValueError:
                    imm = 0
            if base in regs:
                slot = regs[base] + imm
                tgt = RELOCS.get(slot)
                if tgt is not None and tgt in STRINGS:
                    s = STRINGS[tgt].replace("\n", "\\n")
                    print("%08x: slot %#x -> %-40r %s" % (i.address, slot, s[:38],
                                                          ("  [len %d]" % len(STRINGS[tgt]))))
                elif slot in STRINGS:
                    print("%08x: slot %#x -> %r" % (i.address, slot, STRINGS[slot][:40]))


if __name__ == "__main__":
    sweep(int(sys.argv[1], 16), int(sys.argv[2]))
