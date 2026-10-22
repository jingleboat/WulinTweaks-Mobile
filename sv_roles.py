# -*- coding: utf-8 -*-
"""列出 R10049-2 里涉及指定角色 id 的条件节点"""
import json, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

g = json.load(open(sys.argv[1], encoding="utf-8"))
nodes = g["nodes"]
ids = {int(x.split(":")[0]): x.split(":")[1] for x in sys.argv[2:]}

print(f"=== {g['graph']} 里涉及 {list(ids.values())} 的节点 ===")
hit = 0
for pid, n in nodes.items():
    p = n.get("partnerID")
    if p in ids:
        hit += 1
        print(f"  [{pid}] {n.get('m_Name'):<18} partnerID={p}({ids[p]}) Negate={n.get('Negate')} SaveID={n.get('SaveID')}")
print(f"共 {hit} 个")
