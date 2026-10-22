# -*- coding: utf-8 -*-
"""按正确口径重算：哪些『真正跑过的剧情』是被「角色在队」放宽才通过的。

口径说明（与之前的 GraphHoldList 口径不同）：
  * 『真正跑过』 = SaveObjectStory.GraphDataList 里存在的图（State 4=已完成 / 3=进行中）
  * 『因队友条件』 = 运行时 [compare] 行里 放宽=通过 且 原版=不通过 的图
  * 记事本 note 只有部分剧情有，不能当『是否跑过』的判据
"""
import hashlib, gzip, json, re, sys, io
from collections import defaultdict
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

ROOT = r"C:\Users\User\AppData\LocalLow\DefaultCompany\Wulin\76561198807754750"
LOG = r"D:\steamgame\steamapps\common\WulinSH\BepInEx\LogOutput.log"
LOC = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor\Assets\GameRes\json\3\LocData.txt"

STATE = {0: "NotOpen", 1: "Close", 2: "Opened", 3: "Started", 4: "已完成", 5: "Failed", 6: "TimeOut", 7: "CloseAfterOpen"}

def decrypt(p):
    d = open(p, "rb").read()
    iv, ct = d[:16], d[16:]
    key = hashlib.pbkdf2_hmac("sha1", b"Meow", iv, 100, 16)
    c = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    pt = c.update(ct) + c.finalize()
    pt = pt[:-pt[-1]]
    return gzip.decompress(pt) if pt[:2] == b"\x1f\x8b" else pt

def load(slot):
    raw = decrypt(ROOT + "\\" + slot + "\\SaveObjectStory.save").decode("utf-8")
    raw = re.sub(r'(?<=[{,])\s*(-?\d+)\s*:', lambda m: '"%s":' % m.group(1), raw)
    return json.loads(raw)["Data"]["value"]

loc = {}
for r in json.loads(open(LOC, "rb").read().decode("utf-8-sig"))["LocData"]:
    un = r.get("uName") or ""
    if un:
        loc[un] = r.get("SChinese") or ""
def name_of(g):
    return loc.get(g + "_name", "") or loc.get(g + "_overview1", "")

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

print("日志里比对到的剧情图：%d 个（因队友条件 %d，全条件满足 %d）\n"
      % (len(set(list(team) + list(both))), len(team), len(both)))

for slot in ("Save0", "Save2"):
    v = load(slot)
    gdl = v.get("GraphDataList", {})
    ran = {g for g, d in gdl.items() if isinstance(d, dict)}
    print("=" * 90)
    print("%s：GraphDataList 共 %d 个图（真正跑过的）" % (slot, len(ran)))
    print("=" * 90)

    rows = []
    for g in sorted(ran):
        st = gdl[g].get("State")
        note = gdl[g].get("note") or []
        cause = ""
        if g in team:
            cause = "★ 因队友条件才通过(%d)" % team[g]
        elif g in both:
            cause = "全条件本来就满足"
        else:
            cause = "（无比对数据）"
        rows.append((g, st, len(note), cause, name_of(g)))

    star = [r for r in rows if r[3].startswith("★")]
    print("\n— ★ 因「角色在队」才通过、且确实跑过的：%d 个" % len(star))
    for g, st, nn, cause, nm in star:
        print("  %-24s State=%-10s note=%-2d %-22s %s" % (g, STATE.get(st, st), nn, cause, nm))

    nocmp = [r for r in rows if r[3].startswith("（无")]
    print("\n— 跑过但这次没比对到的：%d 个" % len(nocmp))
    for g, st, nn, cause, nm in nocmp[:40]:
        print("  %-24s State=%-10s %s" % (g, STATE.get(st, st), nm))
    if len(nocmp) > 40:
        print("  ... 还有 %d 个" % (len(nocmp) - 40))
    print()
