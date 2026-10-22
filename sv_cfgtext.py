# -*- coding: utf-8 -*-
"""把 UnityFS bundle 按 utf16 整体解码（两种字节偏移），正则抽取中文串，定位关键词及其邻近 key"""
import zipfile, sys, io, re
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

APK = r"E:\笔记\wulin_v49-Hold按条件来源压制-2026-10-21.apk"
BUNDLE = "assets/aa/Android/config_assets_all.bundle"
Z = zipfile.ZipFile(APK)
data = Z.read(BUNDLE)
print(f"bundle {len(data)} 字节\n", file=sys.stderr)

kws = sys.argv[1:] or ["朱文"]
CJK = r"\u4e00-\u9fff"
pat = re.compile(rf"[{CJK}0-9A-Za-z_\-，。；：？！、·（）()<>#%/]{{3,200}}")

def scan(off):
    s = data[off:].decode("utf-16-le", errors="replace")
    out = []
    for m in pat.finditer(s):
        t = m.group()
        if re.search(rf"[{CJK}]", t):
            out.append((m.start(), t))
    return out

for off in (0, 1):
    hits = scan(off)
    print(f"=== 偏移 {off}：抽出中文串 {len(hits)} 条 ===", file=sys.stderr)
    for kw in kws:
        idxs = [(i, t) for i, t in hits if kw in t]
        print(f"\n#### 关键词 {kw!r} 命中 {len(idxs)} 条（off={off}）")
        for n, (pos, t) in enumerate(idxs[:20]):
            print(f"  --- #{n} pos={pos} ---")
            # 打印前后各 6 条，找 key
            for j in range(max(0, [x for x, _ in hits].index(pos) - 6), min(len(hits), [x for x, _ in hits].index(pos) + 3)):
                p2, t2 = hits[j]
                mark = " <<<" if p2 == pos else ""
                print(f"     [{p2}] {t2!r}{mark}")
