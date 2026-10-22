# -*- coding: utf-8 -*-
"""修复「答题首选项正确」这个 mod 的 SolveRiddlesConfig patch：

问题：210 条 patch 里有 200 条是 {"Selections":[],"RightIndex":"0"}，
      空数组覆盖原表选项后，答题时访问 Selections[0] 就抛
      ArgumentOutOfRangeException: Index was out of range.

修法：对 Selections 为空的条目，用原表（TestMod0001/Excel/答题.xlsx）里同 uid 的选项补上，
      并按 mod 的本意「正确选项挪到第 0 位 + RightIndex=0」重排。
      原表里找不到的 uid 直接从 patch 里删掉（不 patch = 用原表值，安全）。
"""
import json
import os
import struct

import openpyxl
import UnityPy

HERE = os.path.dirname(os.path.abspath(__file__))
MOD_DIR = os.path.join(HERE, 'modswap3', 'NewMyMod001')
BUNDLE_NAME = 'newmymod001_assets_all_dd3656b265f5596de8c8c2b7f3491a9c.bundle'
KEY = 'Assets/_Mods/NewMyMod001/GameDataPatch/SolveRiddlesConfig.asset'

MOD_BASE = r'F:\SteamLibrary\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor\Assets\_Mods'
ORIG_XLSX = os.path.join(MOD_BASE, 'TestMod0001', 'Excel', '答题.xlsx')
MOD_XLSX = os.path.join(MOD_BASE, 'NewMyMod001', 'Excel', '答题首选项正确.xlsx')


def load_orig():
    ws = openpyxl.load_workbook(ORIG_XLSX, data_only=True).worksheets[0]
    out = {}
    for r in ws.iter_rows(min_row=4, values_only=True):
        if all(v is None for v in r):
            continue
        uid, name, sel, ri = r[1], r[2], r[6], r[7]
        if str(uid) == 'end' or uid is None:
            continue
        if not sel:
            continue
        opts = [x for x in str(sel).split(',')]
        try:
            idx = int(ri)
        except (TypeError, ValueError):
            continue
        out[str(uid)] = (name, opts, idx)
    return out


def parse_raw(raw):
    """头部固定到 offset 96，之后是 [前置int32][count] + 条目"""
    assert struct.unpack_from('<i', raw, 100)[0] > 0
    # 定位 typeName 结束
    nlen = struct.unpack_from('<i', raw, 28)[0]
    p = (32 + nlen + 3) & ~3
    tlen = struct.unpack_from('<i', raw, p)[0]
    base = (p + 4 + tlen + 3) & ~3
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
    return raw[:base], pre, items


def fmt_json(d):
    sel = d.get('Selections') or []
    s = '{\r\n  "Selections": ['
    if sel:
        s += '\r\n' + ',\r\n'.join('    "%s"' % x for x in sel) + '\r\n  '
    s += '],\r\n  "RightIndex": "%s"\r\n}' % d.get('RightIndex', '0')
    return s


def build_raw(head, pre, items):
    out = bytearray(head)
    out += struct.pack('<i', pre)
    out += struct.pack('<i', len(items))
    for key, d in items:
        kb = key.encode('utf-8')
        out += struct.pack('<i', len(kb)) + kb
        while len(out) % 4:
            out.append(0)
        jb = fmt_json(d).encode('utf-8')
        out += struct.pack('<i', len(jb)) + jb
        while len(out) % 4:
            out.append(0)
    out += bytes(4)
    return bytes(out)


def main():
    orig = load_orig()
    print('原表条目:', len(orig))

    bp = os.path.join(MOD_DIR, BUNDLE_NAME)
    env = UnityPy.load(bp)
    target = None
    for obj in env.objects:
        if obj.type.name != 'MonoBehaviour':
            continue
        if any(v.path_id == obj.path_id and v_k.endswith('SolveRiddlesConfig.asset')
               for v_k, v in env.container.items() for v in [v]):
            pass
    # 直接用 container 反查
    want_pid = next(v.path_id for k, v in env.container.items() if k == KEY)
    target = next(o for o in env.objects if o.path_id == want_pid)
    raw = target.get_raw_data()
    head, pre, items = parse_raw(raw)
    print('patch 条目:', len(items))

    fixed = 0
    dropped = []
    out = []
    for key, d in items:
        sel = d.get('Selections') or []
        if sel:
            out.append((key, d))
            continue
        o = orig.get(key)
        if not o:
            dropped.append(key)
            continue
        name, opts, idx = o
        idx = max(0, min(idx, len(opts) - 1))
        newsel = [opts[idx]] + [x for i, x in enumerate(opts) if i != idx]
        out.append((key, {'Selections': newsel, 'RightIndex': '0'}))
        fixed += 1

    print('补全选项: %d 条' % fixed)
    print('原表找不到、已从 patch 删除: %d 条 %s' % (len(dropped), dropped[:10]))

    newraw = build_raw(head, pre, out)
    print('raw: %d -> %d 字节' % (len(raw), len(newraw)))
    target.set_raw_data(newraw)
    blob = env.file.save()
    open(bp, 'wb').write(blob)
    print('bundle 写出 %d 字节' % len(blob))

    # 复核
    env2 = UnityPy.load(bp)
    pid2 = next(v.path_id for k, v in env2.container.items() if k == KEY)
    raw2 = next(o for o in env2.objects if o.path_id == pid2).get_raw_data()
    _, _, items2 = parse_raw(raw2)
    empty = [k for k, d in items2 if not d.get('Selections')]
    print('复核: %d 条，Selections 仍为空 %d 条' % (len(items2), len(empty)))
    for k, d in items2[:4]:
        print('   key=%-4s %r RightIndex=%r' % (k, d['Selections'], d['RightIndex']))


if __name__ == '__main__':
    main()
