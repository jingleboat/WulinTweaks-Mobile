# -*- coding: utf-8 -*-
"""在 libil2cpp.so 的可执行段里找 BL/B 到指定 RVA 的调用点（找"谁在用这个函数"）。

用法：
  python -X utf8 xref_bl.py <so> <rva-hex> [<rva-hex> ...]
"""
import struct
import sys

import numpy as np


def main():
    so = sys.argv[1]
    targets = {int(x, 0) for x in sys.argv[2:]}
    d = open(so, 'rb').read()

    e_phoff = struct.unpack_from('<Q', d, 0x20)[0]
    e_phes = struct.unpack_from('<H', d, 0x36)[0]
    e_phn = struct.unpack_from('<H', d, 0x38)[0]

    for i in range(e_phn):
        o = e_phoff + i * e_phes
        t, fl, off, va, pa, fsz, msz, al = struct.unpack_from('<IIQQQQQQ', d, o)
        if t != 1 or not (fl & 1):
            continue
        buf = d[off:off + fsz]
        n = len(buf) // 4
        words = np.frombuffer(buf[:n * 4], dtype='<u4')
        # BL: 100101 imm26 / B: 000101 imm26
        for opcode, name in ((0x94000000, 'BL'), (0x14000000, 'B')):
            mask = 0xFC000000
            hit = (words & np.uint32(mask)) == np.uint32(opcode)
            idx = np.nonzero(hit)[0]
            if len(idx) == 0:
                continue
            imm = (words[idx] & np.uint32(0x3FFFFFF)).astype(np.int64)
            imm = np.where(imm >= (1 << 25), imm - (1 << 26), imm)
            pc = va + idx.astype(np.int64) * 4
            tgt = pc + (imm << 2)
            for t in sorted(targets):
                sel = np.nonzero(tgt == t)[0]
                for k in sel:
                    print('%s  caller 0x%08X -> 0x%08X' % (name, pc[k], t))


if __name__ == '__main__':
    main()
