# -*- coding: utf-8 -*-
"""在 config bundle 里精确定位某 utf16 字符串，并向前后 dump 可读文本与邻近 key"""
import zipfile, sys, io, re
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

APK = r"E:\笔记\wulin_v49-Hold按条件来源压制-2026-10-21.apk"
Z = zipfile.ZipFile(APK)
d = Z.read("assets/aa/Android/config_assets_all.bundle")

kw = sys.argv[1]
span = int(sys.argv[2]) if len(sys.argv) > 2 else 900
needle = kw.encode("utf-16-le")
pos = 0
n = 0
while True:
    i = d.find(needle, pos)
    if i < 0:
        break
    pos = i + 2
    n += 1
    print("=" * 78)
    print(f"命中 #{n}  @ {i}")
    print("=" * 78)
    lo, hi = max(0, i - span), min(len(d), i + span)
    # 两种对齐各解一遍
    for off in (0, 1):
        s = d[lo + off:hi + off].decode("utf-16-le", errors="replace")
        # 只看含中文或 key 形态的片段
        frags = re.findall(r"[\u4e00-\u9fff\u3000-\u303fA-Za-z0-9_\-/\.，。；：？！、（）()<>#%]{4,400}", s)
        good = [f for f in frags if re.search(r"[\u4e00-\u9fff]", f) or re.match(r"^[A-Za-z]", f)]
        if not good:
            continue
        print(f"\n--- 对齐 off={off} ---")
        for f in good:
            mark = "   <<<<<" if kw in f else ""
            print(f"   {f!r}{mark}")
    if n >= 3:
        print("...(只显示前 3 处)")
        break
