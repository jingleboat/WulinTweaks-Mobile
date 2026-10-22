# -*- coding: utf-8 -*-
import re

d = open('_cfg.bin', 'rb').read()
print('len', len(d), 'braces', d.count(b'{'), 'quotes', d.count(b'"'))
pat = re.compile(rb'(?:[\xe4-\xe9][\x80-\xbf][\x80-\xbf]){4,}')
count = 0
for m in pat.finditer(d):
    count += 1
    if count <= 5:
        s = max(0, m.start() - 160)
        print('--- cjk at', m.start(), 'len', m.end() - m.start())
        print(d[s:m.end() + 160].decode('utf8', 'replace'))
    if count > 4000:
        break
print('cjk runs found (capped):', count)
