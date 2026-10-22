# -*- coding: utf-8 -*-
"""解析 config_assets_all.bundle：统计对象类型，找 EJ-* / DLC5* / StateMachine 图"""
import zipfile, sys, io, os, tempfile, collections
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

import UnityPy

APK = r"E:\笔记\wulin_v49-Hold按条件来源压制-2026-10-21.apk"
Z = zipfile.ZipFile(APK)
target = "assets/aa/Android/config_assets_all.bundle"
tmp = os.path.join(tempfile.gettempdir(), "svcfg.bundle")
if not os.path.exists(tmp) or os.path.getsize(tmp) != Z.getinfo(target).file_size:
    with open(tmp, "wb") as f:
        f.write(Z.read(target))
print("bundle:", os.path.getsize(tmp), "字节")

env = UnityPy.load(tmp)
cnt = collections.Counter()
hits = []
allnames = 0
for obj in env.objects:
    t = obj.type.name
    cnt[t] += 1
    if t in ("TextAsset", "MonoBehaviour", "AssetBundle"):
        try:
            d = obj.read()
            nm = getattr(d, "m_Name", None) or getattr(d, "name", None)
        except Exception:
            nm = None
        if nm:
            allnames += 1
            if nm.startswith(("EJ-", "DLC5", "M20100-22")) or "大梁" in str(nm):
                hits.append((t, nm, obj.path_id))

print("\n对象类型统计：")
for k, v in cnt.most_common(40):
    print(f"  {k:<22}{v}")
print(f"\n拿到名字的对象 {allnames} 个")
print(f"命中 EJ-/DLC5 前缀的 {len(hits)} 个：")
for t, n, pid in hits[:60]:
    print(f"  {t:<16}{n:<28}path_id={pid}")
