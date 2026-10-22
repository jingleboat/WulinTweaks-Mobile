# -*- coding: utf-8 -*-
"""剧情 asset 图分析（正确方向版）：端口 _direction:0=输入(对方是上游) / 1=输出(对方是下游)
用法:
  python sv_graph_full.py <图名> tree [深度]      # 从事件头 BFS 打印下游流程
  python sv_graph_full.py <图名> up <节点名>      # 打印该节点上游链
  python sv_graph_full.py <图名> path <节点名>    # 打印从事件头到该节点的路径
"""
import zipfile, re, sys, io
from collections import deque
Z = zipfile.ZipFile(r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor.zip")

def unesc(s):
    try:
        return s.encode().decode("unicode_escape")
    except Exception:
        return s

B_RE = re.compile(r"^--- !u!(\d+) &(-?\d+)\s*\r?\n", re.M)

class Blk:
    def __init__(self, cls, fid, txt, name):
        self.cls, self.fid, self.txt, self.name = cls, fid, txt, name
        self.out = []   # (portName, dstFid)
        self.inn = []   # (portName, srcFid)
        self.fields = {}
        self._parse()

    def _parse(self):
        for fm in re.finditer(r"^\s{2}(\w+):\s*(-?\d+)\s*$", self.txt, re.M):
            self.fields.setdefault(fm.group(1), fm.group(2))
        # 逐个端口块处理
        for pm in re.finditer(r"^\s{4}- _fieldName: (\w+)\s*\r?\n(.*?)(?=^\s{4}- _fieldName: |\Z)", self.txt, re.M | re.S):
            port, body = pm.group(1), pm.group(2)
            dm = re.search(r"_direction:\s*(\d)", body)
            direction = int(dm.group(1)) if dm else 1
            for cm in re.finditer(r"-\s*fieldName:\s*(\w+)\s*\r?\n\s*node:\s*\{fileID:\s*(-?\d+)\}", body):
                other = cm.group(2)
                if other == self.fid:
                    continue
                if direction == 0:
                    self.inn.append((port, other))
                else:
                    self.out.append((port, other))

COND = ("partnerID", "Negate", "isPrisoner", "needShowBubble", "autoCallFunc",
        "mapID", "itemID", "count", "value", "factionID", "tagKey", "tagValue",
        "graphName", "graph", "npcID", "battleID", "key", "id", "SaveID",
        "IsSaveNode", "DoResult", "Limit", "TimeLimit")

def brief(b):
    kv = []
    for k in COND:
        for m in re.finditer(rf"^\s{{2}}{k}:\s*(\S+)\s*$", b.txt, re.M):
            kv.append(f"{k}={m.group(1)}")
    return b.name + ("  [" + " ".join(kv[:6]) + "]" if kv else "")

def load(gname):
    path = f"Assets/GameRes/Story/{gname}.asset"
    if path not in Z.namelist():
        cand = [n for n in Z.namelist() if n.endswith("/" + gname + ".asset")]
        if not cand:
            return None
        path = cand[0]
    t = Z.read(path).decode("utf-8", "ignore")
    marks = list(B_RE.finditer(t))
    blocks = {}
    for i, m in enumerate(marks):
        end = marks[i + 1].start() if i + 1 < len(marks) else len(t)
        body = t[m.end():end]
        nm = re.search(r'\n  m_Name: ("(?:[^"\\]|\\.)*"|[^\r\n]*)', body)
        name = ""
        if nm:
            s = nm.group(1).strip()
            name = unesc(s[1:-1]) if s.startswith('"') else s
        blocks[m.group(2)] = Blk(m.group(1), m.group(2), body, name)
    return blocks

def tree(blocks, maxd):
    roots = [f for f, b in blocks.items() if b.name == "事件头"]
    if not roots:
        print("!! 没有事件头"); return
    seen = set()
    q = deque([(roots[0], 0)])
    while q:
        fid, d = q.popleft()
        if fid in seen:
            continue
        seen.add(fid)
        b = blocks.get(fid)
        if not b:
            print("  " * d + f"<?{fid}>"); continue
        print("  " * d + f"[{fid}] {brief(b)}")
        if d >= maxd:
            continue
        for port, tgt in b.out:
            q.append((tgt, d + 1))

def up(blocks, want):
    for fid, b in blocks.items():
        if b.name != want:
            continue
        print("=" * 76)
        print(f"[{fid}] {brief(b)}  上游 {len(b.inn)}")
        for port, src in b.inn:
            sb = blocks.get(src)
            print(f"  ← [{src}] {brief(sb) if sb else '?'}   (经 {sb and sb.name}.{port})")
        print(f"  下游 {len(b.out)}")
        for port, dst in b.out:
            db = blocks.get(dst)
            print(f"  → [{dst}] {brief(db) if db else '?'}   (经 {port})")

if __name__ == "__main__":
    g = sys.argv[1]
    mode = sys.argv[2] if len(sys.argv) > 2 else "tree"
    blocks = load(g)
    if not blocks:
        print("找不到", g); sys.exit(1)
    print(f"### {g}  blocks={len(blocks)}\n")
    if mode == "tree":
        tree(blocks, int(sys.argv[3]) if len(sys.argv) > 3 else 4)
    elif mode == "up":
        up(blocks, sys.argv[3])
