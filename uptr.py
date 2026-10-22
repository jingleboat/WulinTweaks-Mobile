# -*- coding: utf-8 -*-
"""离线：看 map_504 的 _SwimGrid 用的瓦片 PPtr 指向哪个 bundle。"""
import UnityPy

path = 'tmp_bundles/map_504.bundle'
env = UnityPy.load(path)
print('files:', [getattr(f, 'name', '?') for f in env.files])

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
    print('_SwimGrid m_TileAssetArray:', tt.get('m_TileAssetArray')[:3])
    print('_SwimGrid m_TileSpriteArray:', tt.get('m_TileSpriteArray')[:3])
    print('tiles sample:', (tt.get('m_Tiles') or [])[:2])
    break

for f in env.files:
    ext = getattr(f, 'externals', None)
    print('file', getattr(f, 'name', '?'), 'externals:', (ext[:6] if ext else ext))
