# -*- coding: utf-8 -*-
"""在 APK 原始字节流里搜关键词，并把命中偏移反查到 zip 条目（适用于 store 未压缩的 bundle）"""
import zipfile, sys, io, os, mmap, struct
sys.stdout = io.TextIOWrapper(sys.stdout.buffer, encoding="utf-8", errors="replace")

APK = r"E:\笔记\wulin_v49-Hold按条件来源压制-2026-10-21.apk"
kws = sys.argv[1:] or ["EJ-004"]

Z = zipfile.ZipFile(APK)
infos = Z.infolist()
# 只保留 store（compress_type == 0）的条目，压缩过的搜不到
store = [(i.header_offset, i.header_offset + i.compress_size, i.filename, i.compress_size)
         for i in infos if i.compress_type == 0]
store.sort()
print(f"store 条目 {len(store)} / 总 {len(infos)}")

# 建偏移索引，二分查
offs = [s[0] for s in store]

def which(off):
    import bisect
    i = bisect.bisect_right(offs, off) - 1
    if i < 0:
        return None
    s = store[i]
    if s[0] <= off < s[1]:
        return s
    return None

f = open(APK, "r+b")
mm = mmap.mmap(f.fileno(), 0, access=mmap.ACCESS_READ)
try:
    for kw in kws:
        for label, needle in (("utf8", kw.encode("utf-8")), ("utf16", kw.encode("utf-16-le"))):
            n = 0
            seen_files = set()
            pos = 0
            while True:
                i = mm.find(needle, pos)
                if i < 0:
                    break
                pos = i + 1
                n += 1
                s = which(i)
                if s and s[2] not in seen_files:
                    seen_files.add(s[2])
            print(f"\n{kw!r} ({label}) 命中 {n} 处，涉及 {len(seen_files)} 个文件")
            for x in sorted(seen_files)[:25]:
                print("   ", x)
finally:
    mm.close()
    f.close()
