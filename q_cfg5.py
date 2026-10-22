# -*- coding: utf-8 -*-
"""Look for console-command strings in the config payload."""
import re

d = open('_cfg.bin', 'rb').read()
idx = [m.start() for m in re.finditer(rb'ConsoleCommand', d)]
print('ConsoleCommand occurrences:', len(idx))
for i in idx[:6]:
    print('---', i)
    print(d[max(0, i - 300):i + 600].decode('utf8', 'replace').replace('\x00', '.')[:900])
