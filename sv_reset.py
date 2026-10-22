# -*- coding: utf-8 -*-
"""把某张图整体重置成"刚触发"前的状态：State=0、note=[]、noteActive=false、GraphHoldList=[]"""
import json, re, sys, io, hashlib, gzip
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

def decrypt(d):
    iv, ct = d[:16], d[16:]
    k = hashlib.pbkdf2_hmac("sha1", b"Meow", iv, 100, 16)
    c = Cipher(algorithms.AES(k), modes.CBC(iv)).decryptor()
    pt = c.update(ct) + c.finalize()
    pad = pt[-1]
    if 1 <= pad <= 16:
        pt = pt[:-pad]
    gz = pt[:2] == b"\x1f\x8b"
    if gz:
        pt = gzip.decompress(pt)
    return pt, iv, gz

def encrypt(plain, iv, gz):
    data = gzip.compress(plain, mtime=0) if gz else plain
    pad = 16 - (len(data) % 16)
    data += bytes([pad]) * pad
    k = hashlib.pbkdf2_hmac("sha1", b"Meow", iv, 100, 16)
    c = Cipher(algorithms.AES(k), modes.CBC(iv)).encryptor()
    return iv + c.update(data) + c.finalize()

src, dst, gname = sys.argv[1], sys.argv[2], sys.argv[3]
plain, iv, gz = decrypt(open(src, "rb").read())
text = plain.decode("utf-8")

# 1) GraphDataList 里的整条 GraphData
pat = re.compile(r'"%s":\{"State":\d+,"note":\[[^\]]*\],"noteActive":(?:true|false)' % re.escape(gname))
m = pat.search(text)
if not m:
    print("!! 没找到", gname, "的 GraphData")
    sys.exit(1)
print("原 GraphData 头:", m.group(0))
new_head = '"%s":{"State":0,"note":[],"noteActive":false' % gname
text = text[:m.start()] + new_head + text[m.end():]

# 2) GraphHoldList 里清空
hp = re.compile(r'"%s":\[[0-9,]*\]' % re.escape(gname))
hm = hp.search(text)
if hm:
    print("原 hold:", hm.group(0))
    text = text[:hm.start()] + '"%s":[]' % gname + text[hm.end():]
else:
    print("（GraphHoldList 里没有该图，跳过）")

out = encrypt(text.encode("utf-8"), iv, gz)
open(dst, "wb").write(out)
print("已写出", dst, len(out), "字节")

chk, _, _ = decrypt(open(dst, "rb").read())
t2 = chk.decode("utf-8")
m2 = pat.search(t2)
print("回读 GraphData 头:", m2.group(0) if m2 else "未找到")
h2 = hp.search(t2)
print("回读 hold:", h2.group(0) if h2 else "无")
