# -*- coding: utf-8 -*-
"""列出一个剧情 asset 里所有 block 的 m_Name 统计"""
import zipfile, re, sys, io, collections
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
Z = zipfile.ZipFile(r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor.zip")

def unesc(s):
    try:
        return s.encode().decode("unicode_escape")
    except Exception:
        return s

NAME_RE = re.compile(r'\n  m_Name: ("(?:[^"\\]|\\.)*"|[^\r\n]*)')

for gname in sys.argv[1:]:
    path = f"Assets/GameRes/Story/{gname}.asset"
    if path not in Z.namelist():
        cand = [n for n in Z.namelist() if n.endswith("/" + gname + ".asset")]
        if not cand:
            print("!! 找不到", gname); continue
        path = cand[0]
    t = Z.read(path).decode("utf-8", "ignore")
    ns = []
    for m in NAME_RE.finditer(t):
        s = m.group(1).strip()
        if s.startswith('"'):
            s = unesc(s[1:-1])
        ns.append(s)
    c = collections.Counter(ns)
    print("=" * 70)
    print(gname, "block 数", len(ns), "不同名", len(c))
    print("=" * 70)
    for k, v in c.most_common(80):
        print(f"{v:>4}  {k}")
    print()
