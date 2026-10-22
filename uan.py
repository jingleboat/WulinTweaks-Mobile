# -*- coding: utf-8 -*-
"""List distinct tile asset names used by Ground layers of a map prefab."""
import sys
from collections import Counter

import UnityPy


def tname(pptr):
    try:
        return pptr.read().m_Name
    except Exception as e:
        return '<err>'


def main():
    path = sys.argv[1]
    env = UnityPy.load(path)
    for obj in env.objects:
        if obj.type.name != 'Tilemap':
            continue
        try:
            name = obj.read().m_GameObject.read().m_Name
        except Exception:
            continue
        if not name.startswith('Ground'):
            continue
        tt = obj.read_typetree()
        arr = tt.get('m_TileAssetArray') or []
        counts = Counter()
        for _, td in tt.get('m_Tiles') or []:
            idx = td.get('m_TileIndex', -1)
            if 0 <= idx < len(arr):
                counts[idx] += 1
        names = {}
        for idx in counts:
            names[idx] = tname(arr[idx])
        print('--- %s : %d cells, %d distinct tiles' % (name, sum(counts.values()), len(counts)))
        for idx, c in counts.most_common(12):
            print('    %-28s %d' % (names.get(idx, '?'), c))


if __name__ == '__main__':
    main()
