import struct, re
p = r"F:\SteamLibrary\steamapps\common\WulinSH\Wulin_Data\il2cpp_data\Metadata\global-metadata.dat"
d = open(p,"rb").read()
sanity, version = struct.unpack_from("<Ii", d, 0)
print("sanity %#x version %d size %d" % (sanity, version, len(d)))
hdr = struct.unpack_from("<Ii 20I", d, 0)
names = ["sanity","version","stringLiteralOffset","stringLiteralCount","stringLiteralDataOffset","stringLiteralDataCount",
         "stringOffset","stringCount","eventsOffset","eventsCount","propertiesOffset","propertiesCount",
         "methodsOffset","methodsCount","parameterDefaultValuesOffset","parameterDefaultValuesCount",
         "fieldDefaultValuesOffset","fieldDefaultValuesCount","fieldAndParameterDefaultValueDataOffset","fieldAndParameterDefaultValueDataCount",
         "fieldMarshaledSizesOffset","fieldMarshaledSizesCount"]
for n,v in zip(names, hdr): print(f"  {n:38} {v}")
lo, lc, ldo, ldc = hdr[2], hdr[3], hdr[4], hdr[5]
n = lc // 8
print("literal entries:", n)
idx = 0x1dbd34
# try as index
if idx < n:
    ln, di = struct.unpack_from("<II", d, lo + idx*8)
    print("as index -> len", ln, "dataIdx", di, "str", d[ldo+di: ldo+di+ln])
