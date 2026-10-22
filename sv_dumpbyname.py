# -*- coding: utf-8 -*-
"""按图名从 story bundle 里 dump 完整节点 JSON（先按名字找 path_id）"""
import sys, io, os, tempfile, json
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
import UnityPy

tmp = os.path.join(tempfile.gettempdir(), "svstory.bundle")
env = UnityPy.load(tmp)

names = set(sys.argv[1:-1])
out_json = sys.argv[-1]

# 先建 path_id -> obj
objs = {}
for obj in env.objects:
    if obj.type.name == "MonoBehaviour":
        objs[obj.path_id] = obj

found = {}
for pid, obj in objs.items():
    try:
        t = obj.read_typetree()
    except Exception:
        continue
    nm = t.get("m_Name")
    if nm in names:
        found[nm] = (pid, t)
print("找到:", {k: v[0] for k, v in found.items()})

result = {}
for nm, (pid, gtree) in found.items():
    res = {"graph": nm, "meta": {k: v for k, v in gtree.items() if k != "nodes"}, "nodes": {}}
    for ref in gtree.get("nodes", []):
        npid = ref["m_PathID"]
        o = objs.get(npid)
        if o is None:
            continue
        try:
            res["nodes"][str(npid)] = o.read_typetree()
        except Exception as e:
            res["nodes"][str(npid)] = {"__error": str(e)}
    result[nm] = res
    print(f"{nm}: 节点 {len(res['nodes'])} 个")

open(out_json, "w", encoding="utf-8").write(json.dumps(result, ensure_ascii=False, indent=1, default=str))
print("已写出", out_json, os.path.getsize(out_json), "字节")
