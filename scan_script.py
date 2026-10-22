import json, re, io
p = "pcdump/script.json"
txt = open(p, encoding="utf-8", errors="replace").read()
print("loaded", len(txt))
for m in re.finditer(r'"(?:Value|Name)"\s*:\s*"((?:[^"\\]|\\.)*)"', txt):
    s = m.group(1)
    if re.search(r'(?i)dlc', s) and ("/" in s or "\\" in s or "." in s):
        print(repr(s))
