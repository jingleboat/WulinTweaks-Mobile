# -*- coding: utf-8 -*-
import json, sys, io, re
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

p = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor\Assets\GameRes\json\3\LocData.txt"
raw = open(p, "rb").read().decode("utf-8-sig")
j = json.loads(raw)
rows = j["LocData"]
print("LocData rows:", len(rows))

targets = ["一醉方休", "杯中明镜", "烈酒与故事", "纪家密室"]
print("\n=== 精确匹配 SChinese ===")
for t in targets:
    exact = [r for r in rows if r.get("SChinese") == t]
    print(f"\n[{t}] exact={len(exact)}")
    for r in exact:
        print("   uName =", r.get("uName"), " uid =", r.get("uid"), " EN =", r.get("English"))

print("\n=== 苗彩蝶 ===")
for r in rows:
    if r.get("SChinese") == "苗彩蝶":
        print("   uName =", r.get("uName"), " uid =", r.get("uid"), " EN =", r.get("English"))
        break
