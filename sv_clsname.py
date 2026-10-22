# -*- coding: utf-8 -*-
"""查 R10049-2 里各类节点的真实 C# 类名（MonoScript.m_ClassName）。

用法：把 story_assets_all.bundle 从 APK 解到 %TEMP%，再按 JSON 里的 m_Script pathID 反查。
"""
import json, sys, io, os, zipfile, tempfile
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
import UnityPy

APK = r"E:\笔记\wulin_v49-Hold按条件来源压制-2026-10-21.apk"
ENTRY = "assets/aa/Android/story_assets_all.bundle"
TMP = os.path.join(tempfile.gettempdir(), "svstory.bundle")

if not os.path.exists(TMP) or os.path.getsize(TMP) < 1000000:
    z = zipfile.ZipFile(APK)
    with open(TMP, "wb") as f:
        f.write(z.read(ENTRY))
print("bundle:", TMP, os.path.getsize(TMP), "字节")

G = json.load(open("patch-out/R10049-2.json", encoding="utf-8"))
NODES = G["nodes"]

# 1) 按节点名统计各自的 m_Script pathID
want = {}   # scriptPid -> set(节点名)
for pid, n in NODES.items():
    sp = n["m_Script"]["m_PathID"]
    want.setdefault(sp, set()).add(n["m_Name"])

print("\n=== 这张图用到的 script 类型 %d 种 ===" % len(want))
for sp, names in sorted(want.items(), key=lambda kv: -len(kv[1])):
    print("  scriptPid=%-22s 节点名: %s" % (sp, ", ".join(sorted(names))))

# 2) 在 bundle 里找这些 MonoScript
env = UnityPy.load(TMP)
want_set = {str(k) for k in want}
found = {}
for obj in env.objects:
    if obj.type.name != "MonoScript":
        continue
    if str(obj.path_id) not in want_set:
        continue
    try:
        t = obj.read_typetree()
    except Exception as e:
        print("  读失败", obj.path_id, e); continue
    found[str(obj.path_id)] = t

print("\n=== 反查结果 ===")
for sp in sorted(want, key=lambda k: -len(want[k])):
    t = found.get(str(sp))
    if not t:
        print("  scriptPid=%-22s -> bundle 里没找到 MonoScript" % sp); continue
    ns = t.get("m_Namespace") or ""
    cls = t.get("m_ClassName") or ""
    asm = t.get("m_AssemblyName") or ""
    print("  %-22s %-34s ns=%-22s asm=%s   节点名: %s"
          % (sp, cls, ns or "-", asm, ", ".join(sorted(want[sp]))))
