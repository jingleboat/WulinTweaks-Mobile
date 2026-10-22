# -*- coding: utf-8 -*-
"""反汇编 libil2cpp_intl.so 指定 RVA 处的函数（ARM64），带 adrp/add 目标解析"""
import sys, io, struct, re
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM
from capstone.arm64 import ARM64_OP_REG, ARM64_OP_IMM, ARM64_OP_MEM

SO = r"libil2cpp_intl.so"
data = open(SO, "rb").read()

def rva2off(rva):
    return rva - 0x4000

def dis(rva, n=120, extra=0x600000):
    off = rva2off(rva)
    code = data[off:off + n * 4 + extra]
    md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
    md.detail = True
    ins = list(md.disasm(code, rva))
    # 解析 adrp 目标
    adrp = {}
    out = []
    for i in ins[:n]:
        m = i.mnemonic
        ops = i.op_str
        note = ""
        if m == "adrp" and len(i.operands) == 2 and i.operands[1].type == ARM64_OP_IMM:
            adrp[i.operands[0].reg] = i.operands[1].imm
            note = f"    ; = {hex(i.operands[1].imm)}"
        elif m == "add" and len(i.operands) == 3 and i.operands[1].type == ARM64_OP_REG and i.operands[2].type == ARM64_OP_IMM:
            b = i.operands[1].reg
            if b in adrp:
                note = f"    ; = {hex(adrp[b] + i.operands[2].imm)}"
        elif m == "ldr" and len(i.operands) >= 2 and i.operands[1].type == ARM64_OP_MEM:
            mem = i.operands[1].mem
            base = mem.base
            if base in adrp:
                note = f"    ; = {hex(adrp[base] + mem.disp)}"
            elif mem.disp:
                note = f"    ; [{i.reg_name(base)}+{hex(mem.disp)}]"
        out.append(f"  {hex(i.address)}:  {m:<8} {ops}{note}")
    return out

if __name__ == "__main__":
    rva = int(sys.argv[1], 0)
    n = int(sys.argv[2]) if len(sys.argv) > 2 else 60
    print(f"### RVA {hex(rva)}  offset {hex(rva2off(rva))}")
    for l in dis(rva, n):
        print(l)
