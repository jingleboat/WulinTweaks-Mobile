# -*- coding: utf-8 -*-
"""对比多个剧情图的 meta（_init / checkDlc / head-opennode），找"被加载 vs 不被加载"的差异"""
import sys, io, os, tempfile, json
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
import UnityPy

tmp = os.path.join(tempfile.gettempdir(), "svstory.bundle")
env = UnityPy.load(tmp)

want = set(sys.argv[1:]) or {"R10049-2", "R10049-2-1"}
idx = {}
for obj in env.objects:
    if obj.type.name != "MonoBehaviour":
        continue
    try:
        t = obj.read_typetree()
    except Exception:
        continue
    nm = t.get("m_Name")
    if nm in want:
        idx[nm] = t

keys = ["m_Enabled", "_init", "checkDlc"]
for nm in sorted(idx):
    t = idx[nm]
    head = t.get("headnode", {}).get("m_PathID")
    openn = t.get("opennode", {}).get("m_PathID")
    start = t.get("startnode", {}).get("m_PathID")
    extra = {k: t.get(k) for k in t if k not in ("m_GameObject", "m_Script", "nodes", "ports")}
    print("=" * 70)
    print(nm)
    print("  m_Enabled=", t.get("m_Enabled"), " _init=", t.get("_init"), " checkDlc=", t.get("checkDlc"))
    print("  headnode=", head, " opennode=", openn, " startnode=", start)
    print("  节点数=", len(t.get("nodes", [])))
    print("  其它字段:", {k: v for k, v in extra.items() if k not in ("m_Name", "headnode", "opennode", "startnode", "m_Enabled", "_init", "checkDlc")})
