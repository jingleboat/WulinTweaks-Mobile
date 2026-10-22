# -*- coding: utf-8 -*-
"""List Tilemap objects with owning GameObject name and non-empty tile count."""
import sys

import UnityPy
from UnityPy.classes import PPtr


def go_name(obj):
    try:
        d = obj.read()
        return d.m_Name
    except Exception:
        try:
            return obj.read_typetree().get('m_Name', '?')
        except Exception as e:
            return '<err %s>' % e


def main():
    path = sys.argv[1]
    env = UnityPy.load(path)
    rows = []
    for obj in env.objects:
        if obj.type.name != 'Tilemap':
            continue
        data = obj.read()
        name = '<?>'
        try:
            gop = data.m_GameObject
            name = go_name(gop)
        except Exception as e:
            name = '<gerr %s>' % e
        ntiles = None
        try:
            tt = obj.read_typetree()
            tiles = tt.get('m_Tiles')
            if isinstance(tiles, list):
                ntiles = len(tiles)
            elif isinstance(tiles, dict):
                ntiles = len(tiles.get('Array', []) or [])
            else:
                ntiles = 'raw:%r' % type(tiles)
        except Exception as e:
            ntiles = 'tt-err:%s' % e
        rows.append((obj.path_id, name, ntiles))
    for pid, name, n in rows:
        print('Tilemap pid=%-24s %-24r tiles=%s' % (pid, name, n))


if __name__ == '__main__':
    main()
