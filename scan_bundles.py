# -*- coding: utf-8 -*-
"""Scan all bundles under a prefix for ASCII object names (_SwimGrid/_Block/_UnBlock/_SwimLadder)."""
import io
import re
import struct
import sys
import zipfile
from collections import Counter

import lz4.block

APK = r'E:\笔记\base_sign.apk'
CLASS_MASK = 0x3F
NEEDLES = [b'_SwimGrid', b'_SwimLadder', b'_Block', b'_UnBlock', b'_Water', b'BathWater', b'_BathWater']


def read_cstr(f):
    out = bytearray()
    while True:
        c = f.read(1)
        if not c or c == b'\0':
            break
        out += c
    return out.decode('utf8', 'replace')


def unityfs_payload(blob):
    f = io.BytesIO(blob)
    if f.read(8)[:7] != b'UnityFS':
        return None
    version, = struct.unpack('>I', f.read(4))
    read_cstr(f); read_cstr(f)
    size, = struct.unpack('>q', f.read(8))
    ci_size, ui_size, flags = struct.unpack('>III', f.read(12))
    if version >= 7:
        pos = f.tell()
        f.seek((pos + 15) // 16 * 16)
    comp = flags & CLASS_MASK
    info_raw = f.read(ci_size)
    if comp == 1:
        import lzma
        info = lzma.decompress(info_raw)
    elif comp in (2, 3):
        info = lz4.block.decompress(info_raw, uncompressed_size=ui_size)
    else:
        info = info_raw
    bi = io.BytesIO(info)
    bi.read(16)
    nblocks, = struct.unpack('>I', bi.read(4))
    blocks = [struct.unpack('>IIH', bi.read(10)) for _ in range(nblocks)]
    start = f.tell()
    if flags & 0x200:
        start = (start + 15) // 16 * 16
    if flags & 0x80:
        start = len(blob) - size
    out = bytearray()
    p = start
    for u, c, fl in blocks:
        chunk = blob[p:p + c]
        p += c
        ct = fl & CLASS_MASK
        if ct == 0:
            out += chunk
        elif ct in (2, 3):
            out += lz4.block.decompress(chunk, uncompressed_size=u)
        elif ct == 1:
            import lzma
            out += lzma.decompress(chunk)
        else:
            return None
    return bytes(out)


def main():
    prefix = sys.argv[1] if len(sys.argv) > 1 else 'assets/aa/Android/prefabs_map_assets_'
    limit = int(sys.argv[2]) if len(sys.argv) > 2 else 10 ** 9
    z = zipfile.ZipFile(APK)
    names = [n for n in z.namelist() if n.startswith(prefix) and n.endswith('.bundle')]
    names.sort()
    print('bundles matching %r: %d' % (prefix, len(names)))
    hit_swim = []
    errs = 0
    for i, n in enumerate(names[:limit]):
        try:
            pl = unityfs_payload(z.read(n))
        except Exception as e:
            errs += 1
            continue
        if pl is None:
            errs += 1
            continue
        c = {nd: pl.count(nd) for nd in NEEDLES}
        if c[b'_SwimGrid'] or c[b'_SwimLadder'] or c[b'BathWater'] or c[b'_BathWater']:
            hit_swim.append((n, c))
            print('HIT %s  %s' % (n, {k.decode(): v for k, v in c.items()}))
        if (i + 1) % 50 == 0:
            print('... %d/%d' % (i + 1, len(names)), flush=True)
    print('done. errors=%d  swim-hits=%d' % (errs, len(hit_swim)))


if __name__ == '__main__':
    main()
