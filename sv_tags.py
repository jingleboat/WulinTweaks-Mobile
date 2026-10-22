# -*- coding: utf-8 -*-
"""导出 SaveObjectStory 的 TagInt / TagString / Hearsay 等，并按关键词过滤"""
import json, re, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

p = sys.argv[1]
raw = open(p, encoding="utf-8").read()
raw = re.sub(r'(?<=[{,])\s*(-?\d+)\s*:', lambda m: '"%s":' % m.group(1), raw)
v = json.loads(raw)["Data"]["value"]

print("=== TagInt (%d) ===" % len(v.get("TagInt", {})))
for k, val in v.get("TagInt", {}).items():
    print(f"  {k} = {val}")

print("\n=== TagString (%d) ===" % len(v.get("TagString", [])))
for s in v.get("TagString", []):
    print("  ", s)

print("\n=== HearsayNewKeys (%d) ===" % len(v.get("HearsayNewKeys", [])))
for s in v.get("HearsayNewKeys", []):
    print("  ", s)

print("\n=== NoteCheckName ===", v.get("NoteCheckName"))
print("=== ActivedCabGraph ===", v.get("ActivedCabGraph"))
print("=== SaveAnimationList (%d) ===" % len(v.get("SaveAnimationList", {})))
print("  ", v.get("SaveAnimationList"))
