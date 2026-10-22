# -*- coding: utf-8 -*-
"""剧情图 JSON 分析：建边，打印指定节点的字段与上下游链"""
import json, sys, io, collections
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

g = json.load(open(sys.argv[1], encoding="utf-8"))
nodes = g["nodes"]
out = collections.defaultdict(list)   # pid -> [(port, dst)]
inn = collections.defaultdict(list)   # pid -> [(port, src)]

for pid, n in nodes.items():
    for v in (n.get("ports") or {}).get("values", []):
        d = v.get("_direction", 1)
        port = v.get("_fieldName")
        for c in v.get("connections", []):
            other = str(c["node"]["m_PathID"])
            if other == pid:
                continue
            if d == 0:
                inn[pid].append((port, other))
                out[other].append((port, pid))
            else:
                out[pid].append((port, other))
                inn[other].append((port, pid))

SKIP = {"m_GameObject", "m_Enabled", "m_Script", "graph", "position", "ports",
        "isToNode", "isAutoAddNote", "taskLineType", "cabType", "redDot"}

def desc(pid):
    n = nodes.get(pid)
    if not n:
        return f"<?{pid}>"
    f = {k: v for k, v in n.items() if k not in SKIP and v not in (None, "", [], 0, False)}
    s = n.get("m_Name", "?")
    if f:
        s += "  " + str(f)
    return s

def show(pid, depth=1):
    n = nodes.get(pid)
    print("=" * 78)
    print(f"[{pid}] {desc(pid)}")
    if n:
        print("   全部字段:", {k: v for k, v in n.items() if k not in SKIP})
    print(f"   上游 {len(inn[pid])}:")
    for port, src in inn[pid]:
        print(f"      ←[{port}] [{src}] {desc(src)}")
    print(f"   下游 {len(out[pid])}:")
    for port, dst in out[pid]:
        print(f"      →[{port}] [{dst}] {desc(dst)}")

for pid in sys.argv[2:]:
    show(pid)
