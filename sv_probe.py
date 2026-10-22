# -*- coding: utf-8 -*-
"""只读：在存档所有文件里搜关键词，并查玩家等级"""
import json, re, sys, io, hashlib, gzip, os
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

DIR = sys.argv[1]
KWS = sys.argv[2:]

def decrypt(d):
    iv, ct = d[:16], d[16:]
    k = hashlib.pbkdf2_hmac("sha1", b"Meow", iv, 100, 16)
    c = Cipher(algorithms.AES(k), modes.CBC(iv)).decryptor()
    pt = c.update(ct) + c.finalize()
    pad = pt[-1]
    if 1 <= pad <= 16:
        pt = pt[:-pad]
    if pt[:2] == b"\x1f\x8b":
        pt = gzip.decompress(pt)
    return pt

texts = {}
base = DIR
if os.path.isdir(os.path.join(DIR, os.path.basename(DIR).split("-")[0])):
    base = os.path.join(DIR, os.path.basename(DIR).split("-")[0])
for f in sorted(os.listdir(base)):
    if not f.endswith(".save"):
        continue
    try:
        texts[f] = decrypt(open(os.path.join(base, f), "rb").read()).decode("utf-8", "replace")
    except Exception as e:
        print("解密失败", f, e)

print("已解密:", list(texts.keys()))
for kw in KWS:
    print("\n" + "=" * 60)
    print("关键词:", kw)
    hit = False
    for f, t in texts.items():
        n = t.count(kw)
        if n:
            hit = True
            i = t.find(kw)
            print(f"  {f}: {n} 次   上下文: ...{t[max(0,i-90):i+40]}...")
    if not hit:
        print("  未找到")

# 玩家等级
t = texts.get("SaveObjectGameCharacter.save", "")
if t:
    m = re.search(r'"100401":\{[^}]{0,4000}?"m_level":(\d+)', t)
    print("\n玩家(100401) m_level =", m.group(1) if m else "未匹配到")
    m2 = re.findall(r'"m_level":(\d+)', t)
    if m2:
        print("  存档里出现的 m_level 前 10 个:", m2[:10])
