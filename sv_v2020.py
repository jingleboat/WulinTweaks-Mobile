# -*- coding: utf-8 -*-
"""分析多图 JSON：节点类型统计 + 队友条件 + 关键节点"""
import json, sys, io, collections
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

data = json.load(open(sys.argv[1], encoding="utf-8"))

for gname, g in data.items():
    nodes = g["nodes"]
    print("=" * 76)
    print(f"### {gname}  节点 {len(nodes)}")
    names = collections.Counter(n.get("m_Name", "?") for n in nodes.values())
    print("  类型:", dict(names.most_common(40)))
    # 队友相关
    team = []
    for pid, n in nodes.items():
        if n.get("m_Name") in ("需要伙伴", "需要Npc为临时名", "伙伴邀请或离队", "有伙伴"):
            team.append((pid, n.get("m_Name"), n.get("partnerID"), n.get("Negate"), n.get("state"), n.get("SaveID")))
    if team:
        print("  队友相关节点:")
        for t in team:
            print(f"    {t}")
    print()
