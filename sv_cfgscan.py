# -*- coding: utf-8 -*-
"""在未压缩的 UnityFS bundle 里按 utf16/utf8 扫描 key-value 对（用于提取本地化表内容）"""
import zipfile, sys, io, re
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

APK = r"E:\笔记\wulin_v49-Hold按条件来源压制-2026-10-21.apk"
BUNDLE = "assets/aa/Android/config_assets_all.bundle"

Z = zipfile.ZipFile(APK)
data = Z.read(BUNDLE)
print(f"bundle {len(data)} 字节\n")

def read_utf16_at(d, pos, maxlen=400):
    """从 pos 开始按 utf16-le 读可打印串（\x00 分隔）"""
    out = []
    i = pos
    while i + 1 < len(d) and len(out) < maxlen:
        ch = d[i] | (d[i + 1] << 8)
        if ch == 0:
            break
        if 32 <= ch < 0xFFFF and not (0xD800 <= ch <= 0xDFFF):
            out.append(chr(ch))
            i += 2
        else:
            break
    return "".join(out)

def read_utf8_at(d, pos, maxlen=400):
    out = []
    i = pos
    while i < len(d) and len(out) < maxlen:
        b = d[i]
        if b == 0:
            break
        if 32 <= b < 127 or b >= 0xC0:
            out.append(bytes([b]))
            i += 1
        else:
            break
    try:
        return b"".join(out).decode("utf-8", "replace")
    except Exception:
        return ""

kw = sys.argv[1] if len(sys.argv) > 1 else "DLC5Search"
nb = kw.encode("utf-16-le")

pos = 0
seen = set()
hits = 0
while True:
    i = data.find(nb, pos)
    if i < 0:
        break
    pos = i + 2
    hits += 1
    s = read_utf16_at(data, i)
    if s in seen:
        continue
    seen.add(s)
    print(f"[{i}] {s!r}")
    if hits > 400:
        print("...(截断)")
        break
print(f"\n共命中 {hits} 处，去重 {len(seen)} 个字符串")
