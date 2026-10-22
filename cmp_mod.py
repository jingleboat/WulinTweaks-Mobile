# -*- coding: utf-8 -*-
"""对比安卓原生 mod 与 PC 移植 mod 的 patch 资源（重点 SolveRiddlesConfig）

用法: python -X utf8 cmp_mod.py
"""
import json
import os
import re
import tempfile
import zipfile

import UnityPy

APK = r'E:\笔记\wulin_v15-综合护肝-2026-10-20.apk'
PC = r'F:\SteamLibrary\steamapps\common\WulinSH\Mods\NewMyMod001'

TARGETS = [
    ('Bianjiecaozuo(安卓原生)', 'assets/Mods/Android/Bianjiecaozuo/bianjiecaozuo_assets_all_ba75dfb36d84a314708e39a2de83bc78.bundle'),
    ('NewMyMod001(移植PC)', 'assets/Mods/Android/NewMyMod001/newmymod001_assets_all_b124b9f7cc553d7885d74564d60c9764.bundle'),
]


def cat_ids(z, slot):
    p = 'assets/Mods/Android/%s/catalog_0.1.json' % slot
    try:
        j = json.loads(z.read(p).decode('utf-8-sig'))
    except KeyError:
        return []
    return j.get('m_InternalIds', [])


def show_catalog(z, slot):
    print('---- %s catalog ----' % slot)
    ids = cat_ids(z, slot)
    for i in ids:
        print('   ', i)
    print()


def dump_bundle(tag, path):
    print('=' * 70)
    print(tag)
    print('  file:', path, os.path.getsize(path), 'bytes')
    head = open(path, 'rb').read(48)
    print('  header:', head[:40])
    env = UnityPy.load(path)
    print('  container:')
    for k in env.container:
        print('    ', k)
    print('  objects:')
    for obj in env.objects:
        if obj.type.name == 'MonoBehaviour':
            raw = obj.get_raw_data()
            nm = [k for k, v in env.container.items() if v.path_id == obj.path_id]
            label = nm[0].split('/')[-1] if nm else ('pid%s' % obj.path_id)
            if 'SolveRiddles' in label:
                print('   >>> %s  raw=%d' % (label, len(raw)))
                print('       head hex:', raw[:80].hex())
                prints = re.findall(rb'[\x20-\x7e\xc0-\xff]{6,}', raw)
                for s in prints[:6]:
                    print('       str:', s[:160])
            else:
                print('   %s  raw=%d' % (label, len(raw)))
    print()


def main():
    z = zipfile.ZipFile(APK)
    show_catalog(z, 'Bianjiecaozuo')
    show_catalog(z, 'NewMyMod001')
    tmp = tempfile.mkdtemp()
    for tag, inner in TARGETS:
        p = os.path.join(tmp, inner.split('/')[-1])
        with open(p, 'wb') as f:
            f.write(z.read(inner))
        dump_bundle(tag, p)
    dump_bundle('PC(Steam 综合护肝)', os.path.join(
        PC, 'newmymod001_assets_all_b124b9f7cc553d7885d74564d60c9764.bundle'))


if __name__ == '__main__':
    main()
