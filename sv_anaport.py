# -*- coding: utf-8 -*-
"""分析剧情图 JSON：为指定节点打印完整字段 + 上下游（按端口 _direction 判定方向）"""
import json, sys, io, collections
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

g = json.load(open(sys.argv[1], encoding="utf-8"))
nodes = g["nodes"]

# 建边：ports 是 dict 还是 list?
def get_ports(n):
    p = n.get("ports")
    return p

# 先看一个节点的 ports 结构
sample = next(iter(nodes.values()))
print("样例节点键:", list(sample.keys()))
print("ports 类型:", type(sample.get("ports")).__name__)
p0 = sample.get("ports")
if isinstance(p0, dict):
    print("ports 键:", list(p0.keys()))
    print(json.dumps(p0, ensure_ascii=False, default=str)[:1200])
elif isinstance(p0, list):
    print(json.dumps(p0[:2], ensure_ascii=False, default=str)[:1200])
