# -*- coding: utf-8 -*-
"""Resolve tile asset names per cell and cross-check with _Block / _SwimGrid."""
import sys
from collections import Counter

import UnityPy


def main():
    path = sys.argv[1]
    env = UnityPy.load(path)
    # index MonoBehaviour names (RuleTile assets)
    names = {}
    for obj in env.objects:
        if obj.type.name != 'MonoBehaviour':
            continue
        try:
            tt = obj.read_typetree()
            names[obj.path_id] = tt.get('m_Name', '?')
        except Exception:
            pass
    print('monobehaviour names indexed', len(names))
    print('sample:', sorted(names.values())[:12])

    grids = {}
    celltile = {}
    for obj in env.objects:
        if obj.type.name != 'Tilemap':
            continue
        try:
            gname = obj.read().m_GameObject.read().m_Name
        except Exception:
            continue
        tt = obj.read_typetree()
        arr = tt.get('m_TileAssetArray') or []
        cells = {}
        for pos, td in tt.get('m_Tiles') or []:
            idx = td.get('m_TileIndex', -1)
            nm = '?'
            if 0 <= idx < len(arr):
                pid = arr[idx].get('m_PathID') if isinstance(arr[idx], dict) else getattr(arr[idx], 'path_id', None)
                nm = names.get(pid, '<ext>')
            key = (pos.get('x', 0), pos.get('y', 0))
            cells[key] = nm
        if gname in ('_Block', '_UnBlock', '_SwimGrid', '_SwimLadder'):
            grids[gname] = set(cells)
        elif gname.startswith('Ground'):
            for k, v in cells.items():
                celltile.setdefault(k, []).append((gname, v))

    print('grids:', {k: len(v) for k, v in grids.items()})
    blk = grids.get('_Block', set())
    sw = grids.get('_SwimGrid', set())

    # tile-name census over Ground layers
    cnt = Counter()
    for k, lst in celltile.items():
        for g, n in lst:
            cnt[n] += 1
    print('tile names on ground layers:')
    for n, c in cnt.most_common(20):
        print('   %-30s %d' % (n, c))

    # water-ish tile names
    waterish = [n for n in cnt if any(w in n.lower() for w in ('water', 'river', 'lake', 'sea', 'pond', 'swim', '水'))]
    print('water-ish tiles:', waterish)
    for w in waterish:
        cells = {k for k, lst in celltile.items() if any(n == w for _, n in lst)}
        print('  %-24s cells=%-5d in _Block=%d  in _SwimGrid=%d' % (w, len(cells), len(cells & blk), len(cells & sw)))


if __name__ == '__main__':
    main()
