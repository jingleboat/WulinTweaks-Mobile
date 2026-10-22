# -*- coding: utf-8 -*-
import json, re, sys, io
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

p = r"C:\Users\User\Documents\deepseek-harness\default-workspace\wulin-apk\save0_story.json"
txt = open(p, encoding="utf-8").read()

targets = ["一醉方休", "杯中明镜", "烈酒与故事", "纪家密室", "苗彩蝶"]
print("=== 直接搜中文 ===")
for name in targets:
    idxs = [m.start() for m in re.finditer(re.escape(name), txt)]
    print(f"{name}: {len(idxs)} hits")
    for i in idxs[:3]:
        print("   ...", txt[max(0, i-60):i+60].replace("\n", " "))

j = json.loads(txt)
v = j["Data"]["value"]

print("\n=== TagInt 里带中文的键（前 40）===")
tagint = v.get("TagInt", {})
cjk = [k for k in tagint if re.search(r"[\u4e00-\u9fff]", k)]
print("含中文键数量:", len(cjk), "/ 总数", len(tagint))
for k in cjk[:40]:
    print(f"   {k} = {tagint[k]}")

print("\n=== TagString 里带中文的键（前 20）===")
tags = v.get("TagString", {})
cjk2 = [k for k in tags if isinstance(k, str) and re.search(r"[\u4e00-\u9fff]", k)]
print("含中文键数量:", len(cjk2), "/ 总数", len(tags))
for k in cjk2[:20]:
    print(f"   {k} = {tags[k]}")
