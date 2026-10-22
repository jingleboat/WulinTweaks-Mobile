# -*- coding: utf-8 -*-
"""把 PC「综合护肝」独有的 7 个 patch 加进安卓 bundle，并在 catalog 里注册

bundle 侧：
  · 复制一个 MonoBehaviour 作为模板 → 新 path_id → raw data 用 PC 的
  · m_Script 换成本 bundle 的 MonoScript pathID
  · AssetBundle.m_Container 追加路径；m_PreloadTable 扩展

catalog 侧（Addressables ContentCatalogData）：
  · m_InternalIds 追加全路径
  · m_KeyDataString 追加 [相对路径, hash] 两个 key（hash 从 PC catalog 抄）
  · m_BucketDataString 追加两个 bucket（keyOffset 按新 KeyData 重算）
  · m_EntryDataString 追加 entry
"""
import base64
import json
import os
import random
import struct
import sys

import attrs
import UnityPy
from UnityPy.classes import AssetInfo, PPtr

HERE = os.path.dirname(os.path.abspath(__file__))
MOD_DIR = os.path.join(HERE, 'modswap3', 'NewMyMod001')
BUNDLE_NAME = 'newmymod001_assets_all_dd3656b265f5596de8c8c2b7f3491a9c.bundle'
PC_BUNDLE = r'F:\SteamLibrary\steamapps\common\WulinSH\Mods\NewMyMod001\newmymod001_assets_all_b124b9f7cc553d7885d74564d60c9764.bundle'
PC_CATALOG = r'F:\SteamLibrary\steamapps\common\WulinSH\Mods\NewMyMod001\catalog_0.1.0.json'

# (PC 里的文件名, 目标目录, catalog 里的相对路径, 新 m_Name, 资源类型索引)
NEW_ASSETS = [
    ('BountyHunterBaseTask.asset', 'GameDataPatch', 'BountyHunterBaseTask', 'BountyHunterBaseTask', 2),
    ('FactionData.asset', 'GameDataPatch', 'FactionData', 'FactionData', 2),
    ('GameEnvConstData 1.asset', 'GameDataPatch', 'GameEnvConstData2', 'GameEnvConstData2', 2),
    ('SelfPropBufflData.asset', 'GameDataPatch', 'SelfPropBufflData', 'SelfPropBufflData', 2),
    ('SolveRiddlesConfig.asset', 'GameDataPatch', 'SolveRiddlesConfig', 'SolveRiddlesConfig', 2),
    ('TeacheData.asset', 'GameDataPatch', 'TeacheData', 'TeacheData', 2),
    ('ShiQuanDaBuWan.asset', 'DynamicModifier/Item', 'ShiQuanDaBuWan', 'ShiQuanDaBuWan', 1),
]


# ------------------------------------------------------------ raw data 工具
def split_raw(raw):
    """拆成 (头, m_Name, typeName, 数据区)"""
    f_go, p_go = struct.unpack_from('<iq', raw, 0)
    enabled = struct.unpack_from('<i', raw, 12)[0]
    f_s, p_s = struct.unpack_from('<iq', raw, 16)
    nlen = struct.unpack_from('<i', raw, 28)[0]
    name = raw[32:32 + nlen]
    p = (32 + nlen + 3) & ~3
    tlen = struct.unpack_from('<i', raw, p)[0]
    tname = raw[p + 4:p + 4 + tlen]
    tail = raw[p + 4 + tlen:]
    return dict(go=(f_go, p_go), enabled=enabled, script=(f_s, p_s),
                name=name, tname=tname, tail=tail)


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


# ------------------------------------------------------------- catalog blob
class W:
    def __init__(self):
        self.out = bytearray()

    def i32(self, v):
        self.out += struct.pack('<i', v)

    def u8(self, v):
        self.out.append(v & 0xFF)

    def bytes(self, b):
        self.out += b


