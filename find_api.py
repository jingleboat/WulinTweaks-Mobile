# -*- coding: utf-8 -*-
"""扫 dump.cs，定位若干类型的 RVA（为移动端移植做可行性盘点）。"""
import re

DUMP = 'dumpout-intl/dump.cs'

TYPES = [
    'InteractiveObject', 'ViewElemenetAdapator', 'RoamingManager', 'MapManager',
    'UIWorldMapManager', 'UIWorldMap', 'UICabMapManager',
    'IsJoinedTeamCondition', 'HavePartnerCondition', 'ConditionNode',
    'TradingWithNpcManager', 'GameItemPack', 'GameItemInstance',
    'CharacterPoolData', 'GameCharacterInstance', 'StealManager',
]
KEY = ('SetOutLine', 'ValueOffset', 'GetTotalValue', 'CheckAllConnectNode',
       'IsMapEnable', 'IsPassIcon', 'OnLoadMapComplated', 'AddItem', 'MergeInto',
       'clickType', 'buildingType', 'backPack', 'StealPool', 'ItemsDropOnKill')

typerx = re.compile(r'(?:class|struct|enum|interface)\s+([A-Za-z_0-9.]+)')
cur = None
rva = None
depth = 0
res = {}

with open(DUMP, encoding='utf-8', errors='replace') as f:
    for line in f:
        s = line.rstrip('\n')
        st = s.strip()
        if depth == 0:
            m = typerx.search(st)
            if m and m.group(1) in TYPES:
                cur = m.group(1)
                res.setdefault(cur, [])
            elif m:
                cur = None
        if st.startswith('// RVA:'):
            rva = st[3:]
            continue
        if cur is not None:
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
            if rva and any(k in st for k in KEY):
                res[cur].append((rva, st))
                rva = None
                continue
        if not st.startswith('//') and not st.startswith('['):
            rva = None

for t in TYPES:
    rows = res.get(t) or []
    print('=' * 8, t, len(rows))
    for rv, sig in rows:
        parts = {}
        for x in rv.split(', '):
            if ': ' in x:
                k, v = x.split(': ', 1); parts[k] = v
        print('   RVA %-10s | %s' % (parts.get('RVA', '?'), sig[:120]))
