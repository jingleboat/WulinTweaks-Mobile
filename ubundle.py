# -*- coding: utf-8 -*-
"""Minimal UnityFS reader: decompress blocks info + data blocks, then scan for ASCII names.

usage: python -X utf8 ubundle.py <apk> <internal-bundle-path> [needle ...]
"""
import io
import struct
import sys
import zipfile

import lz4.block

CLASS_MASK = 0x3F


def read_cstr(f):
    out = bytearray()
    while True:
        c = f.read(1)
        if not c or c == b'\0':
            break
        out += c
    return out.decode('utf8', 'replace')


def lz4_decompress_block(buf, usize):
    # Unity LZ4 blocks are raw LZ4 block format
    return lz4.block.decompress(buf, uncompressed_size=usize)


def read_unityfs(blob):
    f = io.BytesIO(blob)
    magic = f.read(8)
    if magic[:7] != b'UnityFS':
        raise SystemExit('not UnityFS: %r' % magic)
    version, = struct.unpack('>I', f.read(4))
    unity_ver = read_cstr(f)
    unity_rev = read_cstr(f)
    size, = struct.unpack('>q', f.read(8))
    ci_size, ui_size, flags = struct.unpack('>III', f.read(12))
    if version >= 7:
        pos = f.tell()
        f.seek((pos + 15) // 16 * 16)
    comp_type = flags & CLASS_MASK
    blocks_info_at_end = bool(flags & 0x80)
    info_raw = f.read(ci_size)
    if comp_type == 1:
        import lzma
        info = lzma.decompress(info_raw)
    elif comp_type in (2, 3):
        info = lz4_decompress_block(info_raw, ui_size)
    elif comp_type == 0:
        info = info_raw
    else:
        raise SystemExit('unsupported blocksInfo compression %d' % comp_type)

    bi = io.BytesIO(info)
    bi.read(16)  # uncompressedDataHash
    nblocks, = struct.unpack('>I', bi.read(4))
    blocks = []
    for _ in range(nblocks):
        u, c, fl = struct.unpack('>IIH', bi.read(10))
        blocks.append((u, c, fl))

    data_start = f.tell()
    if flags & 0x200:
        data_start = (data_start + 15) // 16 * 16
    if blocks_info_at_end:
        data_start = len(blob) - size
    # data blocks always start right after blocksInfo when not at end
    data = bytearray()
    p = data_start
    for u, c, fl in blocks:
        chunk = blob[p:p + c]
        p += c
        ctype = fl & CLASS_MASK
        if ctype == 0:
            data += chunk
        elif ctype in (2, 3):
            data += lz4_decompress_block(chunk, u)
        elif ctype == 1:
            import lzma
            data += lzma.decompress(chunk)
        else:
            raise SystemExit('bad block compression %d' % ctype)
    return version, unity_ver, bytes(data)


def main():
    apk, path = sys.argv[1], sys.argv[2]
    needles = [n.encode() for n in sys.argv[3:]] or [b'_SwimGrid', b'_Block', b'_UnBlock', b'_SwimLadder']
    z = zipfile.ZipFile(apk)
    blob = z.read(path)
    ver, uver, data = read_unityfs(blob)
    print('UnityFS v%d  unity %s  payload %d bytes' % (ver, uver, len(data)))
    for n in needles:
        print('  %-14s count=%d' % (n.decode(), data.count(n)))


if __name__ == '__main__':
    main()
