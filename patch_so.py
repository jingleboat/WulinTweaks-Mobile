"""Apply an ARM64 patch to libil2cpp (or any ELF) at a given RVA.

usage: python patch_so.py <in.so> <out.so> <rva_hex> <hex_bytes...>
       python patch_so.py <in.so> <out.so> --bl <rva_hex> <target_rva_hex>
"""
import sys, struct, os

SEG = []  # PT_LOAD list filled by load()


def load(path):
    data = bytearray(open(path, "rb").read())
    assert data[:4] == b"\x7fELF"
    e_phoff, = struct.unpack_from("<Q", data, 0x20)
    e_phentsize, e_phnum = struct.unpack_from("<HH", data, 0x36)
    for i in range(e_phnum):
        off = e_phoff + i * e_phentsize
        p_type, = struct.unpack_from("<I", data, off)
        p_offset, p_vaddr, _paddr, p_filesz, _memsz = struct.unpack_from("<QQQQQ", data, off + 8)
        if p_type == 1:
            SEG.append((p_vaddr, p_offset, p_filesz))
    return data


def foff(va):
    for vaddr, offset, filesz in SEG:
        if vaddr <= va < vaddr + filesz:
            return offset + (va - vaddr)
    raise ValueError("rva %#x not mapped" % va)


def enc_b(pc, target):
    delta = target - pc
    assert delta % 4 == 0, "b target must be 4-byte aligned"
    imm = delta >> 2
    assert -(1 << 25) <= imm < (1 << 25), "b out of range"
    return 0x14000000 | (imm & 0x3FFFFFF)


def enc_bl(pc, target):
    delta = target - pc
    assert delta % 4 == 0
    imm = delta >> 2
    assert -(1 << 25) <= imm < (1 << 25)
    return 0x94000000 | (imm & 0x3FFFFFF)


if __name__ == "__main__":
    src, dst = sys.argv[1], sys.argv[2]
    data = load(src)
    args = sys.argv[3:]
    if args[0] == "--bl":
        rva = int(args[1], 16)
        target = int(args[2], 16)
        word = enc_bl(rva, target)
        kind = "bl"
    elif args[0] == "--b":
        rva = int(args[1], 16)
        target = int(args[2], 16)
        word = enc_b(rva, target)
        kind = "b"
    else:
        rva = int(args[0], 16)
        raw = bytes.fromhex("".join(args[1:]))
        word = None
        kind = "raw"
    off = foff(rva)
    if word is not None:
        old, = struct.unpack_from("<I", data, off)
        struct.pack_into("<I", data, off, word)
        print("patch %s @ rva %#x (file %#x): %08x -> %08x" % (kind, rva, off, old, word))
        assert old != word, "already patched?"
    else:
        print("patch raw @ rva %#x (file %#x): %s -> %s" % (rva, off, data[off:off + len(raw)].hex(), raw.hex()))
        data[off:off + len(raw)] = raw
    open(dst, "wb").write(data)
    print("wrote", dst, os.path.getsize(dst))
