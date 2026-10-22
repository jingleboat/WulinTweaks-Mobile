# -*- coding: utf-8 -*-
"""列出 R10049-2 里所有 播放剧情 节点及其 ScenarioKey，以及和它相邻的 流程判断/需要伙伴"""
import json, sys, io, collections
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

g = json.load(open(sys.argv[1], encoding="utf-8"))
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

def brief(p):
    n = nodes.get(p)
    if not n:
        return f"<?{p}>"
    nm = n.get("m_Name", "?")
    extra = []
    for k in ("ScenarioKey", "partnerID", "Negate", "Tag", "SaveID", "Key"):
        if k in n and n[k] not in (None, "", 0, False):
            extra.append(f"{k}={n[k]}")
    return f"{nm}({', '.join(extra)})" if extra else nm

print("=== 所有 播放剧情 节点（按 ScenarioKey 排序）===")
rows = [(n.get("ScenarioKey", ""), pid, n) for pid, n in nodes.items() if n.get("m_Name") == "播放剧情"]
for key, pid, n in sorted(rows):
    ups = [brief(s) for p, s in inn[pid] if p == "InPort"]
    print(f"  {key:<22} [{pid}]")
    if ups:
        print(f"      上游: {ups}")

print()
print("=== 所有 流程判断 的 conditionPort 指向 ===")
for pid, n in sorted(nodes.items(), key=lambda x: x[1].get("SaveID", 999)):
    if n.get("m_Name") != "流程判断":
        continue
    conds = [brief(d) for p, d in out[pid] if p == "conditionPort"]
    downs = [brief(d) for p, d in out[pid] if p in ("OutPort",)]
    print(f"  SaveID={n.get('SaveID')} [{pid}]  Condition: {conds}")
    if downs:
        print(f"      出口: {downs[:4]}")
