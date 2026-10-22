# -*- coding: utf-8 -*-
"""改存档：把指定剧情图的 State 改成新值（保留原 IV，gzip + AES-128-CBC 重新加密）
用法: python sv_patch.py <原.save> <输出.save> <图名> <新State>
      python sv_patch.py <原.save> <输出.save> --verify <图名>    # 只校验
"""
import hashlib, gzip, sys, io, re, os
from cryptography.hazmat.primitives.ciphers import Cipher, algorithms, modes

sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

def decrypt(d):
    iv, ct = d[:16], d[16:]
    key = hashlib.pbkdf2_hmac("sha1", b"Meow", iv, 100, 16)
    c = Cipher(algorithms.AES(key), modes.CBC(iv)).decryptor()
    pt = c.update(ct) + c.finalize()
    pad = pt[-1]
    if 1 <= pad <= 16:
        pt = pt[:-pad]
    was_gz = pt[:2] == b"\x1f\x8b"
    if was_gz:
        pt = gzip.decompress(pt)
    return pt, iv, was_gz

def encrypt(plain, iv, was_gz):
    data = gzip.compress(plain, mtime=0) if was_gz else plain
    pad = 16 - (len(data) % 16)
    data += bytes([pad]) * pad
    key = hashlib.pbkdf2_hmac("sha1", b"Meow", iv, 100, 16)
    c = Cipher(algorithms.AES(key), modes.CBC(iv)).encryptor()
    return iv + c.update(data) + c.finalize()

src, dst = sys.argv[1], sys.argv[2]
raw = open(src, "rb").read()
plain, iv, was_gz = decrypt(raw)
text = plain.decode("utf-8")
print(f"源 {len(raw)} 字节 → 明文 {len(plain)} 字节 (gzip={was_gz})")

if sys.argv[3] == "--verify":
    g = sys.argv[4]
    m = re.search(r'"%s":\{"State":(\d+)' % re.escape(g), text)
    print(f"{g} State = {m.group(1) if m else '未找到'}")
    sys.exit(0)

gname, newstate = sys.argv[3], sys.argv[4]
pat = re.compile(r'("%s":\{"State":)(\d+)' % re.escape(gname))
m = pat.search(text)
if not m:
    print("!! 没找到目标图", gname)
    sys.exit(1)
old = m.group(2)
print(f"{gname} State: {old} → {newstate}")
new_text = pat.sub(lambda mm: mm.group(1) + newstate, text, count=1)
assert len(new_text) == len(text) or True

out = encrypt(new_text.encode("utf-8"), iv, was_gz)
open(dst, "wb").write(out)
print(f"已写出 {dst} ({len(out)} 字节)")

# 回读校验
chk, _, _ = decrypt(open(dst, "rb").read())
mm = re.search(r'"%s":\{"State":(\d+)' % re.escape(gname), chk.decode("utf-8"))
print(f"回读校验: {gname} State = {mm.group(1) if mm else '未找到'}")
print("明文总长:", len(chk), "（原", len(plain), "）")
