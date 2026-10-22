# -*- coding: utf-8 -*-
"""列出存档里 key 匹配某子串的所有剧情图状态 + hold 记录（找卡住的那张）"""
import json, re, sys, io, hashlib, gzip
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

def load(p):
    d = open(p, "rb").read()
    iv, ct = d[:16], d[16:]
    k = hashlib.pbkdf2_hmac("sha1", b"Meow", iv, 100, 16)
    c = Cipher(algorithms.AES(k), modes.CBC(iv)).decryptor()
    pt = c.update(ct) + c.finalize()
    pad = pt[-1]
    if 1 <= pad <= 16:
        pt = pt[:-pad]
    if pt[:2] == b"\x1f\x8b":
        pt = gzip.decompress(pt)
    t = pt.decode("utf-8")
    t = re.sub(r'(?<=[{,])\s*(-?\d+)\s*:', lambda m: '"%s":' % m.group(1), t)
    return json.loads(t)["Data"]["value"]

v = load(sys.argv[1])
sub = sys.argv[2] if len(sys.argv) > 2 else "2020"
gd = v.get("GraphDataList", {})
hold = v.get("GraphHoldList", {})
ST = {0:"NotOpen",1:"Close",2:"Opened",3:"Started",4:"Complated",5:"Failed",6:"TimeOut",7:"CloseAfterOpen"}

rows = [(k, d) for k, d in gd.items() if sub in k]
print(f"匹配 {sub!r} 的图 {len(rows)} 个:")
for k, d in sorted(rows):
    st = ST.get(d.get("State"), "?")
    print(f"  {k:<22} {st:<15} Last={d.get('LastPlayTime')}  hold={hold.get(k)}  note={d.get('note')}")

print(f"\n=== hold 里匹配 {sub!r} 的（含未在 GraphDataList 的图）===")
for k, val in hold.items():
    if sub in k:
        print(f"  {k}: {val}   State={ST.get(gd.get(k,{}).get('State'),'未记录')}")
