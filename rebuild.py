# -*- coding: utf-8 -*-
"""完整重建安卓 mod bundle + catalog（以 v14 原生包为基底，数据全部取自 PC 最新导出的 bundle）

步骤：
  1) v14 原生 bundle 做基底（Unity 2022.3.33f1，CRC/结构都是游戏认的）
  2) 两边共有的 6 个 GameDataPatch → 用 PC 的 raw 覆盖（保留安卓的 m_Script）
  3) PC 独有的 7 个 → 新建 MonoBehaviour + 登记 container + 扩展 PreloadTable
  4) catalog：InternalIds / KeyData / BucketData / EntryData 同步注册
  5) m_Crc 归零、m_BundleSize 改成新大小
"""
import base64
import json
import os
import random
import shutil
import struct
import tempfile
import zipfile

import attrs
import openpyxl
import UnityPy
from UnityPy.classes import AssetInfo, PPtr

HERE = os.path.dirname(os.path.abspath(__file__))
OUT_DIR = os.path.join(HERE, 'modswap4', 'NewMyMod001')
V14 = r'E:\笔记\wulin_v14.apk'
INNER = 'assets/Mods/Android/NewMyMod001/newmymod001_assets_all_dd3656b265f5596de8c8c2b7f3491a9c.bundle'
PC_DIR = r'F:\SteamLibrary\steamapps\common\WulinSH\Mods\NewMyMod001'
PC_BUNDLE = os.path.join(PC_DIR, 'newmymod001_assets_all_3b046b5d4844aa5f95f007af7cdbb6ee.bundle')
PC_CATALOG = os.path.join(PC_DIR, 'catalog_0.1.0.json')
MOD_BASE = r'F:\SteamLibrary\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor\Assets\_Mods'
ORIG_XLSX = os.path.join(MOD_BASE, 'TestMod0001', 'Excel', '答题.xlsx')

SHARED = ['BuffData.asset', 'FactionGoodsData.asset', 'FactionJobData.asset',
          'GameEnvConstData.asset', 'GameEnvConstStrData.asset', 'MapBuildingKitData.asset']

# (PC 文件名, 子目录, catalog 相对名, 新 m_Name, resType 索引)
# 注意：PC 版导出的 SelfPropBufflData.asset 是 20 条全空的
# {"propRequire": [], "getBuff": ""}，等于把游戏原有的「自身道具触发 buff」配置清空。
# 它没有任何实际修改，手机版不需要，已从列表移除（2026-10-20）。
NEW_ASSETS = [
    ('BountyHunterBaseTask.asset', 'GameDataPatch', 'BountyHunterBaseTask', 'BountyHunterBaseTask', 2),
    ('FactionData.asset', 'GameDataPatch', 'FactionData', 'FactionData', 2),
    ('GameEnvConstData 1.asset', 'GameDataPatch', 'GameEnvConstData2', 'GameEnvConstData2', 2),
    ('SolveRiddlesConfig.asset', 'GameDataPatch', 'SolveRiddlesConfig', 'SolveRiddlesConfig', 2),
    ('TeacheData.asset', 'GameDataPatch', 'TeacheData', 'TeacheData', 2),
]


# ------------------------------------------------------------------ raw 工具
def split_raw(raw):
    nlen = struct.unpack_from('<i', raw, 28)[0]
    name = raw[32:32 + nlen]
    p = (32 + nlen + 3) & ~3
    tlen = struct.unpack_from('<i', raw, p)[0]
    tname = raw[p + 4:p + 4 + tlen]
    tail = raw[p + 4 + tlen:]
    f_go, p_go = struct.unpack_from('<iq', raw, 0)
    enabled = struct.unpack_from('<i', raw, 12)[0]
    f_s, p_s = struct.unpack_from('<iq', raw, 16)
    return dict(go=(f_go, p_go), enabled=enabled, script=(f_s, p_s), name=name, tname=tname, tail=tail)


def build_raw(h):
    out = bytearray()
    out += struct.pack('<iq', *h['go'])
    out += struct.pack('<i', h['enabled'])
    out += struct.pack('<iq', *h['script'])
    out += struct.pack('<i', len(h['name'])) + h['name']
    while len(out) % 4:
        out.append(0)
    out += struct.pack('<i', len(h['tname'])) + h['tname']
    out += h['tail']
    return bytes(out)


