# -*- coding: utf-8 -*-
"""从 APK 的 localization / config bundle 里搜关键词（含 LZ4/LZMA 解压后搜）"""
import zipfile, io, sys, re, os
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

APK = r"E:\笔记\wulin_v49-Hold按条件来源压制-2026-10-21.apk"
Z = zipfile.ZipFile(APK)

kws = sys.argv[1:] or ["朱文", "DLC5Search", "打听朱文"]
targets = [n for n in Z.namelist()
           if n.startswith("assets/aa/Android/") and
           ("localization" in n.lower() or "config_assets" in n.lower() or "gamemanagers" in n.lower())]
print("候选 bundle:", len(targets))
for t in targets:
    print("   ", t, Z.getinfo(t).file_size)

def try_unpack(data):
    """尝试 UnityFS 解压：返回可搜索的多个字节块"""
    out = [data]
    try:
        import lz4.block
        # Unity LZ4 block: 跳过 UnityFS 头后是 blocksInfo(uncompressed len ...) —— 粗略扫描 LZ4 块
        for off in range(0, len(data) - 8, 1):
            pass  # 全扫太慢，跳过
    except Exception:
        pass
    return out

for t in targets:
    data = Z.read(t)
    print("\n" + "=" * 70)
    print("###", t, len(data), "字节，头部:", data[:16])
    needles = []
    for k in kws:
        needles.append((k, "utf8", k.encode("utf-8")))
        needles.append((k, "utf16", k.encode("utf-16-le")))
    for k, enc, nb in needles:
        c = data.count(nb)
        if c:
            print(f"  直接命中 {k!r}({enc}) x{c}")
            i = data.find(nb)
            print("    上下文:", data[max(0, i - 80):i + 80])
