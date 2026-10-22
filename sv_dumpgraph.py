# -*- coding: utf-8 -*-
"""把指定剧情图的全部节点解析成 JSON 并落盘"""
import sys, io, os, tempfile, json
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
import UnityPy

tmp = os.path.join(tempfile.gettempdir(), "svstory.bundle")
graph_pid = int(sys.argv[1])
out_json = sys.argv[2]

env = UnityPy.load(tmp)

# 建 path_id -> obj 索引（只索引 MonoBehaviour）
idx = {}
for obj in env.objects:
    if obj.type.name == "MonoBehaviour":
        idx[obj.path_id] = obj
print("MonoBehaviour 索引", len(idx), "个")

root = idx[graph_pid]
gtree = root.read_typetree()
print("图:", gtree.get("m_Name"), "节点数", len(gtree.get("nodes", [])))
print("headnode", gtree.get("headnode"), "opennode", gtree.get("opennode"), "startnode", gtree.get("startnode"))

result = {"graph": gtree.get("m_Name"), "meta": {k: v for k, v in gtree.items() if k != "nodes"}, "nodes": {}}
missing = []
for ref in gtree.get("nodes", []):
    pid = ref["m_PathID"]
    obj = idx.get(pid)
    if obj is None:
        missing.append(pid)
        continue
    try:
        t = obj.read_typetree()
    except Exception as e:
        result["nodes"][str(pid)] = {"__error": str(e)}
        continue
    result["nodes"][str(pid)] = t

print("解析成功", len(result["nodes"]), "个，缺失", len(missing), "个")
if missing:
    print("缺失 path_id:", missing[:10])
open(out_json, "w", encoding="utf-8").write(json.dumps(result, ensure_ascii=False, indent=1, default=str))
print("已写出", out_json, os.path.getsize(out_json), "字节")
