import json, sys, os
from capstone import *

BASE = r"C:\Users\User\Documents\deepseek-harness\default-workspace\wulin-apk"
SO = os.path.join(BASE, "libil2cpp.so")
DELTA = 0x4000

data = open(SO, "rb").read()
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)

_cache = {}

def syms():
    if "sym" in _cache:
        return _cache["sym"], _cache["str"]
    sym, strs = {}, {}
    with open(os.path.join(BASE, "dumpout", "script.json"), encoding="utf-8") as f:
        sc = json.load(f)
    for m in sc.get("ScriptMethod", []):
        try:
            sym[int(m["Address"])] = m.get("Name", "?")
        except Exception:
            pass
    for s in sc.get("ScriptString", []):
        try:
            strs[int(s["Address"])] = s.get("Value", "")
        except Exception:
            pass
    _cache["sym"], _cache["str"] = sym, strs
    return sym, strs


def dis(va, size):
    sym, strs = syms()
    off = va - DELTA
    print(f"=== VA {va:#x} off {off:#x} size {size:#x} ===")
    regs = {}
    for i in md.disasm(data[off:off + size], va):
        note = ""
        mn, ops = i.mnemonic, i.op_str
        if mn == "adrp":
            try:
                r, imm = ops.split(", ")
                regs[r] = int(imm.replace("#", ""), 16)
            except Exception:
                pass
        elif mn == "ldr" and "[" in ops:
            try:
                dst = ops.split(",")[0].strip()
                inner = ops[ops.index("[") + 1:ops.index("]")]
                parts = [p.strip() for p in inner.split(",")]
                base = parts[0]
                disp = 0
                if len(parts) > 1 and parts[1].startswith("#"):
                    disp = int(parts[1][1:], 0)
                if base in regs:
                    addr = regs[base] + disp
                    note = f"  ; @{addr:#x}"
                    if addr in strs:
                        note += f' = "{strs[addr]}"'
                    elif addr in sym:
                        note += f" = {sym[addr]}"
            except Exception:
                pass
        elif mn in ("bl", "b") and ops.startswith("#"):
            try:
                tgt = int(ops[1:], 16)
                if tgt in sym:
                    note = f"  ; {sym[tgt]}"
            except Exception:
                pass
        print(f"{i.address:08x}: {mn:8} {ops}{note}")


if __name__ == "__main__":
    va = int(sys.argv[1], 16)
    size = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x200
    dis(va, size)
