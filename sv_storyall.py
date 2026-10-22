# -*- coding: utf-8 -*-
"""解析 story_assets_all.bundle，列出所有剧情图名（MonoBehaviour），定位 EJ-* / DLC5Search*"""
import zipfile, sys, io, os, tempfile, collections
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

import UnityPy

APK = r"E:\笔记\wulin_v49-Hold按条件来源压制-2026-10-21.apk"
Z = zipfile.ZipFile(APK)
target = "assets/aa/Android/story_assets_all.bundle"
size = Z.getinfo(target).file_size
print("bundle 大小:", size)
tmp = os.path.join(tempfile.gettempdir(), "svstory.bundle")
if not os.path.exists(tmp) or os.path.getsize(tmp) != size:
    with open(tmp, "wb") as f:
        f.write(Z.read(target))
    print("已落盘", tmp)

env = UnityPy.load(tmp)
cnt = collections.Counter()
graphs = []
for obj in env.objects:
    cnt[obj.type.name] += 1
    if obj.type.name in ("MonoBehaviour", "AssetBundle", "TextAsset"):
        try:
            d = obj.read()
            nm = getattr(d, "m_Name", None) or getattr(d, "name", None)
        except Exception:
            nm = None
        if nm:
            graphs.append((obj.type.name, str(nm), obj.path_id))

print("\n对象类型：")
for k, v in cnt.most_common(20):
    print(f"  {k:<22}{v}")

print(f"\n命名对象 {len(graphs)} 个")
want_pref = ("EJ-", "DLC5", "M20100-22", "R10049")
hit = [g for g in graphs if g[1].startswith(want_pref)]
print(f"命中目标前缀 {len(hit)} 个：")
for t, n, pid in hit[:80]:
    print(f"  {t:<14}{n:<26}path_id={pid}")

print("\n全部图名（前 40 个，按名字排序）：")
for t, n, pid in sorted(graphs, key=lambda x: x[1])[:40]:
    print(f"  {n}")
