# -*- coding: utf-8 -*-
"""在 PC 端 LocData + 事件 zip 里搜关键词，返回 uName（剧情图 id / 对话 key / 角色名 key）"""
import zipfile, re, os, sys, io, json

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

LOC = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor\Assets\GameRes\json\3\LocData.txt"
ZIP = r"D:\steamgame\steamapps\common\WulinSH\Wulin_Data\StreamingAssets\ModEditor\ModEditor.zip"

kws = sys.argv[1:] or ["月咏樱羽", "大梁捞朱", "清风遗痕"]

print("LocData 存在:", os.path.exists(LOC))
rows = json.loads(open(LOC, "rb").read().decode("utf-8-sig"))["LocData"]
print("LocData 条数:", len(rows))

for kw in kws:
    print("\n" + "=" * 70)
    print("关键词:", kw)
    print("=" * 70)
    hit = 0
    for r in rows:
        for k in ("SChinese", "uName"):
            v = r.get(k) or ""
            if kw in v:
                print(f"  uName={r.get('uName')!r:<45} SC={r.get('SChinese')!r}")
                hit += 1
                break
        if hit > 60:
            print("  ...(截断)")
            break
    print(f"  共 {hit} 条")

# zip 里的事件文件名 & 内容搜
z = zipfile.ZipFile(ZIP)
names = [n for n in z.namelist() if "/GameRes/Story/" in n and n.endswith(".asset")]
print(f"\n事件文件数: {len(names)}")
for kw in kws:
    found = []
    for n in names:
        try:
            d = z.read(n)
        except Exception:
            continue
        if kw.encode("utf-8") in d:
            found.append(n)
    print(f"\n[zip 事件内容含 {kw!r}] {len(found)} 个")
    for n in found[:20]:
        print("   ", n)
