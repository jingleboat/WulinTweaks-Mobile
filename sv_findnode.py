# -*- coding: utf-8 -*-
"""按节点类型名查找节点，并打印其上下游 + 全字段"""
import json, sys, io, collections
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

data = json.load(open(sys.argv[1], encoding="utf-8"))
want = sys.argv[2]
SKIP = {"m_GameObject", "m_Enabled", "m_Script", "graph", "position", "ports",
        "isToNode", "isAutoAddNote", "taskLineType", "cabType", "redDot"}

for gname, g in data.items():
    nodes = g["nodes"]
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
    hits = [(pid, n) for pid, n in nodes.items() if n.get("m_Name") == want]
    if not hits:
        continue
    print("=" * 78)
    print(f"### {gname}: 找到 {len(hits)} 个 {want!r}")
    for pid, n in hits:
        print("-" * 78)
        print(f"[{pid}] 全字段:", {k: v for k, v in n.items() if k not in SKIP})
        print(f"  上游 {len(inn[pid])}:")
        for port, s in inn[pid][:8]:
            print(f"    <-[{port}] [{s}] {desc(s)}")
        print(f"  下游 {len(out[pid])}:")
        for port, d in out[pid][:8]:
            print(f"    ->[{port}] [{d}] {desc(d)}")
