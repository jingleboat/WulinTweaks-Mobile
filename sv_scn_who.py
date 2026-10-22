# -*- coding: utf-8 -*-
"""把 r10049-2_2_2.._2_6 的每句「说话人 NpcID + 文本」按顺序打出来，并用 LocData 解析成中文。"""
import zipfile, sys, io, os, json, tempfile, glob
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")
import UnityPy

APK = r"E:\笔记\wulin_v49-Hold按条件来源压制-2026-10-21.apk"
LOC = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor\Assets\GameRes\json\3\LocData.txt"

loc = {}
try:
    rows = json.loads(open(LOC, "rb").read().decode("utf-8-sig"))["LocData"]
    for r in rows:
        u = r.get("uName") or ""
        if u:
            loc[u] = r.get("SChinese") or ""
except Exception as e:
    print("LocData 读取失败:", e)

STEMS = ["r10049-2_2_1", "r10049-2_2_2", "r10049-2_2_3", "r10049-2_2_4",
         "r10049-2_2_5", "r10049-2_2_6", "r10049-2_2_7_1", "r10049-2_2_7_2",
         "r10049-2_2_8", "r10049-2_2_9"]

z = zipfile.ZipFile(APK)
tmp = tempfile.mkdtemp(prefix="scn2_")
for stem in STEMS:
    entry = "assets/aa/Android/scenario_assets_statemachine/scenario/%s.asset.bundle" % stem
    if entry not in z.namelist():
        print("!! 缺 %s" % stem); continue
    p = os.path.join(tmp, stem + ".bundle")
    open(p, "wb").write(z.read(entry))
    env = UnityPy.load(p)

    lines = []
    for obj in env.objects:
        if obj.type.name != "MonoBehaviour":
            continue
        d = obj.read_typetree()
        if d.get("m_Name") != "对话":
            continue
        lines.append(dict(x=d.get("position", {}).get("x", 0.0),
                          npc=d.get("NpcID", 0),
                          txt=d.get("text", ""),
                          dur=d.get("duration", 0)))
    lines.sort(key=lambda r: r["x"])

    print("\n============ %s  (%d 句) ============" % (stem.upper(), len(lines)))
    for r in lines:
        t = loc.get(r["txt"], "")
        print("  NpcID=%-7s x=%-7s %-26s %s" % (r["npc"], int(r["x"]), r["txt"], t))
