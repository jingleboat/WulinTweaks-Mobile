# -*- coding: utf-8 -*-
"""xref BL/B + resolve caller -> il2cpp method name.

usage: python -X utf8 xr2.py <so> <dumpdir> <rva> [<rva> ...]
"""
import bisect, json, struct, sys
import numpy as np


def main():
    so, dump = sys.argv[1], sys.argv[2]
    targets = {int(x, 0) for x in sys.argv[3:]}
    d = open(so, 'rb').read()

    sym = {}
    with open(dump + '/script.json', encoding='utf-8') as f:
        sc = json.load(f)
    for m in sc.get('ScriptMethod', []):
        try:
            sym[int(m['Address'])] = m.get('Name', '?')
        except Exception:
            pass
    addrs = sorted(sym)

    def owner(a):
        i = bisect.bisect_right(addrs, a) - 1
        if i < 0:
            return '?'
        return sym[addrs[i]]

    e_phoff, = struct.unpack_from('<Q', d, 0x20)
    e_phes, e_phn = struct.unpack_from('<HH', d, 0x36)

    acc = {t: [] for t in targets}
    for i in range(e_phn):
        o = e_phoff + i * e_phes
        t, fl, off, va, pa, fsz, msz, al = struct.unpack_from('<IIQQQQQQ', d, o)
        if t != 1 or not (fl & 1):
            continue
        buf = d[off:off + fsz]
        n = len(buf) // 4
        words = np.frombuffer(buf[:n * 4], dtype='<u4')
        for opcode, name in ((0x94000000, 'BL'), (0x14000000, 'B')):
            hit = (words & np.uint32(0xFC000000)) == np.uint32(opcode)
            idx = np.nonzero(hit)[0]
            if len(idx) == 0:
                continue
            imm = (words[idx] & np.uint32(0x3FFFFFF)).astype(np.int64)
            imm = np.where(imm >= (1 << 25), imm - (1 << 26), imm)
            pc = va + idx.astype(np.int64) * 4
            tgt = pc + (imm << 2)
            for t in targets:
                for k in np.nonzero(tgt == t)[0]:
                    acc[t].append((name, int(pc[k])))

    for t in targets:
        print('=== target 0x%X : %d refs' % (t, len(acc[t])))
        for name, pc in sorted(acc[t], key=lambda x: x[1]):
            print('  %s 0x%08X  in %s' % (name, pc, owner(pc)))


if __name__ == '__main__':
    main()
