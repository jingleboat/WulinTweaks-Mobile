# -*- coding: utf-8 -*-
"""Emulate LandManager land set: Ground0 & Building minus _Block; compare with _SwimGrid."""
import sys

import UnityPy


def cells(obj):
    tt = obj.read_typetree()
    out = set()
    for pos, td in tt.get('m_Tiles') or []:
        out.add((pos.get('x', 0), pos.get('y', 0)))
    return out


def main():
    for mid in sys.argv[1:]:
        path = 'tmp_bundles/map_%s.bundle' % mid
        env = UnityPy.load(path)
        s = {}
        for obj in env.objects:
            if obj.type.name != 'Tilemap':
                continue
            try:
                name = obj.read().m_GameObject.read().m_Name
            except Exception:
                continue
            if name in ('Ground0', 'Building', 'BuildingStatic', '_Block', '_UnBlock', '_SwimGrid'):
                s[name] = cells(obj)
        g0 = s.get('Ground0', set())
        bld = s.get('Building', set()) | s.get('BuildingStatic', set())
        blk = s.get('_Block', set())
        unb = s.get('_UnBlock', set())
        sw = s.get('_SwimGrid', set())
        land = (g0 | bld) - (blk - unb)
        print('== map_%s' % mid)
        print('   Ground0=%d Building=%d _Block=%d _UnBlock=%d _SwimGrid=%d' %
              (len(g0), len(bld), len(blk), len(unb), len(sw)))
        print('   land(Ground0|Building - Block)=%d' % len(land))
        print('   swim cells: in land=%d  not in land=%d' % (len(sw & land), len(sw - land)))
        # swim cells covered by Ground0 at all?
        print('   swim & Ground0=%d   swim - Ground0=%d' % (len(sw & g0), len(sw - g0)))
        # adjacency: land cell 4-adjacent to a swim cell
        adj = 0
        for (x, y) in sw:
            for dx, dy in ((1, 0), (-1, 0), (0, 1), (0, -1)):
                if (x + dx, y + dy) in land:
                    adj += 1
                    break
        print('   swim cells touching land (4-adj)=%d' % adj)


if __name__ == '__main__':
    main()
