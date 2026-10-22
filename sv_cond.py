# -*- coding: utf-8 -*-
"""导出剧情图的条件清单：每个条件节点 → 它被谁依赖(conditionPort/exitConditions)、它决定谁"""
import zipfile, re, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
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
        self.out, self.inn, self.f = [], [], {}
        for fm in re.finditer(r"^\s{2}(\w+):\s*(\S+)\s*$", txt, re.M):
            self.f.setdefault(fm.group(1), fm.group(2))
        self.f = {k: v for k, v in self.f.items() if k != "m_Name"}
        for pm in re.finditer(r"^\s{4}- _fieldName: (\w+)\s*\r?\n(.*?)(?=^\s{4}- _fieldName: |\Z)", txt, re.M | re.S):
            port, body = pm.group(1), pm.group(2)
            dm = re.search(r"_direction:\s*(\d)", body)
            d = int(dm.group(1)) if dm else 1
            for cm in re.finditer(r"-\s*fieldName:\s*(\w+)\s*\r?\n\s*node:\s*\{fileID:\s*(-?\d+)\}", body):
                other = cm.group(2)
                if other == self.fid:
                    continue
                (self.inn if d == 0 else self.out).append((port, other))

def load(g):
    path = f"Assets/GameRes/Story/{g}.asset"
    if path not in Z.namelist():
        c = [n for n in Z.namelist() if n.endswith("/" + g + ".asset")]
        if not c:
            return None
        path = c[0]
    t = Z.read(path).decode("utf-8", "ignore")
    ms = list(B_RE.finditer(t))
    bl = {}
    for i, m in enumerate(ms):
        end = ms[i + 1].start() if i + 1 < len(ms) else len(t)
        body = t[m.end():end]
        nm = re.search(r'\n  m_Name: ("(?:[^"\\]|\\.)*"|[^\r\n]*)', body)
        name = ""
        if nm:
            s = nm.group(1).strip()
            name = unesc(s[1:-1]) if s.startswith('"') else s
        bl[m.group(2)] = Blk(m.group(1), m.group(2), body, name)
    return bl

def desc(b):
    keys = []
    for k in ("Key", "partnerID", "Negate", "isPrisoner", "SaveID", "DoResult", "mapID", "value", "count"):
        if k in b.f:
            keys.append(f"{k}={b.f[k]}")
    return b.name + ("  [" + " ".join(keys) + "]" if keys else "")

COND_HINT = ("需要", "判断", "指定", "当", "选项", "事件失败", "玩家获知")

g = sys.argv[1]
bl = load(g)
print(f"### {g} blocks={len(bl)}\n")
conds = [b for b in bl.values() if any(h in b.name for h in COND_HINT) and b.name not in ("选项",)]
print(f"条件类节点 {len(conds)} 个\n")
for b in sorted(conds, key=lambda x: x.name):
    deps = []
    for port, src in b.inn:
        sb = bl.get(src)
        if sb:
            deps.append(f"{port}←{sb.name}[{src}]")
    outs = []
    for port, dst in b.out:
        db = bl.get(dst)
        if db:
            outs.append(f"{port}→{db.name}[{dst}]")
    print("=" * 74)
    print(desc(b), f"  [{b.fid}]")
    if deps:
        print("   依赖方:", "; ".join(deps))
    if outs:
        print("   决定  :", "; ".join(outs))