def build_keys(keys):
    """keys: [(type, value)] -> (blob, [每个 key 在 blob 里的偏移])"""
    w = W()
    w.i32(len(keys))
    offs = []
    for t, v in keys:
        offs.append(len(w.out))
        w.u8(t)
        if t == 0:
            b = v.encode('utf-8')
            w.i32(len(b))
            w.bytes(b)
        elif t == 1:
            w.i32(len(v))
            w.bytes(v.encode('utf-16-le'))
        else:
            raise ValueError('unsupported key type %d' % t)
    return w.out, offs


def parse_keys(blob):
    p = 0
    n = struct.unpack_from('<i', blob, p)[0]
    p += 4
    keys = []
    for _ in range(n):
        t = blob[p]
        p += 1
        if t in (0, 1):
            ln = struct.unpack_from('<i', blob, p)[0]
            p += 4
            raw = blob[p:p + ln]
            p += ln
            keys.append((t, raw.decode('utf-8' if t == 0 else 'utf-16-le', 'replace')))
        else:
            raise ValueError('key type %d' % t)
    return keys


def build_buckets(bs):
    w = W()
    w.i32(len(bs))
    for ents in bs:
        w.i32(ents[0])       # keyOffset
        w.i32(len(ents[1]))
        for e in ents[1]:
            w.i32(e)
    return w.out


def parse_buckets(blob):
    p = 0
    n = struct.unpack_from('<i', blob, p)[0]
    p += 4
    out = []
    for _ in range(n):
        off = struct.unpack_from('<i', blob, p)[0]
        p += 4
        cnt = struct.unpack_from('<i', blob, p)[0]
        p += 4
        ents = list(struct.unpack_from('<%di' % cnt, blob, p))
        p += 4 * cnt
        out.append((off, ents))
    return out


def build_entries(es):
    w = W()
    w.i32(len(es))
    for e in es:
        for x in e:
            w.i32(x)
    return w.out


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
    """从 PC catalog 的 KeyData 里抄每个 asset 的 hash"""
    cat = json.load(open(PC_CATALOG, encoding='utf-8-sig'))
    keys = parse_keys(base64.b64decode(cat['m_KeyDataString']))
    out = {}
    for i in range(len(keys) - 1):
        a, b = keys[i][1], keys[i + 1][1]
        if a.endswith('.asset') and len(b) == 32:
            out[a.split('/')[-1]] = b
    return out


