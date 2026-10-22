# -*- coding: utf-8 -*-
"""从 APK 里的地图 prefab 抽出 `_SwimGrid` 格子，生成 native/src/water_mask.h。

背景：运行期地图实例里 `_SwimGrid` 这个图层已经不存在了（实测运行期图层列表只有 24 层，
没有 _SwimGrid/_SwimLadder），所以游戏自己的 IsSwimGrid 在手机版永远 false。
PC 上 NPC 能"在水里"，靠的就是这份 prefab 数据 —— 我们离线把它烤进 mod 里。

用法： python -X utf8 gen_water_mask.py
"""
import os
import sys
import zipfile

import UnityPy

APK = r'E:\笔记\base_sign.apk'
TMPL = 'assets/aa/Android/prefabs_map_assets_assets/_artwork/map/prefab/map_%s.prefab.bundle'
MAPS = [504, 2160, 2169]
OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'native', 'src', 'water_mask.h')


def swim_cells(blob):
    env = UnityPy.load(blob)
    for obj in env.objects:
        if obj.type.name != 'Tilemap':
            continue
        try:
            name = obj.read().m_GameObject.read().m_Name
        except Exception:
            continue
        if name != '_SwimGrid':
            continue
        tt = obj.read_typetree()
        cells = []
        for pos, _td in tt.get('m_Tiles') or []:
            cells.append((pos.get('x', 0), pos.get('y', 0)))
        return cells
    return []


def main():
    z = zipfile.ZipFile(APK)
    names = set(z.namelist())
    tables = []
    for mid in MAPS:
        p = TMPL % mid
        if p not in names:
            print('map_%s: bundle 不存在，跳过' % mid)
            continue
        cells = swim_cells(z.read(p))
        cells.sort(key=lambda c: (c[1] << 32) | (c[0] & 0xFFFFFFFF))
        tables.append((mid, cells))
        print('map_%s: %d 个水面格' % (mid, len(cells)))

    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('/* 自动生成：gen_water_mask.py —— 来自地图 prefab 的 _SwimGrid 图层，不要手改 */\n')
        f.write('#pragma once\n#include <stdint.h>\n\n')
        f.write('typedef struct { int mapid; int count; const int64_t* keys; } WaterMaskEntry;\n\n')
        for mid, cells in tables:
            keys = [((c[1] & 0xFFFFFFFF) << 32) | (c[0] & 0xFFFFFFFF) for c in cells]
            f.write('static const int64_t WM_%d[%d] = {\n' % (mid, max(1, len(keys))))
            if not keys:
                f.write('    0\n')
            else:
                for i in range(0, len(keys), 6):
                    f.write('    ' + ', '.join('0x%016xLL' % k for k in keys[i:i + 6]) + ',\n')
            f.write('};\n\n')
        f.write('static const WaterMaskEntry WATER_MASKS[] = {\n')
        for mid, cells in tables:
            f.write('    { %d, %d, WM_%d },\n' % (mid, len(cells), mid))
        f.write('};\n')
        f.write('#define WATER_MASK_COUNT %d\n' % len(tables))

    print('写出 %s (%.1f KB)' % (OUT, os.path.getsize(OUT) / 1024.0))


if __name__ == '__main__':
    sys.exit(main())
