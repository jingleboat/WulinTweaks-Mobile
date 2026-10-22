# -*- coding: utf-8 -*-
"""统计：出端口扇出、互补兄弟对、A/B 分类、汇合点真实入度。"""
import json, sys, io
from collections import Counter
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

G = json.load(open("patch-out/R10049-2.json", encoding="utf-8"))
NODES = G["nodes"]

def node(p): return NODES.get(str(p))
def nm(p):
    n = node(p); return n["m_Name"] if n else "?"
def ports(p):
    n = node(p)
    return {k: v for k, v in zip(n["ports"]["keys"], n["ports"]["values"])} if n else {}
def outs(p):
    """只取输出端口（_direction==1）的连接"""
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

# ---------- 1) 汇合点真实入度（只扫输出端口） ----------
indeg = Counter()
for k, n in NODES.items():
    for pk, v in zip(n["ports"]["keys"], n["ports"]["values"]):
        if v.get("_direction") != 1: continue
        for c in v.get("connections") or []:
            indeg[str(c["node"]["m_PathID"])] += 1

print("=== 入度 > 2 的节点（汇合点候选） ===")
for pid, d in indeg.most_common():
    if d <= 2: break
    print("  入度=%d  %s" % (d, label(pid)))

# ---------- 2) 出端口扇出 ----------
print("\n=== 同一输出端口扇出 >= 2 的节点（分支点） ===")
fan = []
for k, n in NODES.items():
    for pk, v in zip(n["ports"]["keys"], n["ports"]["values"]):
        if v.get("_direction") != 1: continue
        cs = v.get("connections") or []
        if len(cs) >= 2:
            fan.append((label(k), pk, [nm(c["node"]["m_PathID"]) + "(" + str(c["node"]["m_PathID"]) + ")"
                                       for c in cs]))
print("  共 %d 处" % len(fan))
for a, b, cs in fan:
    print("  %s .%s -> %s" % (a, b, cs))

# ---------- 3) 互补兄弟对 + A/B 分类 ----------
print("\n=== 互补兄弟对分类 ===")
def cond_of(gate):
    """流程判断 的 conditionPort 指向的条件节点"""
    for p, t in outs(gate):
        if p == "conditionPort":
            return t
    return None
def sig(c):
    n = node(c)
    if not n: return None
    f = {kk: n[kk] for kk in ("partnerID", "Negate", "Key") if kk in n}
    return (n["m_Name"], tuple(sorted(f.items())))

for k, n in NODES.items():
    for pk, v in zip(n["ports"]["keys"], n["ports"]["values"]):
        if v.get("_direction") != 1: continue
        cs = v.get("connections") or []
        if len(cs) != 2: continue
        a, b = cs[0]["node"]["m_PathID"], cs[1]["node"]["m_PathID"]
        if nm(a) != "流程判断" or nm(b) != "流程判断": continue
        ca, cb = cond_of(a), cond_of(b)
        if ca is None or cb is None: continue
        sa, sb = sig(ca), sig(cb)
        if not sa or not sb or sa[0] != sb[0]: continue
        da, db = dict(sa[1]), dict(sb[1])
        if da.get("partnerID") == db.get("partnerID") and da.get("Negate") != db.get("Negate"):
            ta = [nm(t) for p_, t in outs(a) if p_ == "OutPort"]
            tb = [nm(t) for p_, t in outs(b) if p_ == "OutPort"]
            ka = "播放剧情" if len(ta) == 1 and ta[0] == "播放剧情" else "流程判断"
            kb = "播放剧情" if len(tb) == 1 and tb[0] == "播放剧情" else "流程判断"
            kind = "A(一侧剧情/一侧直通)" if {ka, kb} == {"播放剧情", "流程判断"} else \
                   ("B(两侧都有剧情)" if ka == kb == "播放剧情" else "C(其它)")
            print("  父=%s" % label(k))
            print("     Neg=0: %s(%s) 条件=%s  -> %s" % (nm(a), a, sa[1], ta))
            print("     Neg=1: %s(%s) 条件=%s  -> %s" % (nm(b), b, sb[1], tb))
            print("     分类: %s" % kind)
