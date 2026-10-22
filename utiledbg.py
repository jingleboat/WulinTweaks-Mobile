# -*- coding: utf-8 -*-
import sys

import UnityPy

path = sys.argv[1]
want = sys.argv[2] if len(sys.argv) > 2 else '_SwimGrid'
env = UnityPy.load(path)
for obj in env.objects:
    if obj.type.name != 'Tilemap':
        continue
    try:
        name = obj.read().m_GameObject.read().m_Name
    except Exception as e:
        continue
    if name != want:
        continue
    tt = obj.read_typetree()
    print('keys:', list(tt.keys()))
    tiles = tt.get('m_Tiles')
    print('m_Tiles type', type(tiles), 'len', len(tiles) if hasattr(tiles, '__len__') else '?')
    for t in tiles[:3]:
        print('  entry type', type(t), repr(t)[:400])
    break
