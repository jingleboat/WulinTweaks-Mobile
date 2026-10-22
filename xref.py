import json, os, struct, sys, bisect

BASE = r"C:\Users\User\Documents\deepseek-harness\default-workspace\wulin-apk"
SO = os.path.join(BASE, "libil2cpp.so")

data = open(SO, "rb").read()

# ---- ELF64 program headers: executable segments ----
assert data[:4] == b"\x7fELF"
e_phoff = struct.unpack_from("<Q", data, 0x20)[0]
e_phentsize = struct.unpack_from("<H", data, 0x36)[0]
e_phnum = struct.unpack_from("<H", data, 0x38)[0]

exec_segs = []
for i in range(e_phnum):
    off = e_phoff + i * e_phentsize
    p_type = struct.unpack_from("<I", data, off)[0]
    if p_type != 1:  # PT_LOAD
        continue
    p_flags = struct.unpack_from("<I", data, off + 4)[0]
    p_offset = struct.unpack_from("<Q", data, off + 8)[0]
    p_vaddr = struct.unpack_from("<Q", data, off + 16)[0]
    p_filesz = struct.unpack_from("<Q", data, off + 32)[0]
    if p_flags & 1:  # X
        exec_segs.append((p_vaddr, p_offset, p_filesz))

_cache = {}
def symbols():
    if "s" in _cache:
        return _cache["s"], _cache["a"]
    with open(os.path.join(BASE, "dumpout", "script.json"), encoding="utf-8") as f:
        sc = json.load(f)
    sym = {}
    for m in sc.get("ScriptMethod", []):
        try:
            sym[int(m["Address"])] = m.get("Name", "?")
        except Exception:
            pass
    addrs = sorted(sym)
    _cache["s"], _cache["a"] = sym, addrs
    return sym, addrs

def owner(addr, sym, addrs):
    i = bisect.bisect_right(addrs, addr) - 1
    if i < 0:
        return None, 0
    return sym[addrs[i]], addrs[i]

def xref(target):
    sym, addrs = symbols()
    hits = []
    t64 = target & 0xFFFFFFFFFFFFFFFF
    for vaddr, off, size in exec_segs:
        end = off + size - 4
        p = off
        while p <= end:
            w = struct.unpack_from("<I", data, p)[0]
            op = w >> 26
            if op == 0x25 or op == 0x05:  # BL / B
                imm = w & 0x3FFFFFF
                if imm & 0x2000000:
                    imm -= 0x4000000
                pc = vaddr + (p - off)
                tgt = pc + imm * 4
                if tgt == t64:
                    hits.append((pc, "BL" if op == 0x25 else "B "))
            p += 4
    out = []
    for pc, kind in hits:
        name, base = owner(pc, sym, addrs)
        out.append((pc, kind, name, base))
    return out


if __name__ == "__main__":
    t = int(sys.argv[1], 16)
    print(f"xref target {t:#x}")
    res = xref(t)
    print(f"hits: {len(res)}")
    seen = set()
    for pc, kind, name, base in res:
        key = (name, base)
        if key in seen:
            continue
        seen.add(key)
        print(f"  {kind} at {pc:#x}  in {name}  (fn {base:#x})")
