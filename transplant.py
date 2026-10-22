# -*- coding: utf-8 -*-
"""把 PC mod 的 patch 数据搬进安卓原生 bundle（保持安卓 catalog 完全不变）

原理：
  MonoBehaviour raw data = [m_GameObject(12)] [m_Enabled(4)] [m_Script(12)] [m_Name(str)]
                          [typeName(str)] [patch 数据]
  两边结构完全一致，唯一不能跨 bundle 搬的是 m_Script 的 pathID
  （安卓 BaseDataClassPatch = 0x23a7a28d48c9167b，PC 的是 0xd5066fcfc721dd7a）。
  所以替换时保留目标 asset 自己的 m_Script，其余字节照搬。
"""
import os
import shutil
import struct
import sys
import zipfile

import UnityPy

APK = r'E:\笔记\wulin_v14.apk'
INNER = 'assets/Mods/Android/NewMyMod001/newmymod001_assets_all_dd3656b265f5596de8c8c2b7f3491a9c.bundle'
PC_DIR = r'F:\SteamLibrary\steamapps\common\WulinSH\Mods\NewMyMod001'
PC_BUNDLE = os.path.join(PC_DIR, 'newmymod001_assets_all_b124b9f7cc553d7885d74564d60c9764.bundle')
OUT_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'modswap3', 'NewMyMod001')

# 两边都有的 asset（按 container 路径尾名匹配）
SHARED = ['BuffData.asset', 'FactionGoodsData.asset', 'FactionJobData.asset',
          'GameEnvConstData.asset', 'GameEnvConstStrData.asset', 'MapBuildingKitData.asset']


def header(raw):
    fileid_s, pathid_s = struct.unpack_from('<iq', raw, 16)
    return fileid_s, pathid_s


def main():
    if os.path.exists(OUT_DIR):
        shutil.rmtree(OUT_DIR)
    os.makedirs(OUT_DIR)

    z = zipfile.ZipFile(APK)
    work = os.path.join(OUT_DIR, '_tmp.bundle')
    open(work, 'wb').write(z.read(INNER))

    env = UnityPy.load(work)
    pids = {v.path_id: k for k, v in env.container.items()}

    # 收集 PC 侧 raw data
    pc_raw = {}
    env2 = UnityPy.load(PC_BUNDLE)
    pids2 = {v.path_id: k for k, v in env2.container.items()}
    for obj in env2.objects:
        if obj.type.name != 'MonoBehaviour':
            continue
        tail = pids2.get(obj.path_id, '').split('/')[-1]
        pc_raw[tail] = obj.get_raw_data()
    print('PC assets:', sorted(pc_raw))

    done = []
    for obj in env.objects:
        if obj.type.name != 'MonoBehaviour':
            continue
        tail = pids.get(obj.path_id, '').split('/')[-1]
        if tail not in SHARED:
            continue
        src = pc_raw.get(tail)
        if not src:
            print('  !! PC 没有 %s，跳过' % tail)
            continue
        dst = obj.get_raw_data()
        f_s, p_s = header(dst)
        f2, p2 = header(src)
        # 用目标 bundle 的 m_Script（fileID + pathID），其余整段照搬
        new = bytearray(src)
        struct.pack_into('<iq', new, 16, f_s, p_s)
        obj.set_raw_data(bytes(new))
        done.append((tail, len(dst), len(src), hex(p2 & 0xFFFFFFFFFFFFFFFF)))

    print('\n替换 %d 个 asset:' % len(done))
    for t, a, b, p2 in done:
        print('  %-26s %6d -> %6d   (PC script %s -> 保留安卓)' % (t, a, b, p2))

    blob = env.file.save()
    with open(os.path.join(OUT_DIR, os.path.basename(INNER)), 'wb') as f:
        f.write(blob)
    print('\n新 bundle:', len(blob), 'bytes')

    # 复检
    env3 = UnityPy.load(os.path.join(OUT_DIR, os.path.basename(INNER)))
    print('复检: MonoBehaviour=%d container=%d'
          % (sum(1 for o in env3.objects if o.type.name == 'MonoBehaviour'), len(env3.container)))
    for obj in env3.objects:
        if obj.type.name != 'MonoBehaviour':
            continue
        tail = {v.path_id: k for k, v in env3.container.items()}.get(obj.path_id, '').split('/')[-1]
        if tail in SHARED:
            raw = obj.get_raw_data()
            txt = raw.decode('utf-8', 'ignore')
            i = txt.find('{')
            print('  %-26s raw=%5d  %s' % (tail, len(raw), txt[i:i + 90].replace('\r\n', ' ') if i >= 0 else '(binary)'))

    # 组装 mod 目录：安卓原生 catalog（不动）+ PC 的 Info/Logo
    for f in ('catalog_0.1.json', 'catalog_0.1.hash'):
        open(os.path.join(OUT_DIR, f), 'wb').write(z.read('assets/Mods/Android/NewMyMod001/' + f))
    for f in ('Info.json', 'Logo.png'):
        shutil.copy2(os.path.join(PC_DIR, f), os.path.join(OUT_DIR, f))
    os.remove(work)
    print('\n目录就绪:', OUT_DIR)
    for f in sorted(os.listdir(OUT_DIR)):
        print('   %8d  %s' % (os.path.getsize(os.path.join(OUT_DIR, f)), f))


if __name__ == '__main__':
    main()
