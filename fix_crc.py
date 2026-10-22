# -*- coding: utf-8 -*-
"""定位 catalog 里 AssetBundle 的 CRC 字段，并替换成新 bundle 的 CRC32。

背景：Unity 的 AssetBundle 加载会校验 CRC（crc 参数 = bundle 文件的 CRC-32），
      catalog 的 m_ExtraDataString 里存了每个 bundle 的 CRC。
      改了 bundle 内容而不改 catalog 的 CRC，就会：
      "CRC Mismatch. Provided b547bc72, calculated 177a2799 ... Will not load AssetBundle"
"""
import base64
import json
import os
import struct
import zlib

MOD_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'modswap3', 'NewMyMod001')
BUNDLE = 'newmymod001_assets_all_dd3656b265f5596de8c8c2b7f3491a9c.bundle'


def crc32_file(p):
    return zlib.crc32(open(p, 'rb').read()) & 0xFFFFFFFF


def main():
    bp = os.path.join(MOD_DIR, BUNDLE)
    new_crc = crc32_file(bp)
    print('新 bundle: %s  %d bytes  CRC32=0x%08x' % (BUNDLE, os.path.getsize(bp), new_crc))

    cp = os.path.join(MOD_DIR, 'catalog_0.1.json')
    cat = json.load(open(cp, encoding='utf-8-sig'))
    blob = base64.b64decode(cat['m_ExtraDataString'])
    print('m_ExtraDataString: bin=%d' % len(blob))
    print('  head hex:', blob[:64].hex())

    # 找旧 CRC（catalog 里记录的）
    old_crc = 0xb547bc72  # 报错里的 Provided 值
    pats = [struct.pack('<I', old_crc), struct.pack('>I', old_crc),
            bytes.fromhex('b547bc72'), bytes.fromhex('72bc47b5')]
    hits = []
    for pat in pats:
        i = blob.find(pat)
        while i >= 0:
            hits.append((i, pat.hex()))
            i = blob.find(pat, i + 1)
    print('  CRC 旧值出现位置:', hits)

    if not hits:
        print('!! 没在 ExtraData 里找到 CRC，改为全 blob 搜索 catalog 其它字段')
        for k, v in cat.items():
            if isinstance(v, str) and len(v) > 100:
                try:
                    b = base64.b64decode(v)
                except Exception:
                    continue
                for pat in pats:
                    j = b.find(pat)
                    if j >= 0:
                        print('   命中 %s @%d' % (k, j))
        return

    # 原位替换（4 字节换 4 字节，长度不变）
    nb = bytearray(blob)
    for off, _ in hits:
        nb[off:off + 4] = struct.pack('<I', new_crc)
    cat['m_ExtraDataString'] = base64.b64encode(bytes(nb)).decode('ascii')
    json.dump(cat, open(cp, 'w', encoding='utf-8'), separators=(',', ':'), ensure_ascii=False)
    print('\n已写入 catalog，新 CRC=0x%08x，ExtraData 长度不变=%s'
          % (new_crc, len(base64.b64decode(cat['m_ExtraDataString'])) == len(blob)))

    # 复核
    chk = json.load(open(cp, encoding='utf-8-sig'))
    b2 = base64.b64decode(chk['m_ExtraDataString'])
    for off, _ in hits:
        print('  校验 @%d: %s -> %s' % (off, blob[off:off + 4].hex(), b2[off:off + 4].hex()))


if __name__ == '__main__':
    main()
