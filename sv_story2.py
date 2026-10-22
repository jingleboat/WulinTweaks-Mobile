# -*- coding: utf-8 -*-
import json, re, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

p = sys.argv[1] if len(sys.argv) > 1 else r"device-saves\Save1-20261022\Save1\SaveObjectStory.json"
raw = open(p, encoding="utf-8").read()
raw = re.sub(r'(?<=[{,])\s*(-?\d+)\s*:', lambda m: '"%s":' % m.group(1), raw)
v = json.loads(raw)["Data"]["value"]
gd = v["GraphDataList"]

want = sys.argv[2:] or ["R10049-0", "R10049-2"]
print("=== 指定图 ===")
for g in want:
    print(g, json.dumps(gd.get(g), ensure_ascii=False))

for st, label, key in ((5, "Failed", "State"), (6, "TimeOut", "State")):
    rows = [(g, d) for g, d in gd.items() if isinstance(d, dict) and d.get(key) == st]
    print(f"\n=== {label} ({len(rows)}) ===")
    for g, d in rows:
        print(" ", g, json.dumps(d, ensure_ascii=False))

print("\n=== 带 TimeLimit / LimitQuaterFrom 的图 ===")
for g, d in gd.items():
    if isinstance(d, dict) and (d.get("TimeLimitSaveID", 0) or d.get("LimitQuaterFrom", 0)):
        print(" ", g, json.dumps(d, ensure_ascii=False))

print("\n=== GraphHoldList (%d) ===" % len(v.get("GraphHoldList", {})))
for k, val in v.get("GraphHoldList", {}).items():
    print("  ", k, val)

print("\n=== StandPosList ===")
print(json.dumps(v.get("StandPosList"), ensure_ascii=False, indent=1)[:3000])
