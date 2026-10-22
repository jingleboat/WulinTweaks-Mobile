# -*- coding: utf-8 -*-
import os, re, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

S = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor\Assets\GameRes\Scenario"
targets = ["R10005-11", "R10028-7", "R10025-6", "R10112-2"]

with_partner = []
per_target = {t: [] for t in targets}
scanned = 0

for fn in os.listdir(S):
    if not fn.endswith(".asset"):
        continue
    p = os.path.join(S, fn)
    try:
        d = open(p, "rb").read()
    except Exception:
        continue
    scanned += 1
    has = (b"partnerID" in d) or (b"partnerId" in d) or (b"partnerName" in d)
    if has:
        with_partner.append(fn)
        for t in targets:
            if fn.startswith(t):
                per_target[t].append(fn)

print("scanned:", scanned)
print("含 partnerID/partnerName 的剧情图 asset:", len(with_partner))
print("\n=== 目标图是否含队友条件 ===")
for t in targets:
    files = sorted(set(os.path.splitext(f)[0] for f in per_target[t]))
    print(f"  {t}: {files if files else '无（该图没有 partnerID 字段）'}")

print("\n=== 含队友条件的图（前 20 个文件名，即图 ID）===")
ids = sorted(set(re.sub(r"_\d+.*$", "", f.replace(".asset", "")) for f in with_partner))
print("  去重后的图 ID 数:", len(ids))
for i in ids[:20]:
    print("   ", i)
print("\n  是否包含目标图 ID:", [t for t in targets if t in ids])
