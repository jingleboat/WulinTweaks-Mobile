# -*- coding: utf-8 -*-
"""手机/PC 通用水林存档解密：AES-128-CBC(pbkdf2-sha1 'Meow', iv=d[:16], 100轮) + PKCS7 + 可选 gzip"""
import hashlib, gzip, sys, io, os
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

def decrypt_bytes(d):
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

def decrypt_file(p):
    return decrypt_bytes(open(p, "rb").read())

if __name__ == "__main__":
    src = sys.argv[1]
    out = sys.argv[2] if len(sys.argv) > 2 else src + ".json"
    pt = decrypt_file(src)
    open(out, "wb").write(pt)
    print("OK ->", out, len(pt), "bytes")
    print(pt[:400].decode("utf-8", "replace"))
