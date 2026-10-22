# -*- coding: utf-8 -*-
"""审计 hook_install 的目标函数：前 4 条指令里有没有「分支目标落在被覆盖的 16 字节内」。

hook.cpp 的 build_trampoline 对这种内部分支的处理是 `o[n++]=w; continue`（原样复制），
但 cbz/cbnz/b.cond/b 都是 **PC 相对**寻址 —— 搬到跳板后原样复制 = 跳错地方。
这类函数一旦被 hook，运行到那条分支就会跳到垃圾地址（v62 的 TMP.get_text 就是这么崩的）。
"""
import re, sys, struct, os

OBJ = sys.argv[1] if len(sys.argv) > 1 else r"C:\ProgramData\Temp\libil2cpp.so"
SRC = sys.argv[2] if len(sys.argv) > 2 else r"native\src"

# 1) 收集所有 hook_install(..., (void*)(g_base + RVA_XXX), ...) 用到的 RVA 宏名
rva_def = {}
for fn in os.listdir(SRC):
    if not fn.endswith((".h", ".cpp")):
        continue
    with open(os.path.join(SRC, fn), encoding="utf-8", errors="ignore") as f:
        for m in re.finditer(r"^#define\s+(RVA_\w+)\s+(0x[0-9A-Fa-f]+)", f.read(), re.M):
            rva_def[m.group(1)] = int(m.group(2), 16)

used = set()
for fn in os.listdir(SRC):
    if not fn.endswith(".cpp"):
        continue
    with open(os.path.join(SRC, fn), encoding="utf-8", errors="ignore") as f:
        txt = f.read()
    for m in re.finditer(r"hook_install\s*\(\s*\"([^\"]*)\"\s*,\s*\(void\*\)\(g_base\s*\+\s*(RVA_\w+)", txt):
        used.add((m.group(1), m.group(2)))

# 2) ELF：把虚拟地址映射到文件偏移
data = open(OBJ, "rb").read()
assert data[:4] == b"\x7fELF"
e_phoff = struct.unpack_from("<Q", data, 0x20)[0]
e_phentsize, e_phnum = struct.unpack_from("<HH", data, 0x36)
segs = []
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p_type, p_flags = struct.unpack_from("<II", data, off)
    p_offset, p_vaddr, p_paddr, p_filesz, p_memsz = struct.unpack_from("<QQQQQ", data, off + 8)
    if p_type == 1:  # PT_LOAD
        segs.append((p_vaddr, p_vaddr + p_filesz, p_offset))

def read_va(va, n):
    for lo, hi, fo in segs:
        if lo <= va and va + n <= hi:
            o = fo + (va - lo)
            return data[o:o + n]
    return None

def sxt(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v

def analyze(w0, w1, w2, w3, base):
    """返回 (verdict, note)。verdict: ok / BROKEN / FAIL_INSTALL"""
    words = [w0, w1, w2, w3]
    notes = []
    bad = False
    hard_fail = False
    for i, w in enumerate(words):
        pc = base + i * 4
        # ldr-literal / prfm  literal
        if (w & 0x3B000000) == 0x18000000:
            notes.append("insn%d=ldr-literal(装不上)" % i); hard_fail = True; continue
        if (w & 0x7E000000) == 0x36000000:
            notes.append("insn%d=tbz/tbnz(装不上)" % i); hard_fail = True; continue
        # b.cond
        if (w & 0xFF000010) == 0x54000000:
            t = pc + sxt((w >> 5) & 0x7FFFF, 19) * 4
            if base <= t < base + 16:
                notes.append("insn%d=b.cond→+%d(内部分支,已绝对化到跳板槽位)" % (i, t - base)); bad = True
            continue
        # cbz/cbnz
        if (w & 0x7E000000) == 0x34000000:
            t = pc + sxt((w >> 5) & 0x7FFFF, 19) * 4
            if base <= t < base + 16:
                notes.append("insn%d=cbz→+%d(内部分支,已绝对化到跳板槽位)" % (i, t - base)); bad = True
            continue
        # b / bl
        if (w & 0x7C000000) == 0x14000000:
            t = pc + sxt(w & 0x3FFFFFF, 26) * 4
            kind = "bl" if (w & 0x80000000) else "b"
            if base <= t < base + 16:
                notes.append("insn%d=%s→+%d(内部分支,已绝对化到跳板槽位)" % (i, kind, t - base)); bad = True
            continue
    if hard_fail:
        return "FAIL_INSTALL", "; ".join(notes)
    if bad:
        return "RELOC", "; ".join(notes)
    return "ok", "; ".join(notes)

print("%-46s %-10s %s" % ("hook 名称", "RVA", "结论"))
print("-" * 110)
for name, macro in sorted(used, key=lambda x: rva_def.get(x[1], 0)):
    rva = rva_def.get(macro)
    if rva is None:
        print("%-46s %-10s 未知宏" % (name, macro)); continue
    b = read_va(rva, 16)
    if b is None:
        print("%-46s 0x%-8X 读不到" % (name, rva)); continue
    w = struct.unpack("<4I", b)
    verdict, note = analyze(*w, rva)
    flag = {"ok": "  ", "RELOC": "✔", "FAIL_INSTALL": "⚠"}[verdict]
    print("%s %-46s 0x%-8X %s%s" % (flag, name, rva, verdict, ("  " + note) if note else ""))
