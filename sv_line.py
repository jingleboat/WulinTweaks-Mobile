# -*- coding: utf-8 -*-
"""精确查某条剧情线：状态 + 时间字段"""
import json, re, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

ST = {0: "NotOpen", 1: "Close", 2: "Opened", 3: "Started", 4: "Complated", 5: "Failed", 6: "TimeOut", 7: "CloseAfterOpen"}
p = r"device-saves\Save1-20261022\Save1\SaveObjectStory.json"
raw = open(p, encoding="utf-8").read()
raw = re.sub(r'(?<=[{,])\s*(-?\d+)\s*:', lambda m: '"%s":' % m.group(1), raw)
v = json.loads(raw)["Data"]["value"]
gd = v["GraphDataList"]

prefixes = sys.argv[1:] or ["R10049", "DLC5"]
for pre in prefixes:
    keys = sorted(k for k in gd if k.startswith(pre))
    print("=" * 78)
    print(f"{pre}*  共 {len(keys)} 个")
    print("=" * 78)
    print(f"{'图':<18}{'状态':<14}{'Hold':<10}{'Last':<8}{'OpenedTime':<12}{'LimitQtrFrom':<14}{'TLsaveID':<10}{'TLqtrStart'}")
    for k in keys:
        d = gd[k]
        if not isinstance(d, dict):
            continue
        hold = v.get("GraphHoldList", {}).get(k, [])
        print(f"{k:<18}{ST.get(d.get('State'),'?'):<14}{str(hold):<10}{d.get('LastPlayTime',-1):<8}{d.get('OpenedTime',0):<12}{d.get('LimitQuaterFrom',0):<14}{d.get('TimeLimitSaveID',0):<10}{d.get('TimeLimitQuaterStart',0)}")
    print()

print("=" * 78)
print("全部 Failed / TimeOut")
print("=" * 78)
for k, d in gd.items():
    if isinstance(d, dict) and d.get("State") in (5, 6):
        print(" ", k, ST[d["State"]], json.dumps(d, ensure_ascii=False))
