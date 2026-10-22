# -*- coding: utf-8 -*-
import sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

SO = r"C:\Users\User\Documents\deepseek-harness\default-workspace\wulin-apk\libil2cpp_intl.so"
data = open(SO, "rb").read()
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)

TARGETS = [
    ("ConditionNode.CheckAllExceptSelf (base, real upstream walk)", 0x3D7A14C, 0x154),
    ("TalkToNpcCondition.CheckAllConnectNode",                       0x3D94450, 0x9C),
    ("TalkToNpcConditionCheckAllConnectNode (static action)",        0x268E26C, 0x1A0),
]

def dis(name, off, size):
    print("=" * 78)
    print("%s  off=0x%x size=0x%x" % (name, off, size))
    print("=" * 78)
    va = off + 0x4000
    for i in md.disasm(data[off:off + size], va):
        extra = ""
        if i.mnemonic in ("bl", "b") and i.op_str.startswith("#"):
            try:
                tgt = int(i.op_str[1:], 0)
                extra = "   ; -> RVA 0x%x" % tgt
            except ValueError:
                pass
        print("  %08x  %-8s %s%s" % (i.address, i.mnemonic, i.op_str, extra))
    print()

for n, o, s in TARGETS:
    dis(n, o, s)
