import json, sys, io

KW = sys.argv[1:] or ['水池', '水潭', '游泳']
path = 'dumpout-intl/stringliteral.json'
data = json.load(open(path, 'r', encoding='utf-8'))
# structure: list of {address, value} or dict
items = data if isinstance(data, list) else data.get('ScriptString', data)
if isinstance(items, dict):
    items = list(items.values())

def rows():
    for it in items:
        if isinstance(it, dict):
            v = it.get('value') or it.get('Value') or it.get('str') or ''
            a = it.get('address') or it.get('Address') or ''
        else:
            v, a = str(it), ''
        yield a, v

for kw in KW:
    hits = [(a, v) for a, v in rows() if kw in v]
    print('=== %s : %d' % (kw, len(hits)))
    for a, v in hits[:60]:
        print('  %-12s %s' % (a, v.replace('\n', '\\n')[:120]))
