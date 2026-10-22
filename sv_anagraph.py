# -*- coding: utf-8 -*-
"""分析导出的剧情图 JSON：节点类型统计、按 partnerID 找条件、找指定路径"""
import json, sys, io, collections
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

p = sys.argv[1]
g = json.load(open(p, encoding="utf-8"))
nodes = g["nodes"]
print("图:", g["graph"], "节点", len(nodes))

names = collections.Counter()
for pid, n in nodes.items():
    names[n.get("m_Name", "?")] += 1
print("\n节点类型：")
for k, v in names.most_common(60):
    print(f"  {v:>4}  {k}")

# 找含指定字段的节点
KEYF = ["partnerID", "Negate", "Key", "Tag", "SaveID", "DoResult", "TotalMap", "areaString", "isPrisoner", "needShowBubble"]
print("\n含条件的节点（有 partnerID 或 Key 或 Tag）：")
for pid, n in nodes.items():
    f = {k: n[k] for k in KEYF if k in n and n[k] not in (None, "", [], 0)}
    if "partnerID" in n or "Key" in n or "Tag" in n:
        print(f"  [{pid}] {n.get('m_Name')}  {f}")
