# -*- coding: utf-8 -*-
"""解析剧情 asset 的节点连接图：从根对象 BFS，打印可达的节点树（含条件内容）"""
import zipfile, re, sys, io, os
from collections import deque

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

class Block:
    __slots__ = ("fid", "cls", "name", "txt", "conns", "fields")
    def __init__(self, fid, cls, name, txt):
        self.fid, self.cls, self.name, self.txt = fid, cls, name, txt
        self.conns = []   # [(fieldName, targetFid)]
        self.fields = {}

def parse(path):
    txt = Z.read(path).decode("utf-8", "ignore")
    blocks = {}
    # 切成 "--- !u!<cls> &<fid>" 分段
    parts = re.split(r"^--- !u!", txt, flags=re.M)[1:]
    for p in parts:
        m = re.match(r"^(\d+) &(-?\d+)\s*\r?\n", p)
        if not m:
            continue
        cls, fid = int(m.group(1)), m.group(2)
        mn = re.search(r'\n  m_Name:\s*"?((?:[^"\\\r\n]|\\.)*)"?\s*\r?\n', p)
        name = unesc(mn.group(1)) if mn else ""
        b = Block(fid, cls, name, p)
        # 连接：fieldName: xxx / node: {fileID: N}
        for cm in re.finditer(r"fieldName:\s*(\w+)\s*\r?\n\s*node:\s*\{fileID:\s*(-?\d+)\}", p):
            b.conns.append((cm.group(1), cm.group(2)))
        for fm in re.finditer(r"^\s{2}(\w+):\s*(-?\d+)\s*$", p, re.M):
            b.fields.setdefault(fm.group(1), fm.group(2))
        blocks[fid] = b
    return blocks

COND_KEYS = ("partnerID", "Negate", "isPrisoner", "needShowBubble", "autoCallFunc", "partnerName",
             "mapID", "itemID", "count", "value", "factionID", "graphID", "tagKey", "tagValue", "id")

def brief(b):
    s = b.name
    kv = []
    for k in COND_KEYS:
        m = re.search(rf"^\s{{2}}{k}:\s*(\S+)\s*$", b.txt, re.M)
        if m:
            kv.append(f"{k}={m.group(1)}")
    if kv:
        s += "  [" + " ".join(kv) + "]"
    return s

def walk(blocks, root_fid, maxdepth=6):
    root = blocks.get(root_fid)
    if not root:
        print("!! root", root_fid, "not found")
        return
    print("ROOT:", root_fid, root.cls, repr(root.name))
    seen = set()
    q = deque([(root_fid, 0, "")])
    while q:
        fid, d, via = q.popleft()
        if fid in seen or d > maxdepth:
            continue
        seen.add(fid)
        b = blocks.get(fid)
        if not b:
            print("  " * d + f"<?> {fid} (未定义)")
            continue
        print("  " * d + f"[{fid}] <{b.cls}> {brief(b)}" + (f"   ← {via}" if via else ""))
        for field, tgt in b.conns:
            q.append((tgt, d + 1, field))

if __name__ == "__main__":
    name = sys.argv[1]
    depth = int(sys.argv[2]) if len(sys.argv) > 2 else 5
    path = f"Assets/GameRes/Story/{name}.asset"
    if path not in Z.namelist():
        cand = [n for n in Z.namelist() if n.endswith("/" + name + ".asset")]
        path = cand[0] if cand else None
    if not path:
        print("找不到", name); sys.exit(1)
    blocks = parse(path)
    print("blocks:", len(blocks))
    walk(blocks, "11400000", depth)
