# -*- coding: utf-8 -*-
"""一步到位：① 从 TagString 删掉指定标记 ② 把 R10049-0 重置成 NotOpen(0)
用法: python sv_fix_r10049.py <源.save> <输出.save>
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

src, dst = sys.argv[1], sys.argv[2]
plain, iv, gz = decrypt(open(src, "rb").read())
text = plain.decode("utf-8")

TAG = "海蛟帮贵客"
print("=" * 60)
print("① 删除标记", TAG)
n0 = text.count('"%s"' % TAG)
print("  出现次数:", n0)
if n0 == 1:
    i = text.find('"%s"' % TAG)
    print("  上下文:", repr(text[max(0, i - 100):i + 60]))
    # 中间元素形如 ,"TAG"  → 删掉（连同前导逗号）
    cand1 = ',"%s"' % TAG
    cand2 = '"%s",' % TAG
    if cand1 in text:
        text = text.replace(cand1, "", 1)
        print("  已按 ,\"TAG\" 删除")
    elif cand2 in text:
        text = text.replace(cand2, "", 1)
        print("  已按 \"TAG\", 删除")
    else:
        print("  !! 逗号形式都不匹配，未改")
else:
    print("  跳过（不是唯一一处，需人工确认）")

print("=" * 60)
print("② 重置 R10049-0 → NotOpen(0)")
pat = re.compile(r'"R10049-0":\{"State":(\d+)')
m = pat.search(text)
if not m:
    print("  !! 没找到 R10049-0")
else:
    print("  原 State =", m.group(1))
    text = pat.sub('"R10049-0":{"State":0', text, count=1)
    print("  新 State = 0")

out = encrypt(text.encode("utf-8"), iv, gz)
open(dst, "wb").write(out)
print("=" * 60)
print("已写出", dst, len(out), "字节")

chk, _, _ = decrypt(open(dst, "rb").read())
t2 = chk.decode("utf-8")
print("回读 标记剩余:", t2.count('"%s"' % TAG), "次")
m2 = pat.search(t2)
print("回读 R10049-0 State =", m2.group(1) if m2 else "未找到")
# TagString 完整性粗检
ts = re.search(r'"TagString":\[(.*?)\]', t2)
if ts:
    items = [x for x in ts.group(1).split('","')]
    print("TagString 元素数（粗算）:", len(items))
print("JSON 可解析性检查：", end="")
try:
    import json
    raw = re.sub(r'(?<=[{,])\s*(-?\d+)\s*:', lambda mm: '"%s":' % mm.group(1), t2)
    json.loads(raw)
    print("OK")
except Exception as e:
    print("失败 ->", e)
