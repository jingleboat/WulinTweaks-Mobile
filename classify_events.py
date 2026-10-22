# -*- coding: utf-8 -*-
import hashlib, gzip, json, re, sys, io
from collections import defaultdict
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

LOG = r"D:\steamgame\steamapps\common\WulinSH\BepInEx\LogOutput.log"
SAVE = r"C:\Users\User\AppData\LocalLow\DefaultCompany\Wulin\76561198807754750\Save0\SaveObjectStory.save"
LOC = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor\Assets\GameRes\json\3\LocData.txt"

# ---- 日志：每个 graph 的 因队友条件 / 全条件满足 计数 ----
team = defaultdict(int)
both = defaultdict(int)
pat = re.compile(r"\[compare\] graph=(\S+) 放宽=(\S+) 原版=(\S+)")
with open(LOG, encoding="utf-8", errors="replace") as f:
    for line in f:
        m = pat.search(line)
        if not m:
            continue
        g, rel, orig = m.group(1), m.group(2), m.group(3)
        if rel == "通过" and orig == "不通过":
            team[g] += 1
        elif rel == "通过" and orig == "通过":
            both[g] += 1

# ---- 存档：已触发剧情图 ----
def decrypt(p):
    d = open(p, "rb").read()
    iv, ct = d[:16], d[16:]
    key = hashlib.pbkdf2_hmac("sha1", b"Meow", iv, 100, 16)
    c = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    pt = c.update(ct) + c.finalize()
    pt = pt[:-pt[-1]]
    return gzip.decompress(pt) if pt[:2] == b"\x1f\x8b" else pt

raw = decrypt(SAVE).decode("utf-8")
raw = re.sub(r'(?<=[{,])\s*(-?\d+)\s*:', lambda m: '"%s":' % m.group(1), raw)
v = json.loads(raw)["Data"]["value"]
triggered = set(v.get("GraphHoldList", {}).keys())

# ---- 本地化名 ----
rows = json.loads(open(LOC, "rb").read().decode("utf-8-sig"))["LocData"]
name = {}
for r in rows:
    un = r.get("uName") or ""
    if un.endswith("_name"):
        name.setdefault(un[:-5], r.get("SChinese") or "")

print("日志覆盖的剧情图：%d 个（因队友条件 %d 个，全条件满足 %d 个）\n"
      % (len(set(list(team) + list(both))), len(team), len(both)))

print("=" * 84)
print("A. 存档已触发 & 因「角色在队」被放宽才通过的剧情图")
print("=" * 84)
hits = sorted((g for g in triggered if g in team), key=lambda g: -team[g])
if not hits:
    print("  （无）")
for g in hits:
    print("  %-24s 因队友条件 %-3d 次   全条件满足 %-3d 次   %s"
          % (g, team[g], both.get(g, 0), name.get(g, "")))

print()
print("=" * 84)
print("B. 存档已触发 & 全条件本来就满足（放宽与否都会触发）")
print("=" * 84)
clean = sorted((g for g in triggered if g not in team and g in both))
for g in clean:
    print("  %-24s %s" % (g, name.get(g, "")))

print()
print("=" * 84)
print("C. 存档已触发但日志里没比对到（当前未加载）")
print("=" * 84)
miss = sorted(g for g in triggered if g not in team and g not in both)
for g in miss:
    print("  %-24s %s" % (g, name.get(g, "")))
