# -*- coding: utf-8 -*-
import os, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

BASE = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor\Assets\GameRes"
targets = ["一醉方休", "杯中明镜", "烈酒与故事", "纪家密室"]
pats = []
for t in targets:
    pats.append((t, t.encode("utf-8"), t.encode("utf-16-le")))

hits = {t: [] for t in targets}
scanned = 0
for sub in ("Scenario", "Story", "json"):
    root = os.path.join(BASE, sub)
    for dirpath, _, files in os.walk(root):
        for fn in files:
            if fn.endswith(".meta"):
                continue
            p = os.path.join(dirpath, fn)
            try:
                d = open(p, "rb").read()
            except Exception:
                continue
            scanned += 1
            for name, u8, u16 in pats:
                if u8 in d or u16 in d:
                    hits[name].append(p)

print("scanned files:", scanned)
for t in targets:
    print(f"\n=== {t}  -> {len(hits[t])} file(s)")
    for p in hits[t][:8]:
        print("   ", os.path.relpath(p, BASE))
