# -*- coding: utf-8 -*-
import os, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

p = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor\Assets\GameRes\json\3\LocData.txt"
raw = open(p, "rb").read()
print("size", len(raw))
txt = None
for enc in ("utf-8", "utf-8-sig", "utf-16-le", "utf-16"):
    try:
        txt = raw.decode(enc)
        print("decoded as", enc, "chars", len(txt))
        break
    except Exception:
        continue
print("--- first 600 chars ---")
print(txt[:600])

lines = txt.splitlines()
print("\nlines:", len(lines))
for name in ["一醉方休", "杯中明镜", "烈酒与故事", "纪家密室"]:
    print(f"\n=== {name}")
    for i, ln in enumerate(lines):
        if name in ln:
            print(f"  L{i+1}: {ln[:300]}")
