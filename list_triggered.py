# -*- coding: utf-8 -*-
import hashlib, gzip, json, re, sys, io
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

def decrypt(save):
    data = open(save, "rb").read()
    iv, ct = data[:16], data[16:]
    key = hashlib.pbkdf2_hmac("sha1", b"Meow", iv, 100, 16)
    d = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    pt = d.update(ct) + d.finalize()
    pt = pt[:-pt[-1]]
    if pt[:2] == b"\x1f\x8b":
        pt = gzip.decompress(pt)
    return pt

SAVE = r"C:\Users\User\AppData\LocalLow\DefaultCompany\Wulin\76561198807754750\Save0\SaveObjectStory.save"
raw = decrypt(SAVE).decode("utf-8")
raw = re.sub(r'(?<=[{,])\s*(-?\d+)\s*:', lambda m: '"%s":' % m.group(1), raw)
v = json.loads(raw)["Data"]["value"]

hold = v.get("GraphHoldList", {})
data = v.get("GraphDataList", {})

LOC = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor\Assets\GameRes\json\3\LocData.txt"
rows = json.loads(open(LOC, "rb").read().decode("utf-8-sig"))["LocData"]
byname = {}
for r in rows:
    un = r.get("uName") or ""
    if un.endswith("_name"):
        byname.setdefault(un[:-5], r.get("SChinese") or "")

print("已触发剧情图（GraphHoldList）共 %d 个：\n" % len(hold))
print("%-24s %-6s %-6s %s" % ("剧情图ID", "Hold", "有数据", "名称"))
print("-" * 78)
for gid in sorted(hold.keys()):
    n = len(hold[gid]) if isinstance(hold[gid], list) else hold[gid]
    has = "是" if gid in data else "-"
    nm = byname.get(gid, "")
    print("%-24s %-6s %-6s %s" % (gid, n, has, nm))
