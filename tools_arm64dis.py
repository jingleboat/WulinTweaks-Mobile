#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""反汇编 libil2cpp（ARM64）里某个 RVA 处的函数，并尽量标注 BL/B 目标。

用法：
    python arm64dis.py <so> <rva_hex> [nbytes] [--syms <file>]

syms 文件格式：`<rva_hex> <名字>`（可用 dump.cs 里的 "RVA: 0x..." 行自建）。
没给 syms 时，只打印 BL/B 的绝对目标地址。
"""
import struct
import sys

from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM


def load_syms(path):
    syms = {}
    try:
        for line in open(path, encoding="utf-8", errors="replace"):
            p = line.split(None, 1)
            if len(p) == 2:
                try:
                    syms[int(p[0], 16)] = p[1].strip()
                except ValueError:
                    pass
    except FileNotFoundError:
        pass
    return syms


def load_so(path):
    """ELF64：解析 Program Headers，返回 (data, [ (vaddr, offset, filesz) ])"""
    data = open(path, "rb").read()
    assert data[:4] == b"\x7fELF"
    is64 = data[4] == 2
    assert is64
    phoff = struct.unpack_from("<Q", data, 0x20)[0]
    phentsize = struct.unpack_from("<H", data, 0x36)[0]
    phnum = struct.unpack_from("<H", data, 0x38)[0]
    segs = []
    for i in range(phnum):
        o = phoff + i * phentsize
        p_type = struct.unpack_from("<I", data, o)[0]
        if p_type != 1:  # PT_LOAD
            continue
        p_offset = struct.unpack_from("<Q", data, o + 0x08)[0]
        p_vaddr = struct.unpack_from("<Q", data, o + 0x10)[0]
        p_filesz = struct.unpack_from("<Q", data, o + 0x20)[0]
        segs.append((p_vaddr, p_offset, p_filesz))
    return data, segs


def rva_to_off(segs, rva):
    for vaddr, off, filesz in segs:
        if vaddr <= rva < vaddr + filesz:
            return off + (rva - vaddr)
    return None


def main():
    so = sys.argv[1]
    rva = int(sys.argv[2], 16)
    nb = int(sys.argv[3]) if len(sys.argv) > 3 and not sys.argv[3].startswith("--") else 800
    syms = {}
    if "--syms" in sys.argv:
        syms = load_syms(sys.argv[sys.argv.index("--syms") + 1])
    data, segs = load_so(so)
    off = rva_to_off(segs, rva)
    if off is None:
        print("RVA 0x%X 不在任何 PT_LOAD 段内" % rva)
        return
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    md.detail = True
    for ins in md.disasm(data[off:off + nb], rva):
        note = ""
        if ins.mnemonic in ("bl", "b") and ins.op_str.startswith("#"):
            try:
                t = int(ins.op_str[1:], 0)
            except ValueError:
                t = None
            if t is not None:
                note = "  -> %s" % syms.get(t, "0x%X" % t)
        elif ins.mnemonic in ("adrp",):
            pass
        print("%08X  %-10s %-8s %s%s" % (ins.address, ins.bytes.hex(), ins.mnemonic, ins.op_str, note))


if __name__ == "__main__":
    main()