def main():
    bundle_path = os.path.join(MOD_DIR, BUNDLE_NAME)
    env = UnityPy.load(bundle_path)
    pids = {v.path_id: k for k, v in env.container.items()}
    template = next(o for o in env.objects if o.type.name == 'MonoBehaviour')
    sf = template.assets_file

    scripts = {}
    for obj in env.objects:
        if obj.type.name == 'MonoScript':
            n = struct.unpack_from('<i', obj.get_raw_data(), 0)[0]
            scripts[obj.get_raw_data()[4:4 + n].decode()] = obj.path_id
    print('MonoScript:', {k: hex(v & 0xFFFFFFFFFFFFFFFF) for k, v in scripts.items()})

    # PC 侧资源
    env2 = UnityPy.load(PC_BUNDLE)
    pids2 = {v.path_id: k for k, v in env2.container.items()}
    pc = {}
    for obj in env2.objects:
        if obj.type.name == 'MonoBehaviour':
            pc[pids2.get(obj.path_id, '').split('/')[-1]] = obj.get_raw_data()
    print('PC assets:', len(pc))

    ab_obj = [o for o in env.objects if o.type.name == 'AssetBundle'][0]
    ab = ab_obj.read()
    used = {o.path_id for o in env.objects}
    added = []

    for fname, subdir, rel, newname, restype in NEW_ASSETS:
        src = pc.get(fname)
        if src is None:
            print('!! PC 没有', fname)
            continue
        h = split_raw(src)
        monospace = 'DynamicModifier' if restype == 1 else 'BaseDataClassPatch'
        h['script'] = (0, scripts[monospace])
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
        key = 'Assets/_Mods/NewMyMod001/%s/%s.asset' % (subdir, rel)
        ab.m_Container.append((key, AssetInfo(asset=PPtr(m_FileID=0, m_PathID=nid, assetsfile=sf),
                                              preloadIndex=pre, preloadSize=1)))
        added.append((fname, rel, subdir, nid, restype, len(raw)))
        print('  + %-26s -> %s  raw=%d  pid=%d  resType=%d' % (fname, key, len(raw), nid, restype))

    ab.save()
    blob = env.file.save()
    open(bundle_path, 'wb').write(blob)
    print('\nbundle 写出 %d 字节（%d 个对象 / %d 个 container）'
          % (len(blob), len(list(UnityPy.load(bundle_path).objects)), len(UnityPy.load(bundle_path).container)))

    # ------------------------------------------------------------ catalog
    cp = os.path.join(MOD_DIR, 'catalog_0.1.json')
    cat = json.load(open(cp, encoding='utf-8-sig'))
    ids = list(cat['m_InternalIds'])
    keys = parse_keys(base64.b64decode(cat['m_KeyDataString']))
    buckets = parse_buckets(base64.b64decode(cat['m_BucketDataString']))
    entries = parse_entries(base64.b64decode(cat['m_EntryDataString']))
    hashes = pc_hashes()
    print('\ncatalog: %d 个 InternalId / %d 个 key / %d 个 entry'
          % (len(ids), len(keys), len(entries)))

    dep_hash = entries[1][3] if len(entries) > 1 else 0
    for fname, rel, subdir, nid, restype, nraw in added:
        full = 'Assets/_Mods/NewMyMod001/%s/%s.asset' % (subdir, rel)
        ids.append(full)
        hkey = hashes.get(fname) or ('%032x' % random.getrandbits(128))
        keys.append((0, '%s/%s.asset' % (subdir, rel)))
        keys.append((0, hkey))
        entries.append((len(ids) - 1, 1, 0, dep_hash, -1, len(keys) - 2, restype))
        buckets.append((0, [len(entries) - 1]))
        buckets.append((0, [len(entries) - 1]))
        print('  + catalog: %s  hash=%s  resType=%d' % (full, hkey, restype))

    kblob, koffs = build_keys(keys)
    # 重算 bucket 的 keyOffset：第 i 个 bucket 对应第 i 个 key
    nb = []
    for i, (_, ents) in enumerate(buckets):
        nb.append((koffs[i] if i < len(koffs) else 0, ents))
    bb = build_buckets(nb)
    eb = build_entries(entries)

    cat['m_InternalIds'] = ids
    cat['m_KeyDataString'] = base64.b64encode(kblob).decode('ascii')
    cat['m_BucketDataString'] = base64.b64encode(bb).decode('ascii')
    cat['m_EntryDataString'] = base64.b64encode(eb).decode('ascii')
    json.dump(cat, open(cp, 'w', encoding='utf-8'), separators=(',', ':'), ensure_ascii=False)
    print('catalog 写出：InternalIds=%d keys=%d buckets=%d entries=%d'
          % (len(ids), len(keys), len(nb), len(entries)))

    # 复核：重新解析能不能对上
    chk = json.load(open(cp, encoding='utf-8-sig'))
    k2 = parse_keys(base64.b64decode(chk['m_KeyDataString']))
    b2 = parse_buckets(base64.b64decode(chk['m_BucketDataString']))
    e2 = parse_entries(base64.b64decode(chk['m_EntryDataString']))
    ok = (len(k2) == len(keys) and len(b2) == len(nb) and len(e2) == len(entries)
          and all(b2[i][0] == koffs[i] for i in range(len(b2))))
    print('复核:', 'OK' if ok else 'MISMATCH')


if __name__ == '__main__':
    main()
