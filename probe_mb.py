# -*- coding: utf-8 -*-
"""验证 UnityPy 能否在 2022.3.33 的 IL2CPP bundle 上做『读->写->再读』往返，
并解析 MonoBehaviour raw data 的对象头，为 raw data 移植做准备。"""
import os
import struct
import sys
import tempfile
import zipfile

import UnityPy

APK = r'E:\笔记\wulin_v14.apk'
INNER = 'assets/Mods/Android/NewMyMod001/newmymod001_assets_all_dd3656b265f5596de8c8c2b7f3491a9c.bundle'
PC = r'F:\SteamLibrary\steamapps\common\WulinSH\Mods\NewMyMod001\newmymod001_assets_all_b124b9f7cc553d7885d74564d60c9764.bundle'


def parse_header(raw):
    """解析 MonoBehaviour raw data 的公共头：m_GameObject, m_Enabled, m_Script, m_Name"""
    fileid_go, pathid_go = struct.unpack_from('<iq', raw, 0)
    enabled = struct.unpack_from('<i', raw, 12)[0]
    fileid_s, pathid_s = struct.unpack_from('<iq', raw, 16)
    nlen = struct.unpack_from('<i', raw, 28)[0]
    name = raw[32:32 + nlen].decode('utf-8', 'replace')
    p = 32 + nlen
    # 对齐到 4
    p = (p + 3) & ~3
    tlen = struct.unpack_from('<i', raw, p)[0]
    tname = raw[p + 4:p + 4 + tlen].decode('utf-8', 'replace')
    return dict(go=(fileid_go, pathid_go), enabled=enabled,
                script=(fileid_s, pathid_s), name=name, typename=tname, data_at=p + 4 + tlen)


def container_of(env):
    return {k: v.path_id for k, v in env.container.items()}


def main():
    z = zipfile.ZipFile(APK)
    tmp = tempfile.mkdtemp()
    a = os.path.join(tmp, 'android.bundle')
    open(a, 'wb').write(z.read(INNER))
    print('android bundle:', os.path.getsize(a))

    print('\n=== 1) Android bundle：解析每个 MonoBehaviour 的头 ===')
    env = UnityPy.load(a)
    cont = container_of(env)
    pids = {v: k for k, v in cont.items()}
    scripts = {}
    for obj in env.objects:
        if obj.type.name == 'MonoScript':
            raw = obj.get_raw_data()
            scripts[obj.path_id] = raw
    print('MonoScript pathIDs:', {hex(k): v[:60] for k, v in scripts.items()})

    for obj in env.objects:
        if obj.type.name != 'MonoBehaviour':
            continue
        raw = obj.get_raw_data()
        h = parse_header(raw)
        label = pids.get(obj.path_id, '?').split('/')[-1]
        print('  %-28s pid=%-22s script=%s name=%s type=%s data_at=%d'
              % (label, obj.path_id, hex(h['script'][1] & 0xFFFFFFFFFFFFFFFF),
                 h['name'], h['typename'], h['data_at']))

    print('\n=== 2) PC bundle：同样的头 ===')
    env2 = UnityPy.load(PC)
    cont2 = container_of(env2)
    pids2 = {v: k for k, v in cont2.items()}
    for obj in env2.objects:
        if obj.type.name != 'MonoBehaviour':
            continue
        raw = obj.get_raw_data()
        h = parse_header(raw)
        label = pids2.get(obj.path_id, '?').split('/')[-1]
        print('  %-28s pid=%-22s script=%s name=%s type=%s data_at=%d'
              % (label, obj.path_id, hex(h['script'][1] & 0xFFFFFFFFFFFFFFFF),
                 h['name'], h['typename'], h['data_at']))

    print('\n=== 3) 往返测试：save 后再 load ===')
    out = os.path.join(tmp, 'roundtrip.bundle')
    try:
        with open(out, 'wb') as f:
            f.write(env.file.save())
        print('  saved:', os.path.getsize(out), 'bytes')
        env3 = UnityPy.load(out)
        n = sum(1 for o in env3.objects if o.type.name == 'MonoBehaviour')
        print('  reload objects: MonoBehaviour=%d, container=%d' % (n, len(env3.container)))
        print('  header:', open(out, 'rb').read(40))
        ok = all('SolveRiddles' not in k or True for k in env3.container)
        print('  RESULT:', 'OK' if n == 16 else 'MISMATCH')
    except Exception as e:
        print('  FAILED:', type(e).__name__, e)


if __name__ == '__main__':
    main()
