# -*- coding: utf-8 -*-
"""不依赖 openpyxl：直接解 xlsx（zip + sharedStrings + sheet xml）"""
import zipfile, re, sys, io
from xml.etree import ElementTree as ET
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

NS = "{http://schemas.openxmlformats.org/spreadsheetml/2006/main}"
path = sys.argv[1]
row_from = int(sys.argv[2]) if len(sys.argv) > 2 else 1
row_to = int(sys.argv[3]) if len(sys.argv) > 3 else 99999

z = zipfile.ZipFile(path)
# shared strings
shared = []
if "xl/sharedStrings.xml" in z.namelist():
    root = ET.fromstring(z.read("xl/sharedStrings.xml"))
    for si in root.findall(NS + "si"):
        txt = "".join(t.text or "" for t in si.iter(NS + "t"))
        shared.append(txt)

# 第一个 sheet
sheets = [n for n in z.namelist() if n.startswith("xl/worksheets/sheet")]
sheets.sort()
root = ET.fromstring(z.read(sheets[0]))
print("sheet:", sheets[0], " sharedStrings:", len(shared))

def colnum(ref):
    m = re.match(r"([A-Z]+)", ref)
    s = m.group(1)
    n = 0
    for ch in s:
        n = n * 26 + (ord(ch) - 64)
    return n

for row in root.iter(NS + "row"):
    r = int(row.get("r", "0"))
    if r < row_from or r > row_to:
        continue
    cells = {}
    for c in row.findall(NS + "c"):
        ref = c.get("r", "")
        t = c.get("t")
        v = c.find(NS + "v")
        isel = c.find(NS + "is")
        if t == "s" and v is not None:
            val = shared[int(v.text)] if int(v.text) < len(shared) else ""
        elif isel is not None:
            val = "".join(x.text or "" for x in isel.iter(NS + "t"))
        elif v is not None:
            val = v.text or ""
        else:
            val = ""
        if val.strip():
            cells[colnum(ref)] = val
    if cells:
        line = " | ".join(f"{k}:{v}" for k, v in sorted(cells.items()))
        print(f"[{r}] {line}")
