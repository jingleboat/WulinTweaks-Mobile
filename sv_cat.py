# -*- coding: utf-8 -*-
"""查 catalog.json 里 story_assets_all.bundle 的记录（hash/crc/依赖），评估改 bundle 的可行性"""
import zipfile, sys, io, re, json
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

APK = r"E:\笔记\wulin_v49-Hold按条件来源压制-2026-10-21.apk"
Z = zipfile.ZipFile(APK)
raw = Z.read("assets/aa/catalog.json")
txt = raw.decode("utf-8", "replace")
print("catalog 大小:", len(txt))

for kw in ["story_assets_all", "scenario_assets_statemachine", "config_assets_all"]:
    idx = [m.start() for m in re.finditer(re.escape(kw), txt)]
    print(f"\n=== {kw!r} 命中 {len(idx)} 处 ===")
    for i in idx[:6]:
        s = txt.rfind('"', 0, i)
        e = txt.find('"', i)
        print("   ", txt[max(0, s - 120):e + 120].replace("\n", " ")[:260])
