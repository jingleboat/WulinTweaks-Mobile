# -*- coding: utf-8 -*-
"""打印某剧情 asset 里指定节点名的完整 block 原文（含 ports）"""
import zipfile, re, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
Z = zipfile.ZipFile(r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor.zip")

def unesc(s):
    try:
        return s.encode().decode("unicode_escape")
    except Exception:
        return s

gname, want = sys.argv[1], sys.argv[2]
limit = int(sys.argv[3]) if len(sys.argv) > 3 else 1
path = f"Assets/GameRes/Story/{gname}.asset"
t = Z.read(path).decode("utf-8", "ignore")
marks = list(re.finditer(r"^--- !u!(\d+) &(-?\d+)\s*\r?\n", t, re.M))
shown = 0
for i, m in enumerate(marks):
    end = marks[i + 1].start() if i + 1 < len(marks) else len(t)
    body = t[m.end():end]
    nm = re.search(r'\n  m_Name: ("(?:[^"\\]|\\.)*"|[^\r\n]*)', body)
    name = ""
    if nm:
        s = nm.group(1).strip()
        name = unesc(s[1:-1]) if s.startswith('"') else s
    if name != want:
        continue
    shown += 1
    if shown > limit:
        break
    print("=" * 78)
    print(f"--- !u!{m.group(1)} &{m.group(2)}   m_Name={name}")
    print("=" * 78)
    print(body)
