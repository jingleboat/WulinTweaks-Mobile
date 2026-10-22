# -*- coding: utf-8 -*-
"""在 PC 的 ModEditor.zip（全部剧情 .asset，YAML）里找哪些图含有指定 partnerID 的「需要伙伴」条件。
   用于定位手机端日志里那个反复重判的门对（10075 / 10033）属于哪张图。"""
import zipfile, re, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

ZIP = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor.zip"
PIDS = sys.argv[1:] or ["10033", "10075"]

z = zipfile.ZipFile(ZIP)
names = [n for n in z.namelist() if n.startswith("Assets/GameRes/Story/") and n.endswith(".asset")]
print("剧情 asset 数:", len(names))

hits = {p: [] for p in PIDS}
for n in names:
    try:
        t = z.read(n).decode("utf-8", "ignore")
    except Exception:
        continue
    for p in PIDS:
        # 需要伙伴 节点的 partnerID 字段
        if re.search(r"partnerID:\s*%s\b" % p, t):
            hits[p].append(n.split("/")[-1].replace(".asset", ""))

for p in PIDS:
    lst = sorted(set(hits[p]))
    print("\n=== partnerID %s 出现在 %d 张图 ===" % (p, len(lst)))
    for g in lst:
        print("   ", g)
