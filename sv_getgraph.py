# -*- coding: utf-8 -*-
"""提取指定剧情图（MonoBehaviour）的完整 typetree 结构"""
import sys, io, os, tempfile, json
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
import UnityPy

tmp = os.path.join(tempfile.gettempdir(), "svstory.bundle")
env = UnityPy.load(tmp)

target_pid = int(sys.argv[1]) if len(sys.argv) > 1 else 6670633184913863344
out_json = sys.argv[2] if len(sys.argv) > 2 else ""

for obj in env.objects:
    if obj.path_id != target_pid:
        continue
    print("找到对象", obj.type.name, obj.path_id)
    try:
        tree = obj.read_typetree()
        print("typetree 读取成功，顶层键：", list(tree.keys())[:20])
        if out_json:
            open(out_json, "w", encoding="utf-8").write(json.dumps(tree, ensure_ascii=False, indent=1, default=str))
            print("已写出", out_json)
        else:
            print(json.dumps(tree, ensure_ascii=False, indent=1, default=str)[:4000])
    except Exception as e:
        print("typetree 失败:", e)
        raw = obj.get_raw_data()
        print("raw 长度", len(raw))
        print("前 400 字节:", raw[:400])
    break
