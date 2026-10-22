# -*- coding: utf-8 -*-
import json, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

p = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor\Assets\GameRes\json\3\LocData.txt"
rows = json.loads(open(p, "rb").read().decode("utf-8-sig"))["LocData"]

print("=== uName 以 _name 结尾且中文含 纪/密室 ===")
for r in rows:
    un = r.get("uName") or ""
    sc = r.get("SChinese") or ""
    if un.endswith("_name") and ("纪" in sc or "密室" in sc):
        print(f"   {un:28} = {sc}   [EN] {r.get('English')}")

print("\n=== 中文含 密室 ===")
for r in rows:
    sc = r.get("SChinese") or ""
    if "密室" in sc:
        print(f"   {r.get('uName'):28} = {sc}")

# 交叉核对：把 _name 结尾的条目全部导出（剧情名清单）
names = [(r.get("uName"), r.get("SChinese")) for r in rows if (r.get("uName") or "").endswith("_name")]
print(f"\n共 {len(names)} 条 *_name 剧情名")
