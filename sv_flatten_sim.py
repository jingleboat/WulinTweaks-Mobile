# -*- coding: utf-8 -*-
"""R10049-2 分支展开：补丁表 + 静态验证（在内存里模拟改完之后的走法）。

补丁只用「把 connections 里某个 pid 换成另一个 pid」，等长替换。
门17 的条件字段单独一条（partnerID -> 0），用于「放宽关闭」时也不卡住。
"""
import json, sys, io, copy, collections
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

SRC = "patch-out/R10049-2.json"
G = json.load(open(SRC, encoding="utf-8"))
NODES = G["nodes"]

def node(p): return NODES.get(str(p))
def nm(p): return (node(p) or {}).get("m_Name", "?")
def ports(p):
    n = node(p)
    return {k: v for k, v in zip(n["ports"]["keys"], n["ports"]["values"])} if n else {}
def label(p):
    n = node(p)
    if not n: return "?"
    s = n["m_Name"]
    for f in ("ScenarioKey", "Key", "partnerID", "Negate", "SaveID"):
        if f in n and n[f] not in ("", None, {}):
            s += " %s=%s" % (f, n[f])
    return s

# ---------- 关键节点 pid（全部从数据里查出来，不靠记忆） ----------
P = {
    "_2_1":  2307069324650989666,
    "_2_2":  8624257868738185314,
    "_2_3":  -8542651908855117726,
    "_2_4":  -7796116142357559198,
    "_2_5":  -5177210677316171678,
    "_2_6":  440128225061292130,
    "_2_9":  -7993697421332395934,
    "gate8":  -4393441399259346846,   # 10084 Neg=0
    "gate9":  1119091300528998498,    # 10084 Neg=1
    "gate10": -3417478748039952286,   # 10083 Neg=0
    "gate11": 3022033317146380386,    # 10083 Neg=1
    "gate12": -8903986025046947742,   # 10078 Neg=0
    "gate13": -4587094667565807518,   # 10078 Neg=1
    "gate14": 2656936388345514082,    # 10053 Neg=0
    "gate15": 85695644987344994,      # 10053 Neg=1
    "gate16": 8516201082422806626,    # 10008 Neg=0
    "gate17": -8366127586497607582,   # 10008 Neg=1  <- 要中性化
    "gate18": 2261853592221829218,    # 20204 在场
    "gate19": -5564159756232243102,   # 20204 不在场
    "_2_7_1": -2577396185123075998,
    "_2_7_2": -2332169008493049758,
    "_2_8":   8009484742017402978,
    "gate20": -7742445857939863454,   # R10009-2-1 已完成
    "gate21": -2987537857865070494,   # R10009-2-1 未完成
    "cond17": 2637197178812156002,    # 需要伙伴 10008 Neg=1
}

# 校验 pid 都在
bad = [k for k, v in P.items() if node(v) is None]
assert not bad, "pid 不存在: %s" % bad

# ---------- 补丁表 ----------
# 每条: (节点, 输出端口, 原目标 pid, 新目标 pid)
PATCH_CONN = [
    ("_2_2", "OutPort", P["_2_9"], P["gate10"]),
    ("_2_3", "OutPort", P["_2_9"], P["gate12"]),
    ("_2_4", "OutPort", P["_2_9"], P["gate14"]),
    ("_2_5", "OutPort", P["_2_9"], P["gate16"]),
    ("_2_6", "OutPort", P["_2_9"], P["gate17"]),
]
PATCH_FIELD = [("cond17", "partnerID", 10008, 0)]

print("=== 补丁表（%d 条连边 + %d 条字段，全部等长） ===" % (len(PATCH_CONN), len(PATCH_FIELD)))
for a, port, old, new in PATCH_CONN:
    print("  %-6s .%-8s  %s -> %s" % (a, port, label(old), label(new)))
for a, f, old, new in PATCH_FIELD:
    print("  %-6s .%-8s  %s -> %s" % (a, f, old, new))

# ---------- 校验：原结构里这些边确实存在 ----------
print("\n=== 前置核对：补丁要改的边都存在 ===")
ok = True
for a, port, old, new in PATCH_CONN:
    n = node(P[a])
    lst = ports(P[a]).get(port)
    targets = [c["node"]["m_PathID"] for c in (lst.get("connections") or [])]
    hit = old in targets
    print("  %-6s .%-8s 当前目标=%s  含 %s : %s" %
          (a, port, [label(t) for t in targets], label(old), "✔" if hit else "✘"))
    ok &= hit
print("  结论:", "全部匹配" if ok else "有边不匹配，停止")

# ---------- 在内存里应用补丁，然后模拟走图 ----------
H = copy.deepcopy(G)
HN = H["nodes"]

def hports(p):
    n = HN.get(str(p))
    return {k: v for k, v in zip(n["ports"]["keys"], n["ports"]["values"])} if n else {}

for a, port, old, new in PATCH_CONN:
    lst = hports(P[a])[port]
    for c in lst.get("connections") or []:
        if c["node"]["m_PathID"] == old:
            c["node"]["m_PathID"] = new
for a, f, old, new in PATCH_FIELD:
    HN[str(P[a])][f] = new

def hout(p):
    r = []
    for k, v in hports(p).items():
        if v.get("_direction") != 1: continue
        for c in v.get("connections") or []:
            r.append((k, c["node"]["m_PathID"]))
    return r

print("\n=== 改完之后：_2_1 起沿「假设门全通」的走法 ===")
def walk(pid, seen, depth=0):
    if depth > 26: print("  " * depth + "…(深度截断)"); return
    mark = "" if str(pid) not in seen else "   ★已走过"
    print("  " * depth + "%s  [%s]" % (label(pid), pid))
    if mark: return
    seen.add(str(pid))
    # 只跟 OutPort；分叉时两条都跟
    outs = [(k, t) for k, t in hout(pid) if k == "OutPort"]
    for k, t in outs:
        walk(t, seen, depth + 1)

seen = set()
walk(P["_2_1"], seen)

# ---------- 可达性检查 ----------
print("\n=== 可达性 ===")
reach = set()
def rch(pid):
    if str(pid) in reach: return
    reach.add(str(pid))
    for k, t in hout(pid):
        rch(t)
rch(P["_2_1"])
must = ["_2_2", "_2_3", "_2_4", "_2_5", "_2_6", "gate17", "gate18", "gate19",
        "_2_7_1", "_2_7_2", "_2_8", "_2_9", "gate20", "gate21"]
for k in must:
    print("  %-8s 可达: %s" % (k, "✔" if str(P[k]) in reach else "✘"))
for k in ["gate8", "gate9", "gate10", "gate11", "gate12", "gate13", "gate14", "gate15", "gate16"]:
    print("  %-8s 变孤立: %s" % (k, "✔" if str(P[k]) not in reach else "✘（仍可达）"))

# 汇合点入度
def indeg(pid, nodes):
    c = 0
    for k, n in nodes.items():
        for pk, v in zip(n["ports"]["keys"], n["ports"]["values"]):
            if v.get("_direction") != 1: continue
            for cc in v.get("connections") or []:
                if str(cc["node"]["m_PathID"]) == str(pid): c += 1
    return c
print("\n=== 汇合点 _2_9 入度 ===")
print("  改前: %d   改后: %d" % (indeg(P["_2_9"], NODES), indeg(P["_2_9"], HN)))
