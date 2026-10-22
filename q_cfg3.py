# -*- coding: utf-8 -*-
import re
import sys

d = open('_cfg.bin', 'rb').read()
targets = [b'"504"', b'"2160"', b'"2169"', b'MapIDShift', b'MapName', b'AreaName']
for t in targets:
    idx = [m.start() for m in re.finditer(re.escape(t), d)][:4]
    print(t.decode(), len(idx), idx)
    for i in idx[:2]:
        print('   ctx:', d[max(0, i - 220):i + 260].decode('utf8', 'replace').replace('\n', ' | '))
