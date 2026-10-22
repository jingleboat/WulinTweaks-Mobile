import re, sys
p = r"F:\SteamLibrary\steamapps\common\WulinSH\Wulin_Data\il2cpp_data\Metadata\global-metadata.dat"
d = open(p,"rb").read()
print("size", len(d))
pat = re.compile(rb"[\x20-\x7e]{4,120}")
seen=set()
for m in pat.finditer(d):
    s = m.group().decode("ascii")
    if re.search(r"(?i)dlc", s) and s not in seen:
        seen.add(s)
        print(repr(s))
