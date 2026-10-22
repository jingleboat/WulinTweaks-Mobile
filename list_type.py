# -*- coding: utf-8 -*-
"""列出指定类型（含嵌套/同名不同命名空间）的全部方法与 RVA。"""
import re
import sys

DUMP = 'dumpout-intl/dump.cs'
TYPES = sys.argv[1:] or ['UIWorldMapManager', 'UIWorldMap', 'UICabMapManager',
                         'TradingWithNpcManager']

typerx = re.compile(r'(?:class|struct|enum|interface)\s+([A-Za-z_0-9.]+)')
cur = None
depth = 0
rva = None

for line in open(DUMP, encoding='utf-8', errors='replace'):
    st = line.strip()
    if st == '{':
        depth += 1
        rva = None
        continue
    if st == '}':
        depth -= 1
        if depth == 0:
            cur = None
        rva = None
        continue
    if depth == 0:
        m = typerx.search(st)
        cur = m.group(1) if (m and m.group(1) in TYPES) else None
        if cur:
            print('\n===== %s' % st[:110])
    if st.startswith('// RVA:'):
        rva = st[3:]
        continue
    if cur and rva and '(' in st:
        parts = {}
        for x in rva.split(', '):
            if ': ' in x:
                k, v = x.split(': ', 1)
                parts[k] = v
        print('   %-10s %s' % (parts.get('RVA', '?'), st[:110]))
        rva = None
        continue
    if not st.startswith('//') and not st.startswith('['):
        rva = None
