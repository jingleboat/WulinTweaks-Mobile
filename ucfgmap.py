# -*- coding: utf-8 -*-
"""Scan the config bundle's MonoBehaviours for a map-id/name table."""
import time
import sys

import UnityPy

path = sys.argv[1]
t0 = time.time()
env = UnityPy.load(path)
print('loaded containers:', len(env.container) if hasattr(env, 'container') else '?')

objs = list(env.objects)
print('objects', len(objs), 'load %.1fs' % (time.time() - t0))

seen = 0
for obj in objs:
    if obj.type.name != 'MonoBehaviour':
        continue
    seen += 1
    try:
        tt = obj.read_typetree()
    except Exception:
        continue
    keys = set(tt.keys())
    low = {k.lower() for k in keys}
    if ('mapid' in low or 'map_id' in low) and ('mapname' in low or 'name' in low):
        print('CAND', obj.path_id, sorted(keys)[:16])
        for k, v in tt.items():
            s = repr(v)
            print('   ', k, s[:200])
        break
    if seen > 4000:
        print('cap reached')
        break
print('scanned monos', seen, '%.1fs' % (time.time() - t0))
