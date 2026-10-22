# -*- coding: utf-8 -*-
"""查多个剧情图在 LocData 里的显示名与描述"""
import json, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
LOC = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor\Assets\GameRes\json\3\LocData.txt"
rows = json.loads(open(LOC, "rb").read().decode("utf-8-sig"))["LocData"]
loc = {}
for r in rows:
    un = r.get("uName") or ""
    if un:
        loc[un] = r.get("SChinese") or ""

for g in sys.argv[1:]:
    print("=" * 60)
    print(g)
    found = False
    for suf in ("_name", "_overview1", "_overview2", "_overview3", "_desc", "_tips"):
        if g + suf in loc:
            print(f"  {suf[1:]:<10} {loc[g+suf]}")
            found = True
    if not found:
        print("  (LocData 无条目)")