def script_of(raw):
    return struct.unpack_from('<iq', raw, 16)


# --- BuffData 的 JSON patch 里，把「十全大补丸」的 DynamicModifier 换成 continousEffect ---
BUFF_UID = '10023303'
SIX = ['拳掌', '御剑', '耍刀', '长兵', '短兵', '乐器']


def _bd_split_head(raw):
    nlen = struct.unpack_from('<i', raw, 28)[0]
    p = (32 + nlen + 3) & ~3
    tlen = struct.unpack_from('<i', raw, p)[0]
    return raw[:((p + 4 + tlen + 3) & ~3)]


def _bd_parse(raw):
    head = _bd_split_head(raw)
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
    return head, pre, items


def _bd_fmt(d):
    parts = []

    def arr(name, vals, comma=True):
        s = '  "%s": [' % name
        if vals:
            s += '\r\n' + ',\r\n'.join('    "%s"' % v for v in vals) + '\r\n  '
        return s + ']' + (',' if comma else '')

    if d.get('instantEffect'):
        parts.append(arr('instantEffect', d['instantEffect']))
    if d.get('continousEffect'):
        parts.append(arr('continousEffect', d['continousEffect']))
    if d.get('dynamicModifier'):
        parts.append(arr('dynamicModifier', d['dynamicModifier'], False))
    if parts and parts[-1].endswith(','):
        parts[-1] = parts[-1][:-1]
    return '{\r\n' + '\r\n'.join(parts) + '\r\n}'


def _bd_build(head, pre, items):
    out = bytearray(head)
    out += struct.pack('<i', pre)
    out += struct.pack('<i', len(items))
    for key, d in items:
        kb = key.encode('utf-8')
        out += struct.pack('<i', len(kb)) + kb
        while len(out) % 4:
            out.append(0)
        jb = _bd_fmt(d).encode('utf-8')
        out += struct.pack('<i', len(jb)) + jb
        while len(out) % 4:
            out.append(0)
    out += bytes(4)     # 原 asset 在 JSON 之后固定还有 4 字节 0，缺了会被 Unity 判成 corrupted
    return bytes(out)


def fix_buff_raw(raw):
    """把十全大补丸改回『引用 DynamicModifier』——但引用的是 v14 原有、构建期就在包里的
    DynamicModifier/Item/TeBieXunZhang.asset（我们已把 ShiQuanDaBuWan 的数据写进那个槽位）。"""
    head, pre, items = _bd_parse(raw)
    for i, (k, d) in enumerate(items):
        if k != BUFF_UID:
            continue
        d.pop('continousEffect', None)
        d['dynamicModifier'] = ['Item/TeBieXunZhang']
        items[i] = (k, d)
    return _bd_build(head, pre, items)


# --------------------------------------------------------------- catalog blob
class W:
    def __init__(self):
        self.b = bytearray()

    def i32(self, v):
        self.b += struct.pack('<i', v)

    def u8(self, v):
        self.b.append(v & 0xFF)


def build_keys(keys):
    w = W()
    w.i32(len(keys))
    offs = []
    for t, v in keys:
        offs.append(len(w.b))
        w.u8(t)
        if t == 0:
            e = v.encode('utf-8')
            w.i32(len(e))
            w.b += e
        else:
            raise ValueError(t)
    return bytes(w.b), offs


def parse_keys(blob):
    p = 0
    n = struct.unpack_from('<i', blob, p)[0]
    p += 4
    out = []
    for _ in range(n):
        t = blob[p]
        p += 1
        ln = struct.unpack_from('<i', blob, p)[0]
        p += 4
        out.append((t, blob[p:p + ln].decode('utf-8')))
        p += ln
    return out


def build_buckets(bs):
    w = W()
    w.i32(len(bs))
    for off, ents in bs:
        w.i32(off)
        w.i32(len(ents))
        for e in ents:
            w.i32(e)
    return bytes(w.b)


