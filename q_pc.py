# -*- coding: utf-8 -*-
"""Resolve crash PCs to nearest il2cpp method."""
import bisect
import json

ADDRS = [0x3d9e264, 0x3d9e46c, 0x2313654, 0x23138e0, 0x3D7E2A0, 0x3D936EC, 0x3D923FC]

sym = {}
with open('dumpout-intl/script.json', encoding='utf-8') as f:
    sc = json.load(f)
for m in sc.get('ScriptMethod', []):
    try:
        sym[int(m['Address'])] = m.get('Name', '?')
    except Exception:
        pass
addrs = sorted(sym)

for pc in ADDRS:
    i = bisect.bisect_right(addrs, pc) - 1
    if i < 0:
        print(hex(pc), '-> ?')
        continue
    base = addrs[i]
    print('%#x -> %s (+%#x)' % (pc, sym[base], pc - base))
