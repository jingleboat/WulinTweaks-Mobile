# -*- coding: utf-8 -*-
"""Compare swim-grid data between the CN and INTL APKs for the 3 swim maps."""
import zipfile

import UnityPy

APKS = {
    'CN  ': r'E:\笔记\base.apk',
    'INTL': r'E:\笔记\base_sign.apk',
}
IDS = ['504', '2160', '2169']
TMPL = 'assets/aa/Android/prefabs_map_assets_assets/_artwork/map/prefab/map_%s.prefab.bundle'


def tilemaps(blob):
    import io
    env = UnityPy.load(blob)
    out = {}
    for obj in env.objects:
        if obj.type.name != 'Tilemap':
            continue
        try:
            name = obj.read().m_GameObject.read().m_Name
        except Exception:
            continue
        if name in ('_Block', '_UnBlock', '_SwimGrid', '_SwimLadder'):
            tt = obj.read_typetree()
            out[name] = len(tt.get('m_Tiles') or [])
    return out


for tag, apk in APKS.items():
    z = zipfile.ZipFile(apk)
    names = set(z.namelist())
    for mid in IDS:
        p = TMPL % mid
        if p not in names:
            print('%s map_%s : bundle missing' % (tag, mid))
            continue
        try:
            info = tilemaps(z.read(p))
        except Exception as e:
            print('%s map_%s : parse err %s' % (tag, mid, e))
            continue
        print('%s map_%s : %s' % (tag, mid, info))
