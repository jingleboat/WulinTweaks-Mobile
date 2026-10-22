import re
MD = r"F:\SteamLibrary\steamapps\common\WulinSH\Wulin_Data\il2cpp_data\Metadata\global-metadata.dat"
d = open(MD, 'rb').read()
pat = re.compile(rb"'\{\}' or")
for m in pat.finditer(d):
    print(m.start(), repr(d[max(0, m.start()-120):m.start()+120].decode('utf-8', 'replace')))
print('---- plain  "Expected" ----')
for m in list(re.finditer(rb"Expected", d))[:20]:
    print(m.start(), repr(d[m.start()-40:m.start()+120].decode('utf-8', 'replace')))
