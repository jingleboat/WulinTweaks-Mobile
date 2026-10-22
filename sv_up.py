# -*- coding: utf-8 -*-
"""剧情 asset 图分析：建立节点图，反向查某个节点的上游，并打印条件内容"""
import zipfile, re, sys, io, collections
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
Z = zipfile.ZipFile(r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor.zip")

def unesc(s):
    try:
        return s.encode().decode("unicode_escape")
    except Exception:
        return s

NAME_RE = re.compile(r'\n  m_Name: ("(?:[^"\\]|\\.)*"|[^\r\n]*)')
BLOCK_RE = re.compile(r"^--- !u!(\d+) &(-?\d+)\s*\r?\n", re.M)

class Blk:
    def __init__(self, cls, fid, txt, name):
        self.cls, self.fid, self.txt, self.name = cls, fid, txt, name
        self.out = []   # (fieldName, fid)
        self.inn = []   # (fieldName, srcfid)

def load(gname):
    path = f"Assets/GameRes/Story/{gname}.asset"
    if path not in Z.namelist():
        cand = [n for n in Z.namelist() if n.endswith("/" + gname + ".asset")]
        if not cand:
            return None, None
        path = cand[0]
    t = Z.read(path).decode("utf-8", "ignore")
    marks = list(BLOCK_RE.finditer(t))
    blocks = {}
    for i, m in enumerate(marks):
        cls, fid = m.group(1), m.group(2)
        end = marks[i + 1].start() if i + 1 < len(marks) else len(t)
        body = t[m.end():end]
        nm = NAME_RE.search(body)
        name = ""
        if nm:
            s = nm.group(1).strip()
            name = unesc(s[1:-1]) if s.startswith('"') else s
        blocks[fid] = Blk(cls, fid, body, name)
    # 连边：在 ports 里找 fieldName + 后面的 node: {fileID: N}
    for fid, b in blocks.items():
        for cm in re.finditer(r"fieldName:\s*(\w+)\s*\r?\n(?:\s+\w+:.*\r?\n)*?\s+node:\s*\{fileID:\s*(-?\d+)\}", b.txt):
            field, tgt = cm.group(1), cm.group(2)
            b.out.append((field, tgt))
            if tgt in blocks:
                blocks[tgt].inn.append((field, fid))
    return blocks, path

KEY = ("partnerID", "Negate", "isPrisoner", "needShowBubble", "autoCallFunc",
       "mapID", "itemID", "count", "value", "factionID", "graphName", "tagKey",
       "tagValue", "graph", "id", "npcID", "battleID", "flagKey", "key", "target")

def brief(b):
    kv = []
    for k in KEY:
        for m in re.finditer(rf"^\s{{2}}{k}:\s*(\S+)\s*$", b.txt, re.M):
            kv.append(f"{k}={m.group(1)}")
    return b.name + ("  [" + " ".join(kv[:8]) + "]" if kv else "")

def main(gname, want):
    blocks, path = load(gname)
    if not blocks:
        print("找不到", gname); return
    print(f"### {gname}  blocks={len(blocks)}")
    targets = [fid for fid, b in blocks.items() if b.name == want]
    print(f"### m_Name == {want!r} 的节点 {len(targets)} 个\n")
    for tfid in targets:
        b = blocks[tfid]
        print("=" * 78)
        print(f"节点 [{tfid}] {brief(b)}")
        print(f"  入边 {len(b.inn)} 条：")
        for field, src in b.inn:
            sb = blocks.get(src)
            print(f"    ← [{src}] <{sb.cls}> {brief(sb)}   (via {field})")
            # 再往上一层
            if sb:
                for f2, s2 in sb.inn:
                    b2 = blocks.get(s2)
                    print(f"        ←← [{s2}] {brief(b2) if b2 else '?'}  (via {f2})")
        print()

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2] if len(sys.argv) > 2 else "事件失败")
