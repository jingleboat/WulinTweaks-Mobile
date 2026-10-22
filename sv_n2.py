# -*- coding: utf-8 -*-
"""打印指定节点的全部字段与上下游"""
import json, sys, io, collections
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

data = json.load(open(sys.argv[1], encoding="utf-8"))
SKIP = {"m_GameObject", "m_Enabled", "m_Script", "graph", "position", "ports",
        "isToNode", "isAutoAddNote", "taskLineType", "cabType", "redDot"}

targets = sys.argv[2:]
for gname, g in data.items():
    nodes = g["nodes"]
    # 建边
    out = collections.defaultdict(list)
    inn = collections.defaultdict(list)
    for pid, n in nodes.items():
        for v in (n.get("ports") or {}).get("values", []):
            d = v.get("_direction", 1)
            port = v.get("_fieldName")
            for c in v.get("connections", []):
                o = str(c["node"]["m_PathID"])
                if o == pid:
                    continue
                if d == 0:
                    inn[pid].append((port, o)); out[o].append((port, pid))
                else:
                    out[pid].append((port, o)); inn[o].append((port, pid))
    def desc(p):
        n = nodes.get(p)
        if not n:
            return f"<?{p}>"
        f = {k: v for k, v in n.items() if k not in SKIP and v not in (None, "", [], 0, False)}
        return n.get("m_Name", "?") + ("  " + str(f) if f else "")
    for t in targets:
        if t not in nodes:
            continue
        n = nodes[t]
        print("=" * 76)
        print(f"[{gname}] [{t}] {desc(t)}")
        print("  全部:", {k: v for k, v in n.items() if k not in SKIP})
        print(f"  上游 {len(inn[t])}:")
        for port, s in inn[t]:
            print(f"    <-[{port}] [{s}] {desc(s)}")
        print(f"  下游 {len(out[t])}:")
        for port, d in out[t]:
            print(f"    ->[{port}] [{d}] {desc(d)}")
