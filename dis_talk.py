# -*- coding: utf-8 -*-
"""只挑关键指令：读 +0x60(partnerID) / +0x65(needShowBubble) / 所有 bl 调用。"""
import sys
from capstone import Cs, CS_ARCH_ARM64, CS_MODE_ARM

SO = r"C:\Users\User\Documents\deepseek-harness\default-workspace\wulin-apk\libil2cpp_intl.so"
data = open(SO, "rb").read()
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)

TARGETS = [
    ("StoryManager.InitTalkingNpcAtMap", 0x2314C1C, 0x544),
    ("StoryManager.AddTalkingNpc",       0x2315160, 0x3C0),
    ("StoryManager.RemoveTalkingNpc",    0x2315758, 0x200),
    ("StoryManager.UpdateAllNpcTalkEmojiShow", 0x2309FF0, 0x180),
]

def dis(name, off, size):
    print("=" * 78)
    print("%s  off=0x%x size=0x%x" % (name, off, size))
    print("=" * 78)
    va = off + 0x4000
    for i in md.disasm(data[off:off + size], va):
        s = "%s %s" % (i.mnemonic, i.op_str)
        keep = False
        if i.mnemonic in ("bl", "b") and i.op_str.startswith("#"):
            keep = True
        elif "#0x60" in s or "#0x65" in s or "#0x50" in s:
            keep = True
        elif i.mnemonic == "ret":
            keep = True
        if keep:
            extra = ""
            if i.mnemonic == "bl" and i.op_str.startswith("#"):
                try:
                    extra = "   ; -> RVA 0x%x" % int(i.op_str[1:], 0)
                except ValueError:
                    pass
            print("  %08x  %s%s" % (i.address, s, extra))
    print()

for n, o, s in TARGETS:
    dis(n, o, s)
