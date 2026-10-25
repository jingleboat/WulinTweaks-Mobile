#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""在 ARM64 的 .so 里找某个 RVA 的直接调用点（BL），并把调用点映射到最近已知符号。

用法：
    python arm64callers.py <so> <target_rva_hex> <symtab.txt>
symtab 同 wulin-mod/tools/rva_map.txt 格式：`<rva_hex> <名字>`
"""
import bisect
import struct
import sys


def load_syms(path):
    rows = []
    for line in open(path, encoding="utf-8", errors="replace"):
        p = line.split(None, 1)
        if len(p) == 2:
            try:
                rows.append((int(p[0], 16), p[1].strip()))
            except ValueError:
                pass
    rows.sort()
    return rows


def load_so(path):
    data = open(path, "rb").read()
    phoff = struct.unpack_from("<Q", data, 0x20)[0]
    phentsize = struct.unpack_from("<H", data, 0x36)[0]
    phnum = struct.unpack_from("<H", data, 0x38)[0]
    segs = []
    for i in range(phnum):
        o = phoff + i * phentsize
        if struct.unpack_from("<I", data, o)[0] != 1:
            continue
        p_offset = struct.unpack_from("<Q", data, o + 0x08)[0]
        p_vaddr = struct.unpack_from("<Q", data, o + 0x10)[0]
        p_filesz = struct.unpack_from("<Q", data, o + 0x20)[0]
        segs.append((p_vaddr, p_offset, p_filesz))
    return data, segs


def main():
    so = sys.argv[1]
    target = int(sys.argv[2], 16)
    syms = load_syms(sys.argv[3])
    keys = [r for r, _ in syms]
    data, segs = load_so(so)
    found = []
    for vaddr, off, filesz in segs:
        blob = data[off:off + filesz]
        n = len(blob) // 4
        words = struct.unpack_from("<%dI" % n, blob, 0)
        for i, w in enumerate(words):
            if (w & 0x7C000000) != 0x14000000:      # BL(0x94000000) 与 B(0x14000000) 都是 imm26
                continue
            imm = w & 0x03FFFFFF
            if imm & 0x02000000:
                imm -= 0x04000000
            src = vaddr + i * 4
            if src + imm * 4 == target:
                found.append(src)
    print("target 0x%X  callers %d" % (target, len(found)))
    for a in sorted(found):
        j = bisect.bisect_right(keys, a) - 1
        nm, off = (syms[j][1], a - syms[j][0]) if j >= 0 else ("?", 0)
        print("  0x%08X  %s +0x%X" % (a, nm, off))


if __name__ == "__main__":
    main()
