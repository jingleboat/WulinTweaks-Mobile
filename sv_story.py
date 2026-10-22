# -*- coding: utf-8 -*-
import json, re, sys, io, os
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

p = sys.argv[1] if len(sys.argv) > 1 else r"device-saves\Save1-20261022\Save1\SaveObjectStory.json"
raw = open(p, encoding="utf-8").read()
raw = re.sub(r'(?<=[{,])\s*(-?\d+)\s*:', lambda m: '"%s":' % m.group(1), raw)
j = json.loads(raw)
v = j["Data"]["value"]

print("=== 顶层键 ===")
for k in v:
    val = v[k]
    if isinstance(val, dict):
        print(f"  {k}: dict[{len(val)}]")
    elif isinstance(val, list):
        print(f"  {k}: list[{len(val)}]")
    else:
        print(f"  {k}: {val!r}")

gd = v.get("GraphDataList", {})
print(f"\n=== GraphDataList: {len(gd)} 个图 ===")

ST = {0:"NotOpen",1:"Started",2:"Close",3:"CloseAfterOpen",4:"Finish",5:"?"}
from collections import Counter
cnt = Counter()
rows = []
for gid, g in gd.items():
    st = g.get("State")
    cnt[st] += 1
    rows.append((gid, st, g.get("LastPlayTime",0), g.get("note")))
print("State 分布:", dict(cnt))
print()
for gid, st, t, note in sorted(rows):
    print(f"{gid:<28} State={st!r:<4} Last={t:<10} note={note}")
