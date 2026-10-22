# -*- coding: utf-8 -*-
"""列出 R10049-2 里所有 A 类（一侧剧情 / 一侧直通）互补兄弟对，供展开方案使用。"""
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
def cond_of(g):
    for p, t in outs(g):
        if p == "conditionPort": return t
def sig(c):
    n = node(c)
    if not n: return None
    f = {kk: n[kk] for kk in ("partnerID", "Negate", "Key") if kk in n}
    return (n["m_Name"], tuple(sorted(f.items())))

A = []
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
        if da.get("partnerID") != db.get("partnerID") or da.get("Negate") == db.get("Negate"):
            continue
        ta = [t for p_, t in outs(a) if p_ == "OutPort"]
        tb = [t for p_, t in outs(b) if p_ == "OutPort"]
        ka = "剧情" if len(ta) == 1 and nm(ta[0]) == "播放剧情" else "直通"
        kb = "剧情" if len(tb) == 1 and nm(tb[0]) == "播放剧情" else "直通"
        if {ka, kb} != {"剧情", "直通"}: continue
        # 哪个 Neg 侧是剧情
        zero = a if da.get("Negate") == 0 else b
        one = b if zero == a else a
        czero = ca if zero == a else cb
        A.append(dict(parent=k, cond=sig(czero)[1], zero=zero, one=one,
                      zero_out=[t for p_, t in outs(zero) if p_ == "OutPort"],
                      one_out=[t for p_, t in outs(one) if p_ == "OutPort"]))

print("=== A 类（一侧剧情 / 一侧直通）共 %d 处 ===" % len(A))
for d in A:
    print("父: %-40s 条件 %s" % (label(d["parent"]), d["cond"]))
    print("   Neg=0 门 %-22s -> %s" % (d["zero"], [label(t) for t in d["zero_out"]]))
    print("   Neg=1 门 %-22s -> %s" % (d["one"], [label(t) for t in d["one_out"]]))
