# -*- coding: utf-8 -*-
"""Census of MonoBehaviour key-sets in the config bundle."""
import sys
from collections import Counter

import UnityPy

env = UnityPy.load(sys.argv[1])
sig = Counter()
samples = {}
for obj in env.objects:
    if obj.type.name != 'MonoBehaviour':
        continue
    try:
        tt = obj.read_typetree()
    except Exception as e:
        sig[('<ERR>',)] += 1
        continue
    keys = tuple(sorted(tt.keys()))
    sig[keys] += 1
    samples.setdefault(keys, tt)
print('distinct schemas', len(sig))
for keys, n in sig.most_common(60):
    print('%4d  %s' % (n, ', '.join(k for k in keys[:14])))
