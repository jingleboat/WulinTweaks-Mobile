# -*- coding: utf-8 -*-
"""跨存档查角色实例是否存在 / 是否存活"""
import json, re, sys, io, os, hashlib, gzip
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

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

slots = {}
for s in sorted(os.listdir(BASE)):
    f = os.path.join(BASE, s, "SaveObjectGameCharacter.save")
    if not os.path.exists(f):
        continue
    try:
        slots[s.split("-")[0] + ("(new)" if "2026" in s else "(old)")] = load(f)
    except Exception as e:
        print("ERR", s, e)

uids = sys.argv[1:] or ["10070", "10049", "20025", "42155", "42156"]
for uid in uids:
    print("=" * 70)
    print("uid", uid)
    for name, v in slots.items():
        cc = v.get("createdCharacter", {})
        d = cc.get(uid)
        if d is None:
            hits = [k for k, x in cc.items() if isinstance(x, dict) and str(x.get("m_templeteUid")) == uid]
            print(f"  {name:<16} 实例不存在  模板命中={hits[:5]}  deadCountDict.get={v.get('deadCountDict',{}).get(uid)}")
        else:
            print(f"  {name:<16} 存在 alive={d.get('m_isAlive')} given={d.get('m_givenName')!r} sur={d.get('m_surName')!r}")
    print()
