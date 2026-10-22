# -*- coding: utf-8 -*-
"""列出某个剧情 asset 里全部条件节点（m_Name / partnerID / Negate / 其它条件字段）"""
import zipfile, re, sys, io, os

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
ZIP = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor.zip"
Z = zipfile.ZipFile(ZIP)

def unesc(s):
    if "\\u" in s:
        try:
            return s.encode().decode("unicode_escape")
        except Exception:
            return s
    return s

COND_FIELDS = ["partnerID", "Negate", "isPrisoner", "needShowBubble", "autoCallFunc",
               "partnerName", "itemID", "count", "value", "mapID", "factionID", "tagKey",
               "tagValue", "compare", "state", "graph", "id", "level", "min", "max"]

def dump_graph(name):
    path = f"Assets/GameRes/Story/{name}.asset"
    if path not in Z.namelist():
        cand = [n for n in Z.namelist() if n.endswith("/" + name + ".asset")]
        if not cand:
            print("!! 找不到", name)
            return
        path = cand[0]
    txt = Z.read(path).decode("utf-8", "ignore")
    print("=" * 80)
    print("### ", path, len(txt), "字节")
    blocks = re.split(r"^--- !u!", txt, flags=re.M)[1:]
    print(f"共 {len(blocks)} 个 block")
    print("-" * 80)
    # 图对象（11400000）
    gm = re.search(r"^114 &11400000\r?\n(.*?)(?=\r?\n--- |\Z)", txt, re.S | re.M)
    if gm:
        mm = re.search(r'm_Name:\s*"?([^"\r\n]*)"?', gm.group(1))
        print("图 m_Name =", unesc(mm.group(1).strip('"')) if mm else "?")
    print()
    for b in blocks:
        fid = re.match(r"^(\d+) &(-?\d+)", b)
        mn = re.search(r'm_Name:\s*"?([^"\r\n]*)"?', b)
        if not mn:
            continue
        nm = unesc(mn.group(1).strip('"'))
        fields = []
        for f in COND_FIELDS:
            m = re.search(rf"^\s*{f}:\s*(\S+)\s*$", b, re.M)
            if m:
                fields.append(f"{f}={m.group(1)}")
        # 只打印条件类节点（有 partnerID / 名字含条件关键字）
        if "partnerID" in b or any(k in nm for k in ("需要", "条件", "Check", "Condition", "Hold", "Negate")):
            print(f"[{fid.group(2) if fid else '?'}] {nm}")
            if fields:
                print("      ", "  ".join(fields))

for n in sys.argv[1:]:
    dump_graph(n)
