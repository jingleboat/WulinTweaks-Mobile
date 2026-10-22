# -*- coding: utf-8 -*-
"""在 ModEditor.zip 里搜某个标记/字符串，返回出现它的剧情图与所在节点"""
import zipfile, re, sys, io, os
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
Z = zipfile.ZipFile(r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor.zip")

def unesc(s):
    try:
        return s.encode().decode("unicode_escape")
    except Exception:
        return s

kw = sys.argv[1]
# 中文在 asset 里可能被转义为 \uXXXX，大小写都搜
esc = kw.encode("unicode_escape").decode()
esc_u = esc.upper().replace("\\U", "\\u")
raw_kw = kw.encode("utf-8")
needles = {raw_kw, esc.encode(), esc_u.encode()}
print(f"扫描，关键词 {kw!r}，变体 {[n[:24] for n in needles]}\n")

names = [n for n in Z.namelist() if n.endswith(".asset")]
print(f"共 {len(names)} 个 asset\n")

hits = []
for n in names:
    try:
        d = Z.read(n)
    except Exception:
        continue
    if any(x in d for x in needles):
        hits.append(n)

print(f"命中 {len(hits)} 个文件:")
for h in hits:
    print("   ", h)

# 对命中的 Story/Scenario 文件，打印含关键词的节点名
for n in hits:
    if not (n.startswith("Assets/GameRes/Story/") or n.startswith("Assets/GameRes/Scenario/")):
        continue
    t = Z.read(n).decode("utf-8", "ignore")
    print("\n" + "=" * 74)
    print("###", n)
    marks = list(re.finditer(r"^--- !u!(\d+) &(-?\d+)\s*\r?\n", t, re.M))
    for i, m in enumerate(marks):
        end = marks[i + 1].start() if i + 1 < len(marks) else len(t)
        body = t[m.end():end]
        if esc not in body and kw not in body:
            continue
        nm = re.search(r'\n  m_Name: ("(?:[^"\\]|\\.)*"|[^\r\n]*)', body)
        name = ""
        if nm:
            s = nm.group(1).strip()
            name = unesc(s[1:-1]) if s.startswith('"') else s
        # 找该字符串所在行
        lines = [l.strip() for l in body.splitlines() if esc in l or kw in l]
        print(f"  [{m.group(2)}] {name}   字段: {lines[:3]}")
