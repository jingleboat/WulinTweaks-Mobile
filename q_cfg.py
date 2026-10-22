# -*- coding: utf-8 -*-
"""Search the config payload for map-name config and resolve map ids."""
import re
import sys

d = open('_cfg.bin', 'rb').read()
print('payload', len(d))
for pat in [b'MapConfig', b'mapName', b'MapName', b'AreaName', b'_SwimGrid']:
    print(pat.decode(), d.count(pat))

m = d.find(b'MapConfig')
if m > 0:
    print('--- context around first MapConfig ---')
    print(d[max(0, m - 300):m + 500].decode('utf8', 'replace'))
