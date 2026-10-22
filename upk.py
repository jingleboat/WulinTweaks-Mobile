# -*- coding: utf-8 -*-
"""Inspect a Unity bundle: object types + GameObject names, and Tilemap tile counts."""
import sys
from collections import Counter

import UnityPy


def main():
    path = sys.argv[1]
    env = UnityPy.load(path)
    types = Counter()
    gos = []
    tilemaps = []
    for obj in env.objects:
        tn = obj.type.name
        types[tn] += 1
        if tn == 'GameObject':
            try:
                d = obj.read()
                gos.append(d.m_Name)
            except Exception as e:
                gos.append('<err %s>' % e)
        elif tn == 'Tilemap':
            tilemaps.append(obj)

    print('== types ==')
    for k, v in types.most_common():
        print('  %-24s %d' % (k, v))
    print('== gameobjects (%d) ==' % len(gos))
    for n in gos:
        print('   ', n)
    print('== tilemaps (%d) ==' % len(tilemaps))
    for obj in tilemaps:
        try:
            d = obj.read()
            attrs = [a for a in dir(d) if not a.startswith('_')]
            name = getattr(d, 'm_Name', '?')
            print('  Tilemap %r attrs=%s' % (name, attrs[:40]))
        except Exception as e:
            print('  Tilemap read err:', e)


if __name__ == '__main__':
    main()
