# -*- coding: utf-8 -*-
"""打印 SaveID 74~82 附近的局部结构（_2_6_* 那条链）。"""
import json, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

G = json.load(open("patch-out/R10049-2.json", encoding="utf-8"))
NODES = G["nodes"]

def node(p): return NODES.get(str(p))
def nm(p): return (node(p) or {}).get("m_Name", "?")
def ports(p):
    n = node(p)
    return {k: v for k, v in zip(n["ports"]["keys"], n["ports"]["values"])} if n else {}
def outs(p):
    r = []
    for k, v in ports(p).items():
        if v.get("_direction") != 1: continue
        for c in v.get("connections") or []:
            r.append((k, c["node"]["m_PathID"]))
    return r
def label(p):
    n = node(p)
    if not n: return "?"
    s = n["m_Name"]
    for f in ("ScenarioKey", "Key", "partnerID", "Negate", "SaveID"):
        if f in n and n[f] not in ("", None, {}):
            s += " %s=%s" % (f, n[f])
    return s
def indeg(p):
    c = 0
    for k, n in NODES.items():
        for pk, v in zip(n["ports"]["keys"], n["ports"]["values"]):
            if v.get("_direction") != 1: continue
            for cc in v.get("connections") or []:
                if str(cc["node"]["m_PathID"]) == str(p): c += 1
    return c

# 找出 ScenarioKey 在 _2_6_x 的节点 + SaveID 74..82
sel = []
for k, n in NODES.items():
    sk = n.get("ScenarioKey") or ""
    sid = n.get("SaveID", 0)
    if sk.startswith("R10049-2_2_6") or sk.startswith("R10049-2_6_") or 72 <= sid <= 84:
        sel.append(k)

seen = set()
for k in sel:
    if k in seen: continue
    seen.add(k)
    print("%s  pid=%s  入度=%d" % (label(k), k, indeg(k)))
    for pk, t in outs(k):
        print("     .%s -> %s  (入度=%d)" % (pk, label(t), indeg(t)))
    print()
