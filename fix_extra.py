# -*- coding: utf-8 -*-
"""改 catalog 里 AssetBundleRequestOptions 的 m_Crc / m_BundleSize

catalog 的 m_ExtraDataString（base64）里，每个 bundle 有一段 UTF-16LE 的 JSON：
    {"m_Hash":"...","m_Crc":3041377394,"m_Timeout":0,...,"m_BundleSize":10884,
     "m_UseCrcForCachedBundles":true,...}
m_Crc=0xB547BC72 正是 Unity 报错里的 "Provided b547bc72"。
改写 bundle 之后必须同步：m_Crc=0（不校验）、m_BundleSize=新大小。
"""
import base64
import json
import re
import os
import struct

MOD_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'modswap3', 'NewMyMod001')
BUNDLE = 'newmymod001_assets_all_dd3656b265f5596de8c8c2b7f3491a9c.bundle'
MARK = b'AssetBundleRequestOptions'


def main():
    new_size = os.path.getsize(os.path.join(MOD_DIR, BUNDLE))
    cp = os.path.join(MOD_DIR, 'catalog_0.1.json')
    cat = json.load(open(cp, encoding='utf-8-sig'))
    b = base64.b64decode(cat['m_ExtraDataString'])

    i = b.find(MARK)
    j = i + len(MARK)
    ln = struct.unpack_from('<I', b, j)[0]
    js = b[j + 4:j + 4 + ln].decode('utf-16-le')
    print('原 JSON (%d 字符 / %d 字节):' % (len(js), ln))
    print(' ', js)

    s = js
    s = re.sub(r'"m_Crc":\d+', '"m_Crc":0', s)
    s = re.sub(r'"m_BundleSize":\d+', '"m_BundleSize":%d' % new_size, s)
    s = s.replace('"m_UseCrcForCachedBundles":true', '"m_UseCrcForCachedBundles":false')
    if s == js:
        print('!! 没有匹配到任何字段，检查 bundle 大小是否为 %d' % new_size)
        return
    nb = s.encode('utf-16-le')
    blob = b[:j] + struct.pack('<I', len(nb)) + nb
    print('\n新 JSON (%d 字符 / %d 字节):' % (len(s), len(nb)))
    print(' ', s)

    # JSON 必须完整（后面没有别的数据的话）
    tail = b[j + 4 + ln:]
    print('\nJSON 之后的剩余 %d 字节: %s' % (len(tail), tail.hex()))
    blob += tail

    cat['m_ExtraDataString'] = base64.b64encode(blob).decode('ascii')
    json.dump(cat, open(cp, 'w', encoding='utf-8'), separators=(',', ':'), ensure_ascii=False)
    print('\n已写回 catalog')

    # 复核
    chk = json.load(open(cp, encoding='utf-8-sig'))
    b2 = base64.b64decode(chk['m_ExtraDataString'])
    j2 = b2.find(MARK) + len(MARK)
    ln2 = struct.unpack_from('<I', b2, j2)[0]
    print('复核:', b2[j2 + 4:j2 + 4 + ln2].decode('utf-16-le')[:200])


if __name__ == '__main__':
    main()
