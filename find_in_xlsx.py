# -*- coding: utf-8 -*-
import os, re, sys, io, zipfile
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

BASE = r"E:\download\xls"
targets = ["一醉方休", "杯中明镜", "烈酒与故事", "纪家密室"]

found = {t: [] for t in targets}
for sub in ("1", "3"):
    d = os.path.join(BASE, sub)
    if not os.path.isdir(d):
        continue
    for fn in os.listdir(d):
        if not fn.lower().endswith(".xlsx"):
            continue
        p = os.path.join(d, fn)
        try:
            with zipfile.ZipFile(p) as z:
                names = [n for n in z.namelist() if n.endswith("sharedStrings.xml") or n.endswith(".xml")]
                blob = b""
                for n in names:
                    if "sharedStrings" in n:
                        blob += z.read(n)
                if not blob:
                    continue
                txt = blob.decode("utf-8", "ignore")
        except Exception as e:
            continue
        for t in targets:
            if t in txt:
                found[t].append(f"{sub}/{fn}")

for t in targets:
    print(f"=== {t}")
    for f in found[t]:
        print("   ", f)
    if not found[t]:
        print("    (not found)")
