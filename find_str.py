import struct

DLL = r"F:\SteamLibrary\steamapps\common\WulinSH\GameAssembly.dll"
with open(DLL, 'rb') as f:
    data = f.read()

needles = [
    "Expected '{' or",
    'or "null", found',
    '"null", found',
    'ES3JSONReader',
    'MultiDataRef',
]
for n in needles:
    for enc, name in (('utf-8', 'utf8'), ('utf-16-le', 'utf16')):
        b = n.encode(enc)
        idx = data.find(b)
        hits = []
        start = 0
        while idx != -1 and len(hits) < 3:
            hits.append(idx)
            start = idx + 1
            idx = data.find(b, start)
        print(f"{name:6} {n!r:26} -> {hits}")
        if hits and 'found' in n:
            off = hits[0]
            print('   ctx:', data[off-60:off+80].decode('utf-8', 'replace'))
