# -*- coding: utf-8 -*-
"""按 il2cpp dump 给出的 Offset 反汇编 libil2cpp_intl.so 里的判定函数。"""
import sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

SO = r"C:\Users\User\Documents\deepseek-harness\default-workspace\wulin-apk\libil2cpp_intl.so"
IMG_BASE = 0x100000  # il2cpp 里 libil2cpp 常见镜像基址；仅用于把 bl 目标显示成 RVA

data = open(SO, "rb").read()
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)

# dump.cs 里 Offset = RVA - 0x4000，VA = RVA，所以 file_offset = Offset
TARGETS = [
    ("ConditionNode.CheckAllConnectNode (base)",            0x3D7A2A0, 0x110),
    ("IsJoinedTeamCondition.CheckAllConnectNode",           0x3D8F6EC, 0x9C),
    ("HavePartnerCondition.CheckAllConnectNode",            0x3D8E3FC, 0x9C),
    ("TalkToNpcCondition.CheckAllConnectNode",              0x3D94450, 0x9C),
]

def dis(name, off, size):
    print("=" * 78)
    print("%s   file_off=0x%x  size=0x%x" % (name, off, size))
    print("=" * 78)
    va = off + 0x4000
    for i in md.disasm(data[off:off + size], va):
        extra = ""
        if i.mnemonic in ("bl", "b") and i.op_str.startswith("#"):
            try:
                tgt = int(i.op_str[1:], 0)
                extra = "   ; -> RVA 0x%x (off 0x%x)" % (tgt, tgt - 0x4000)
            except ValueError:
                pass
        print("  %08x  %-8s %s%s" % (i.address, i.mnemonic, i.op_str, extra))
    print()

for n, o, s in TARGETS:
    dis(n, o, s)
