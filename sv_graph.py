# -*- coding: utf-8 -*-
"""从 ModEditor.zip 里提取指定剧情图的 asset 原文，并列出其中的条件节点"""
import zipfile, re, sys, io, os

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
ZIP = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor.zip"

targets = sys.argv[1:] or ["R10049-0"]
z = zipfile.ZipFile(ZIP)
names = [n for n in z.namelist() if "/GameRes/Story/" in n and n.endswith(".asset")]

def unesc(s):
    if "\\u" in s:
        try:
            return s.encode().decode("unicode_escape")
        except Exception:
            return s
    return s

for t in targets:
    hits = []
    for n in names:
        if t in os.path.basename(n):
            hits.append(n)
    print("=" * 78)
    print(f"目标 {t!r} 文件名命中 {len(hits)}:")
    for h in hits:
        print("   ", h)

    # 再按内容里的 m_Name 命中
    if not hits:
        for n in names:
            d = z.read(n)
            if t.encode() in d:
                hits.append(n)
        print(f"  内容命中 {len(hits)}:")
        for h in hits[:20]:
            print("   ", h)

    for h in hits[:2]:
        txt = z.read(h).decode("utf-8", "ignore")
        print("-" * 78)
        print(f"### {h}  ({len(txt)} 字节)")
        blocks = re.split(r"^--- !u!", txt, flags=re.M)
        for b in blocks:
            m = re.search(r'm_Name:\s*"?([^"\r\n]*)"?', b)
            if not m:
                continue
            nm = unesc(m.group(1).strip('"'))
            if nm == t or "Condition" in nm or "需要" in nm or "Node" in nm:
                print(f"\n--- block m_Name={nm!r} ---")
                print(b[:1600])
