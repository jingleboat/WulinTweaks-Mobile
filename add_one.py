# -*- coding: utf-8 -*-
"""往安卓原生 bundle 里『新增一个 MonoBehaviour』并登记到 AssetBundle.container

先只加 1 个（SolveRiddlesConfig）验证可行性：
  · 复制现有 MonoBehaviour 的 ObjectReader 作为模板（同 bundle、同 Script）
  · path_id 用新的 64 位随机值
  · raw data 换成 PC 版 SolveRiddlesConfig 的（m_Script 改成本 bundle 的 BaseDataClassPatch）
  · AssetBundle.m_Container 追加路径 + AssetInfo
  · 存盘后重新加载校验
"""
import os
import random
import struct
import sys
import zipfile

import attrs
import UnityPy
from UnityPy.classes import AssetInfo, PPtr

HERE = os.path.dirname(os.path.abspath(__file__))
APK = r'E:\笔记\wulin_v14.apk'
INNER = 'assets/Mods/Android/NewMyMod001/newmymod001_assets_all_dd3656b265f5596de8c8c2b7f3491a9c.bundle'
PC_BUNDLE = r'F:\SteamLibrary\steamapps\common\WulinSH\Mods\NewMyMod001\newmymod001_assets_all_b124b9f7cc553d7885d74564d60c9764.bundle'
OUT = os.path.join(HERE, 'probe_add.bundle')


def header(raw):
    fileid_s, pathid_s = struct.unpack_from('<iq', raw, 16)
    return fileid_s, pathid_s


def load_pc_assets():
    env = UnityPy.load(PC_BUNDLE)
    pids = {v.path_id: k for k, v in env.container.items()}
    out = {}
    for obj in env.objects:
        if obj.type.name == 'MonoBehaviour':
            out[pids.get(obj.path_id, '').split('/')[-1]] = obj.get_raw_data()
    return out


def main():
    z = zipfile.ZipFile(APK)
    data = z.read(INNER)
    open(OUT, 'wb').write(data)
    env = UnityPy.load(OUT)

    pids = {v.path_id: k for k, v in env.container.items()}
    print('bundle: %d 个 container，%d 个对象' % (len(env.container), len(env.objects)))

    # Script pathID 表
    scripts = {}
    for obj in env.objects:
        if obj.type.name == 'MonoScript':
            raw = obj.get_raw_data()
            n = struct.unpack_from('<i', raw, 0)[0]
            scripts[raw[4:4 + n].decode()] = obj.path_id
    print('MonoScript:', {k: hex(v & 0xFFFFFFFFFFFFFFFF) for k, v in scripts.items()})
    base_patch = scripts['BaseDataClassPatch']

    # 模板对象（任一 MonoBehaviour）
    template = next(o for o in env.objects if o.type.name == 'MonoBehaviour')
    sf = template.assets_file
    print('SerializedFile:', type(sf).__name__, '对象数', len(sf.objects))
    print('模板 path_id:', template.path_id, 'class_id:', template.class_id,
          'type_id:', template.type_id)

    pc = load_pc_assets()
    src = pc['SolveRiddlesConfig.asset']
    f_s, p_s = header(src)
    newraw = bytearray(src)
    struct.pack_into('<iq', newraw, 16, 0, base_patch)   # m_Script 换成本 bundle 的
    newraw = bytes(newraw)
    print('PC SolveRiddlesConfig raw=%d, m_Script %s -> %s'
          % (len(newraw), hex(p_s & 0xFFFFFFFFFFFFFFFF), hex(base_patch & 0xFFFFFFFFFFFFFFFF)))

    # 新 path_id
    used = {o.path_id for o in env.objects}
    while True:
        nid = random.randint(1, 2**62) * random.choice((1, -1))
        if nid not in used:
            break
    print('新 path_id:', nid)

    newobj = attrs.evolve(template, path_id=nid, data=newraw, byte_size=len(newraw))
    sf.objects[nid] = newobj
    print('已加入 objects，现在 %d 个' % len(sf.objects))

    # container
    ab_obj = [o for o in env.objects if o.type.name == 'AssetBundle'][0]
    ab = ab_obj.read()
    print('PreloadTable 长度:', len(ab.m_PreloadTable))
    max_pre = max(v.preloadIndex for _, v in ab.m_Container)
    print('现有 preloadIndex 最大值:', max_pre)

    key = 'Assets/_Mods/NewMyMod001/GameDataPatch/SolveRiddlesConfig.asset'
    info = AssetInfo(asset=PPtr(m_FileID=0, m_PathID=nid, assetsfile=sf),
                     preloadIndex=max_pre, preloadSize=1)
    ab.m_Container.append((key, info))
    ab.save()
    print('container 已追加:', key)

    blob = env.file.save()
    open(OUT, 'wb').write(blob)
    print('写出 %d 字节' % len(blob))

    # 复检
    env2 = UnityPy.load(OUT)
    print('\n复检: 对象 %d，container %d' % (len(list(env2.objects)), len(env2.container)))
    for o in env2.objects:
        if o.type.name == 'MonoBehaviour' and o.path_id == nid:
            raw = o.get_raw_data()
            print('  新对象 raw=%d，前 96 hex=%s' % (len(raw), raw[:96].hex()))
            txt = raw.decode('utf-8', 'ignore')
            i = txt.find('{')
            print('  JSON 片段:', txt[i:i + 80].replace('\r\n', ' '))
    hit = [k for k in env2.container if 'SolveRiddles' in k]
    print('  container 里有 SolveRiddles:', hit)


if __name__ == '__main__':
    main()
