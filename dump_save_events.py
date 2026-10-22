# -*- coding: utf-8 -*-
import hashlib, gzip, json, re, sys, io
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

ROOT = r"C:\Users\User\AppData\LocalLow\DefaultCompany\Wulin\76561198807754750"
LOC = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor\Assets\GameRes\json\3\LocData.txt"

def decrypt(p):
    d = open(p, "rb").read()
    iv, ct = d[:16], d[16:]
    key = hashlib.pbkdf2_hmac("sha1", b"Meow", iv, 100, 16)
    c = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    pt = c.update(ct) + c.finalize()
    pt = pt[:-pt[-1]]
    return gzip.decompress(pt) if pt[:2] == b"\x1f\x8b" else pt

rows = json.loads(open(LOC, "rb").read().decode("utf-8-sig"))["LocData"]
name = {}
for r in rows:
    un = r.get("uName") or ""
    if un.endswith("_name"):
        name.setdefault(un[:-5], r.get("SChinese") or "")

def dump(slot):
    p = ROOT + "\\" + slot + "\\SaveObjectStory.save"
    raw = decrypt(p).decode("utf-8")
    raw = re.sub(r'(?<=[{,])\s*(-?\d+)\s*:', lambda m: '"%s":' % m.group(1), raw)
    v = json.loads(raw)["Data"]["value"]
    hold = v.get("GraphHoldList", {})
    result = v.get("ResultList", {})
    print("=" * 80)
    print("%s   剧情图 %d 个   ResultList %d 项" % (slot, len(hold), len(result)))
    print("=" * 80)
    print("%-24s %-6s %s" % ("剧情图ID", "Hold", "名称"))
    print("-" * 80)
    for gid in sorted(hold.keys()):
        n = len(hold[gid]) if isinstance(hold[gid], list) else hold[gid]
        print("%-24s %-6s %s" % (gid, n, name.get(gid, "")))
    print()

for slot in ("Save2", "Save0"):
    dump(slot)
