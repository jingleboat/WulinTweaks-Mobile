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
loc = {}
for r in rows:
    un = r.get("uName") or ""
    if un:
        loc[un] = r.get("SChinese") or ""

def zh(key):
    return loc.get(key, "")

def load(slot):
    p = ROOT + "\\" + slot + "\\SaveObjectStory.save"
    raw = decrypt(p).decode("utf-8")
    raw = re.sub(r'(?<=[{,])\s*(-?\d+)\s*:', lambda m: '"%s":' % m.group(1), raw)
    return json.loads(raw)["Data"]["value"]

for slot in ("Save2", "Save0"):
    v = load(slot)
    hold = v.get("GraphHoldList", {})
    gdl = v.get("GraphDataList", {})
    print("=" * 86)
    print("%s：GraphHoldList %d 图 ／ GraphDataList %d 图" % (slot, len(hold), len(gdl)))
    print("=" * 86)

    noted = {}
    for gid, g in gdl.items():
        if isinstance(g, dict):
            notes = g.get("note") or []
            if notes:
                noted[gid] = notes

    print("—— 记事本有记录的（= 玩家在任务日志里能看到的）%d 条" % len(noted))
    for gid in sorted(noted):
        texts = "、".join(zh(n) or n for n in noted[gid])
        flag = "  [也在HoldList]" if gid in hold else ""
        print("  %-24s %s%s" % (gid, texts[:70], flag))

    print("\n—— 只在 HoldList、记事本没有的 %d 条" % len([g for g in hold if g not in noted]))
    for gid in sorted(g for g in hold if g not in noted):
        print("  %-24s" % gid)
    print()

print("=== LocData 里搜「纪家秘事」 ===")
for k, val in loc.items():
    if "纪家秘事" in (val or "") or "纪家" in (val or ""):
        print("  %-30s = %s" % (k, val[:50]))
