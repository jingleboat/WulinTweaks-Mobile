# -*- coding: utf-8 -*-
"""改存档：把某张图的 GraphHoldList 记录替换成指定内容（用于剧情回退）
用法: python sv_hold.py <源.save> <输出.save> <图名> <新列表，如 空 或 64 或 64,65>
"""
import hashlib, gzip, sys, io, re
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

src, dst, gname, newlist = sys.argv[1], sys.argv[2], sys.argv[3], sys.argv[4]
plain, iv, gz = decrypt(open(src, "rb").read())
text = plain.decode("utf-8")

pat = re.compile(r'"%s":\[[0-9,]*\]' % re.escape(gname))
m = pat.search(text)
if not m:
    print("!! 没找到", gname, "的 hold 记录")
    sys.exit(1)
print("原:", m.group(0))

items = [x.strip() for x in newlist.split(",") if x.strip()] if newlist not in ("空", "-", "") else []
repl = '"%s":[%s]' % (gname, ",".join(items))
print("新:", repl)
new_text = text.replace(m.group(0), repl, 1)

out = encrypt(new_text.encode("utf-8"), iv, gz)
open(dst, "wb").write(out)
print("已写出", dst, len(out), "字节")

chk, _, _ = decrypt(open(dst, "rb").read())
m2 = pat.search(chk.decode("utf-8"))
print("回读:", m2.group(0) if m2 else "未找到")
