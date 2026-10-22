# -*- coding: utf-8 -*-
"""用 UnityPy 列出一个 bundle 内的资源类型与名称统计"""
import zipfile, sys, io, os, tempfile, collections
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

import UnityPy

APK = r"E:\笔记\wulin_v49-Hold按条件来源压制-2026-10-21.apk"
Z = zipfile.ZipFile(APK)
target = sys.argv[1]
tmp = os.path.join(tempfile.gettempdir(), "svb.bundle")
with open(tmp, "wb") as f:
    f.write(Z.read(target))
print("落盘", tmp, os.path.getsize(tmp), "字节")

env = UnityPy.load(tmp)
cnt = collections.Counter()
names = []
for obj in env.objects:
    cnt[obj.type.name] += 1
    try:
        if obj.type.name in ("TextAsset", "MonoBehaviour", "AssetBundle"):
            d = obj.read()
            nm = getattr(d, "m_Name", None) or getattr(d, "name", None)
            if nm:
                names.append((obj.type.name, nm))
    except Exception:
        pass

print("\n对象类型统计：")
for k, v in cnt.most_common(30):
    print(f"  {k:<24}{v}")

print(f"\n可读名字 {len(names)} 个，前 40：")
for t, n in names[:40]:
    print(f"  {t:<16}{n}")
