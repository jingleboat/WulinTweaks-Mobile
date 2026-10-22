# -*- coding: utf-8 -*-
"""读存档里指定图的状态/进度，用于回退"""
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
gd = v.get("GraphDataList", {})
hold = v.get("GraphHoldList", {})
ST = {0:"NotOpen",1:"Close",2:"Opened",3:"Started",4:"Complated",5:"Failed",6:"TimeOut",7:"CloseAfterOpen"}

for g in sys.argv[2:]:
    print("=" * 70)
    print(g, "->", ST.get(gd.get(g, {}).get("State"), "?"), json.dumps(gd.get(g), ensure_ascii=False))
    print("  GraphHoldList:", hold.get(g))
print()
print("=== 所有带 Hold 记录的 EJ/大梁相关图 ===")
for k, val in hold.items():
    if "EJ" in k or "M20100" in k or "DLC5" in k:
        print(f"  {k}: {val}  State={ST.get(gd.get(k,{}).get('State'),'?')}")
