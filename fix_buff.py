# -*- coding: utf-8 -*-
"""把「十全大补丸」的 DynamicModifier 引用换成等价的 continousEffect

背景：DynamicModifier/Item/ShiQuanDaBuWan 在 mod 初始化时没被注册进游戏字典，
      BuffData 里引用它会让 DynamicModifierInstance.get_IsValid 抛 NRE，
      结果十全大补丸直接「不能用」。

那个修改器的实际效果（从 ModEditor 的 YAML 里读出来）是：
      战斗中把 拳掌/御剑/耍刀/长兵/短兵/乐器 六项补到 210（低于 210 时补足）
武功属性上限就是 210，所以写成 continousEffect 的 +210 等价。
"""
import json
import os
import struct
import shutil

import UnityPy

HERE = os.path.dirname(os.path.abspath(__file__))
MOD_DIR = os.path.join(HERE, 'modswap4', 'NewMyMod001')
BUNDLE_NAME = 'newmymod001_assets_all_dd3656b265f5596de8c8c2b7f3491a9c.bundle'
KEY = 'Assets/_Mods/NewMyMod001/GameDataPatch/BuffData.asset'
BUFF_UID = '10023303'

SIX = ['拳掌', '御剑', '耍刀', '长兵', '短兵', '乐器']


def split_head(raw):
    nlen = struct.unpack_from('<i', raw, 28)[0]
    p = (32 + nlen + 3) & ~3
    tlen = struct.unpack_from('<i', raw, p)[0]
    base = (p + 4 + tlen + 3) & ~3
    return raw[:base]


def parse_items(raw):
    head = split_head(raw)
    base = len(head)
    pre = struct.unpack_from('<i', raw, base)[0]
    cnt = struct.unpack_from('<i', raw, base + 4)[0]
    r = base + 8
    items = []
    for _ in range(cnt):
        klen = struct.unpack_from('<i', raw, r)[0]
        r += 4
        key = raw[r:r + klen].decode('utf-8')
        r += klen
        r = (r + 3) & ~3
        jlen = struct.unpack_from('<i', raw, r)[0]
        r += 4
        js = raw[r:r + jlen].decode('utf-8')
        r += jlen
        r = (r + 3) & ~3
        items.append((key, json.loads(js)))
    return head, pre, items, len(raw) - r


def fmt(d):
    """按 Unity JsonUtility 的缩进风格输出（与 mod 原格式一致）"""
    def arr(name, vals, comma=True):
        s = '  "%s": [' % name
        if vals:
            s += '\r\n' + ',\r\n'.join('    "%s"' % v for v in vals) + '\r\n  '
        return s + ']' + (',' if comma else '')

    parts = []
    if d.get('instantEffect'):
        parts.append(arr('instantEffect', d['instantEffect']))
    if d.get('continousEffect'):
        parts.append(arr('continousEffect', d['continousEffect']))
    if d.get('dynamicModifier'):
        parts.append(arr('dynamicModifier', d['dynamicModifier'], False))
    if parts and parts[-1].endswith(','):
        parts[-1] = parts[-1][:-1]
    return '{\r\n' + '\r\n'.join(parts) + '\r\n}'


def build_raw(head, pre, items):
    out = bytearray(head)
    out += struct.pack('<i', pre)
    out += struct.pack('<i', len(items))
    for key, d in items:
        kb = key.encode('utf-8')
        out += struct.pack('<i', len(kb)) + kb
        while len(out) % 4:
            out.append(0)
        jb = fmt(d).encode('utf-8')
        out += struct.pack('<i', len(jb)) + jb
        while len(out) % 4:
            out.append(0)
    return bytes(out)


def main():
    bp = os.path.join(MOD_DIR, BUNDLE_NAME)
    shutil.copy2(bp, bp + '.bak')
    env = UnityPy.load(bp)
    pid = next(v.path_id for k, v in env.container.items() if k == KEY)
    obj = next(o for o in env.objects if o.path_id == pid)
    raw = obj.get_raw_data()
    head, pre, items, left = parse_items(raw)
    print('BuffData: %d 条，尾部剩余 %d 字节' % (len(items), left))

    changed = False
    new_items = []
    for key, d in items:
        if key == BUFF_UID:
            print('  原 JSON:', json.dumps(d, ensure_ascii=False))
            d.pop('dynamicModifier', None)
            ce = d.get('continousEffect') or []
            for s in SIX:
                v = '%s|210' % s
                if v not in ce:
                    ce.append(v)
            d['continousEffect'] = ce
            print('  新 JSON:', json.dumps(d, ensure_ascii=False))
            changed = True
        new_items.append((key, d))

    if not changed:
        print('!! 没找到 uid', BUFF_UID)
        return

    newraw = build_raw(head, pre, new_items)
    print('raw: %d -> %d 字节' % (len(raw), len(newraw)))
    obj.set_raw_data(newraw)
    blob = env.file.save()
    tmp = bp + '.new'
    open(tmp, 'wb').write(blob)
    shutil.copy2(tmp, bp)
    try:
        os.remove(tmp)
    except OSError:
        pass
    print('bundle 写出 %d 字节' % os.path.getsize(bp))

    # 复核
    env2 = UnityPy.load(bp)
    pid2 = next(v.path_id for k, v in env2.container.items() if k == KEY)
    raw2 = next(o for o in env2.objects if o.path_id == pid2).get_raw_data()
    _, _, items2, _ = parse_items(raw2)
    for k, d in items2:
        if k == BUFF_UID:
            print('复核:', json.dumps(d, ensure_ascii=False))


if __name__ == '__main__':
    main()