def parse_buckets(blob):
    p = 0
    n = struct.unpack_from('<i', blob, p)[0]
    p += 4
    out = []
    for _ in range(n):
        off = struct.unpack_from('<i', blob, p)[0]
        p += 4
        c = struct.unpack_from('<i', blob, p)[0]
        p += 4
        ents = list(struct.unpack_from('<%di' % c, blob, p))
        p += 4 * c
        out.append((off, ents))
    return out


def build_entries(es):
    w = W()
    w.i32(len(es))
    for e in es:
        for x in e:
            w.i32(x)
    return bytes(w.b)


def parse_entries(blob):
    p = 0
    n = struct.unpack_from('<i', blob, p)[0]
    p += 4
    out = []
    for _ in range(n):
        out.append(tuple(struct.unpack_from('<7i', blob, p)))
        p += 28
    return out


def pc_hashes():
    cat = json.load(open(PC_CATALOG, encoding='utf-8-sig'))
    keys = parse_keys(base64.b64decode(cat['m_KeyDataString']))
    out = {}
    for i in range(len(keys) - 1):
        a, b = keys[i][1], keys[i + 1][1]
        if a.endswith('.asset') and len(b) == 32:
            out[a.split('/')[-1]] = b
    return out


def main():
    if os.path.exists(OUT_DIR):
        shutil.rmtree(OUT_DIR)
    os.makedirs(OUT_DIR)

    z = zipfile.ZipFile(V14)
    # 中转文件放临时目录：UnityPy 会一直占着它的句柄，放 OUT_DIR 就删不掉、会被打进 APK
    bp = os.path.join(tempfile.gettempdir(), 'wulin_mod_base.bundle')
    open(bp, 'wb').write(z.read(INNER))
    # catalog 也从 v14 原生带过来（安卓的 Addressables 格式）
    open(os.path.join(OUT_DIR, 'catalog_0.1.json'), 'wb').write(
        z.read('assets/Mods/Android/NewMyMod001/catalog_0.1.json'))
    open(os.path.join(OUT_DIR, 'catalog_0.1.hash'), 'wb').write(
        z.read('assets/Mods/Android/NewMyMod001/catalog_0.1.hash'))
    for f in ('Info.json', 'Logo.png'):
        shutil.copy2(os.path.join(PC_DIR, f), os.path.join(OUT_DIR, f))

    env = UnityPy.load(bp)
    pids = {v.path_id: k for k, v in env.container.items()}
    template = next(o for o in env.objects if o.type.name == 'MonoBehaviour')
    sf = template.assets_file
    scripts = {}
    for obj in env.objects:
        if obj.type.name == 'MonoScript':
            n = struct.unpack_from('<i', obj.get_raw_data(), 0)[0]
            scripts[obj.get_raw_data()[4:4 + n].decode()] = obj.path_id
    print('MonoScript:', {k: hex(v & 0xFFFFFFFFFFFFFFFF) for k, v in scripts.items()})

    env2 = UnityPy.load(PC_BUNDLE)
    pids2 = {v.path_id: k for k, v in env2.container.items()}
    pc = {}
    for obj in env2.objects:
        if obj.type.name == 'MonoBehaviour':
            pc[pids2.get(obj.path_id, '').split('/')[-1]] = obj.get_raw_data()
    print('PC 资源:', len(pc), sorted(pc))

    # ---- 1) 共有 asset：换 raw（保留安卓 m_Script）
    print('\n[1] 共有 asset 覆盖')
    for obj in env.objects:
        if obj.type.name != 'MonoBehaviour':
            continue
        tail = pids.get(obj.path_id, '').split('/')[-1]
        if tail not in SHARED:
            continue
        src = pc.get(tail)
        if not src:
            print('   !! PC 没有', tail)
            continue
        dst = obj.get_raw_data()
        f_s, p_s = script_of(dst)
        new = bytearray(src)
        struct.pack_into('<iq', new, 16, f_s, p_s)
        new = bytes(new)
        if tail == 'BuffData.asset':
            before = len(new)
            new = fix_buff_raw(new)
            print('   %-26s 额外改写十全大补丸 patch：%d -> %d' % (tail, before, len(new)))
        obj.set_raw_data(new)
        print('   %-26s %6d -> %6d' % (tail, len(dst), len(new)))

    # ---- 1b) 复用 v14 原有的 DynamicModifier 槽位 ----
    # 金鲤鱼（buff 10026161）引用的是 DynamicModifier/Item/TeBieXunZhang.asset，
    # 那个 asset 是 ModEditor 构建时就打进 bundle 的；事后用 UnityPy 新塞进去的
    # MonoBehaviour 不会被注册进 cachedDynamicModifierDict。
    # 所以把 ShiQuanDaBuWan 的数据写进 TeBieXunZhang 这个已有槽位，路径与 m_Name 都保持原样。
    print('\n[1b] 复用 TeBieXunZhang 槽位')
    for obj in env.objects:
        if obj.type.name != 'MonoBehaviour':
            continue
        tail = pids.get(obj.path_id, '').split('/')[-1]
        if tail != 'TeBieXunZhang.asset':
            continue
        src = pc.get('ShiQuanDaBuWan.asset')
        if not src:
            print('   !! PC 没有 ShiQuanDaBuWan.asset')
            break
        h = split_raw(src)
        h['script'] = (0, scripts['DynamicModifier'])
        h['name'] = b'TeBieXunZhang'
        new = build_raw(h)
        obj.set_raw_data(new)
        print('   TeBieXunZhang.asset  raw %d -> %d（内容 = PC 的 ShiQuanDaBuWan）'
              % (len(obj.get_raw_data()), len(new)))

    # ---- 2) 新增 asset
    print('\n[2] 新增 asset')
    ab_obj = [o for o in env.objects if o.type.name == 'AssetBundle'][0]
    ab = ab_obj.read()
    used = {o.path_id for o in env.objects}
    added = []
    for fname, subdir, rel, newname, restype in NEW_ASSETS:
        src = pc.get(fname)
        if src is None:
            print('   !! PC 没有', fname)
            continue
        h = split_raw(src)
        h['script'] = (0, scripts['DynamicModifier' if restype == 1 else 'BaseDataClassPatch'])
        h['name'] = newname.encode('utf-8')
        raw = build_raw(h)
        while True:
            nid = random.randint(1, 2**62) * random.choice((1, -1))
            if nid not in used:
                used.add(nid)
                break
        sf.objects[nid] = attrs.evolve(template, path_id=nid, data=raw, byte_size=len(raw))
        pre = len(ab.m_PreloadTable)
        ab.m_PreloadTable.append(PPtr(m_FileID=0, m_PathID=nid, assetsfile=sf))
        ab.m_PreloadTable.append(PPtr(m_FileID=0, m_PathID=nid, assetsfile=sf))
        key = 'Assets/_Mods/NewMyMod001/%s/%s.asset' % (subdir, rel)
        ab.m_Container.append((key, AssetInfo(asset=PPtr(m_FileID=0, m_PathID=nid, assetsfile=sf),
                                              preloadIndex=pre, preloadSize=2)))
        added.append((fname, rel, subdir, restype, len(raw)))
        print('   + %-26s raw=%d resType=%d' % (fname, len(raw), restype))

    ab.save()
    blob = env.file.save()
    tmp = os.path.join(OUT_DIR, '_out.bundle')
    open(tmp, 'wb').write(blob)
    bpath = os.path.join(OUT_DIR, os.path.basename(INNER))
    shutil.copy2(tmp, bpath)
    for f in ('_base.bundle', '_out.bundle'):
        try:
            os.remove(os.path.join(OUT_DIR, f))
        except OSError:
            pass
    print('\nbundle: %d 字节' % os.path.getsize(bpath))
    env3 = UnityPy.load(bpath)
    print('   对象 %d，container %d' % (len(list(env3.objects)), len(env3.container)))

    # ---- 3) catalog
    print('\n[3] catalog')
    cp = os.path.join(OUT_DIR, 'catalog_0.1.json')
    cat = json.load(open(cp, encoding='utf-8-sig'))
    ids = list(cat['m_InternalIds'])
    keys = parse_keys(base64.b64decode(cat['m_KeyDataString']))
    buckets = parse_buckets(base64.b64decode(cat['m_BucketDataString']))
    entries = parse_entries(base64.b64decode(cat['m_EntryDataString']))
    hashes = pc_hashes()
    dep_hash = entries[1][3]
    for fname, rel, subdir, restype, nraw in added:
        full = 'Assets/_Mods/NewMyMod001/%s/%s.asset' % (subdir, rel)
        ids.append(full)
        hk = hashes.get(fname) or ('%032x' % random.getrandbits(128))
        keys.append((0, '%s/%s.asset' % (subdir, rel)))
        keys.append((0, hk))
        entries.append((len(ids) - 1, 1, 0, dep_hash, -1, len(keys) - 2, restype))
        buckets.append((0, [len(entries) - 1]))
        buckets.append((0, [len(entries) - 1]))
        print('   + %s  resType=%d  hash=%s' % (full, restype, hk))

    kblob, koffs = build_keys(keys)
    nb = [(koffs[i] if i < len(koffs) else 0, ents) for i, (_, ents) in enumerate(buckets)]
    cat['m_InternalIds'] = ids
    cat['m_KeyDataString'] = base64.b64encode(kblob).decode()
    cat['m_BucketDataString'] = base64.b64encode(build_buckets(nb)).decode()
    cat['m_EntryDataString'] = base64.b64encode(build_entries(entries)).decode()
    json.dump(cat, open(cp, 'w', encoding='utf-8'), separators=(',', ':'), ensure_ascii=False)
    print('   InternalIds=%d keys=%d buckets=%d entries=%d' % (len(ids), len(keys), len(nb), len(entries)))

    # ---- 4) CRC
    import re
    b = base64.b64decode(cat['m_ExtraDataString'])
    MARK = b'AssetBundleRequestOptions'
    j = b.find(MARK) + len(MARK)
    ln = struct.unpack_from('<I', b, j)[0]
    js = b[j + 4:j + 4 + ln].decode('utf-16-le')
    size = os.path.getsize(bpath)
    js2 = re.sub(r'"m_Crc":\d+', '"m_Crc":0', js)
    js2 = re.sub(r'"m_BundleSize":\d+', '"m_BundleSize":%d' % size, js2)
    js2 = js2.replace('"m_UseCrcForCachedBundles":true', '"m_UseCrcForCachedBundles":false')
    nb2 = js2.encode('utf-16-le')
    blob2 = b[:j] + struct.pack('<I', len(nb2)) + nb2 + b[j + 4 + ln:]
    cat['m_ExtraDataString'] = base64.b64encode(blob2).decode()
    json.dump(cat, open(cp, 'w', encoding='utf-8'), separators=(',', ':'), ensure_ascii=False)
    print('\n[4] CRC=0，BundleSize=%d' % size)

    # 复核
    chk = json.load(open(cp, encoding='utf-8-sig'))
    k2 = parse_keys(base64.b64decode(chk['m_KeyDataString']))
    b2 = parse_buckets(base64.b64decode(chk['m_BucketDataString']))
    e2 = parse_entries(base64.b64decode(chk['m_EntryDataString']))
    ok = len(k2) == len(keys) and len(b2) == len(nb) and len(e2) == len(entries)
    print('复核:', 'OK' if ok else 'MISMATCH')

    # 清理中转文件：UnityPy 有时还占着句柄，Windows 上第一次删会失败（WinError 32），
    # 留着会被 swap_mod 一起打进 APK，所以重试几次并明确报出来。
    for f in ('_base.bundle', '_out.bundle'):
        p2 = os.path.join(OUT_DIR, f)
        for _ in range(5):
            if not os.path.exists(p2):
                break
            try:
                os.remove(p2)
            except OSError:
                import time
                time.sleep(0.3)
        if os.path.exists(p2):
            print('   !! 删不掉中转文件 %s（会被打进 APK，请手动删）' % f)

    print('\n目录:')
    for f in sorted(os.listdir(OUT_DIR)):
        print('   %8d  %s' % (os.path.getsize(os.path.join(OUT_DIR, f)), f))


if __name__ == '__main__':
    main()
