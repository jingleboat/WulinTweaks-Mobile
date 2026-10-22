# -*- coding: utf-8 -*-
"""按 fileID 打印剧情图节点详情"""
import sys, io
sys.path.insert(0, ".")
from sv_graph_full import load
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

g = sys.argv[1]
bl = load(g)
for fid in sys.argv[2:]:
    b = bl.get(fid)
    if not b:
        print("找不到", fid)
        continue
    print("=" * 76)
    print("节点", fid, b.name, b.f)
    print("上游:")
    for p, s in b.inn:
        sb = bl.get(s)
        print(f"    [{p}]<- [{s}] {sb.name if sb else '?'} {sb.f if sb else ''}")
    print("下游:")
    for p, d in b.out:
        db = bl.get(d)
        print(f"    [{p}]-> [{d}] {db.name if db else '?'} {db.f if db else ''}")
    print()
