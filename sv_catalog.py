# -*- coding: utf-8 -*-
"""在 APK 的 Addressables catalog.json 里搜关键词，列出命中的 address 与所在 bundle"""
import zipfile, sys, io, re, json
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

APK = r"E:\笔记\wulin_v49-Hold按条件来源压制-2026-10-21.apk"
Z = zipfile.ZipFile(APK)
raw = Z.read("assets/aa/catalog.json")
print(f"catalog.json {len(raw)} 字节，头部: {raw[:80]}\n")

txt = raw.decode("utf-8", "replace")
kws = sys.argv[1:] or ["DLC5Search"]
for kw in kws:
    idxs = [m.start() for m in re.finditer(re.escape(kw), txt)]
    print("=" * 70)
    print(f"{kw!r} 命中 {len(idxs)} 处")
    seen = set()
    for i in idxs:
        # 抓取包含它的 JSON 字符串值
        s = txt.rfind('"', 0, i)
        e = txt.find('"', i)
        frag = txt[max(0, s - 200):e + 200]
        key = txt[s:e + 1]
        if key in seen:
            continue
        seen.add(key)
        print(f"  [{i}] {key}")
    print(f"  去重 {len(seen)} 个字符串")
