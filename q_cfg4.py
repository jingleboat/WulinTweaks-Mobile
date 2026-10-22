# -*- coding: utf-8 -*-
import re

d = open('_cfg.bin', 'rb').read()
for t in [b'map_504', b'map_2160', b'map_2169', b'prefab', b'Mods/']:
    print(t.decode(), d.count(t))
m = d.find(b'map_504')
print('first map_504 at', m)
if m > 0:
    print(d[max(0, m - 400):m + 400])
