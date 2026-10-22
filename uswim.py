# -*- coding: utf-8 -*-
"""Compare _Block vs _SwimGrid tile positions in a map prefab bundle."""
import sys

import UnityPy


def go_name(obj):
    try:
        return obj.read().m_Name
    except Exception:
        return obj.read_typetree().get('m_Name', '?')


def tiles_of(obj):
    tt = obj.read_typetree()
    out = []
    for t in tt.get('m_Tiles') or []:
        pos = t[0] if isinstance(t, tuple) else t
        out.append((pos.get('x', 0), pos.get('y', 0)))
    return out


def main():
    path = sys.argv[1]
    print('==', path)
    env = UnityPy.load(path)
    grids = {}
    for obj in env.objects:
        if obj.type.name != 'Tilemap':
            continue
        try:
            name = data_name = obj.read().m_GameObject
            name = go_name(data_name)
        except Exception as e:
            name = '<err %s>' % e
        if name in ('_Block', '_UnBlock', '_SwimGrid', '_SwimLadder'):
            grids[name] = set(tiles_of(obj))
    for k, v in grids.items():
        if v:
            xs = [p[0] for p in v]; ys = [p[1] for p in v]
            print('%-12s cells=%-5d bbox x %d..%d  y %d..%d' % (k, len(v), min(xs), max(xs), min(ys), max(ys)))
    blk = grids.get('_Block', set())
    unb = grids.get('_UnBlock', set())
    sw = grids.get('_SwimGrid', set())
    lad = grids.get('_SwimLadder', set())
    if sw:
        print('swim  & block      = %d' % len(sw & blk))
        print('swim  - block      = %d  (walkable water cells)' % len(sw - blk))
        print('swim  & unblock    = %d' % len(sw & unb))
        print('ladder& block      = %d' % len(lad & blk))
        sample = sorted(sw - blk)[:12]
        print('sample enterable water cells:', sample)


if __name__ == '__main__':
    main()
