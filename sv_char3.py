# -*- coding: utf-8 -*-
"""跨存档查角色实例（自动适配目录层级）"""
import json, re, sys, io, os, hashlib, gzip
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

BASE = r"device-saves"

def dec(p):
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
    return pt

def load(p):
    raw = dec(p).decode("utf-8")
    raw = re.sub(r'(?<=[{,])\s*(-?\d+)\s*:', lambda m: '"%s":' % m.group(1), raw)
    return json.loads(raw)["Data"]["value"]

paths = {}
for s in sorted(os.listdir(BASE)):
    slot = s.split("-")[0]
    for c in (os.path.join(BASE, s, "SaveObjectGameCharacter.save"),
              os.path.join(BASE, s, slot, "SaveObjectGameCharacter.save")):
        if os.path.exists(c):
            paths[s] = c
            break

cache = {}
for s, p in paths.items():
    try:
        cache[s] = load(p)
    except Exception as e:
        print("ERR", s, e)

uids = sys.argv[1:] or ["10070", "10049"]
for uid in uids:
    print("=" * 70)
    print("uid", uid)
    for s, v in cache.items():
        cc = v.get("createdCharacter", {})
        d = cc.get(uid)
        if d is None:
            hits = [k for k, x in cc.items() if isinstance(x, dict) and str(x.get("m_templeteUid")) == uid]
            print(f"  {s:<22} 不存在  模板命中={hits[:5]}  deadCnt={v.get('deadCountDict',{}).get(uid)}")
        else:
            print(f"  {s:<22} 存在 alive={d.get('m_isAlive')} given={d.get('m_givenName')!r} sur={d.get('m_surName')!r}")
    print()
