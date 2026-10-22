import struct
p = r"F:\SteamLibrary\steamapps\common\WulinSH\Wulin_Data\il2cpp_data\Metadata\global-metadata.dat"
d = open(p,"rb").read()
hdr = struct.unpack_from("<Ii 20I", d, 0)
so, sc = hdr[6], hdr[7]
ldo, ldc = hdr[4], hdr[5]
off = 0x1dbd34
def cstr(base, off, cap=200):
    s = d[base+off: base+off+cap]
    z = s.find(b"\x00")
    return s[:z].decode("utf-8", "replace")
print("string table   :", repr(cstr(so, off)))
print("literal data   :", repr(cstr(ldo, off)))
