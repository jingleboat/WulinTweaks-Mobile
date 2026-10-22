MD = r"F:\SteamLibrary\steamapps\common\WulinSH\Wulin_Data\il2cpp_data\Metadata\global-metadata.dat"
with open(MD, 'rb') as f:
    data = f.read()
print('size', len(data))

needles = [
    "Expected '{' or",
    'or "null", found',
    '"null", found',
    "found '\"'",
    "Expected '[' or",
]
for n in needles:
    for enc, name in (('utf-8', 'utf8'), ('utf-16-le', 'utf16')):
        b = n.encode(enc)
        hits = []
        start = 0
        while True:
            idx = data.find(b, start)
            if idx == -1 or len(hits) >= 3:
                break
            hits.append(idx)
            start = idx + 1
        print(f"{name:6} {n!r:24} -> {hits}")
        for off in hits[:2]:
            if enc == 'utf-16-le':
                print('   ctx:', data[max(0, off-80):off+120].decode('utf-16-le', 'replace'))
            else:
                print('   ctx:', data[max(0, off-80):off+120].decode('utf-8', 'replace'))
