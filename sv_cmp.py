# -*- coding: utf-8 -*-
"""多存档对比：自动解密 SaveObjectStory.save，比较指定剧情图的 State"""
import json, re, sys, io, os, hashlib, gzip
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

ST = {0: "NotOpen", 1: "Close", 2: "Opened", 3: "Started", 4: "Done", 5: "Failed", 6: "TimeOut", 7: "CloseAfOpen"}
BASE = r"device-saves"

def dec(p):
    d = open(p, "rb").read()
    iv, ct = d[:16], d[16:]
    key = hashlib.pbkdf2_hmac("sha1", b"Meow", iv, 100, 16)
    c = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    pt = c.update(ct) + c.finalize()
    pad = pt[-1]
    if 1 <= pad <= 16:
        pt = pt[:-pad]
    if pt[:2] == b"\x1f\x8b":
        pt = gzip.decompress(pt)
    return pt

def load(p):
    raw = dec(p).decode("utf-8")
    raw = re.sub(r'(?<=[{,])\s*(-?\d+)\s*:', lambda m: '"%s":' % m.group(1), raw)
    return json.loads(raw)["Data"]["value"]

data = {}
for s in sorted(os.listdir(BASE)):
    f = os.path.join(BASE, s, "SaveObjectStory.save")
    if not os.path.exists(f):
        f2 = os.path.join(BASE, s, os.path.basename(s).split("-")[0], "SaveObjectStory.save")
        if os.path.exists(f2):
            f = f2
        else:
            continue
    try:
        data[s] = load(f)
    except Exception as e:
        print("ERR", s, e)

print("可用存档:", list(data.keys()), "\n")
want = sys.argv[1:] or ["R10049-0", "R10049-2", "DLC5Search1-1", "DLC5Search2-3"]
names = [k.split("-")[0] for k in data]
print(f"{'图':<18}" + "".join(f"{n:<20}" for n in names))
print("-" * (18 + 20 * len(names)))
for g in want:
    line = f"{g:<18}"
    for k in data:
        d = data[k].get("GraphDataList", {}).get(g)
        line += (f"{ST.get(d.get('State'),'?')}(o={d.get('OpenedTime',0)})".ljust(20) if isinstance(d, dict) else "—".ljust(20))
    print(line)

print("\n=== 各存档 Failed/TimeOut ===")
for k, v in data.items():
    bad = [f"{g}={ST[d['State']]}" for g, d in v.get("GraphDataList", {}).items() if isinstance(d, dict) and d.get("State") in (5, 6)]
    print(f"  {k.split('-')[0]}: TimeSort={v.get('TimeSort')} 图数={len(v.get('GraphDataList',{}))} 异常={bad}")
