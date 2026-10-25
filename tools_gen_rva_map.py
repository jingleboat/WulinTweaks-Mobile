#!/usr/bin/env python3
# -*- coding: utf-8 -*-
"""从 dump.cs 生成 RVA→名字 表（Il2CppDumper 的注释行里带 RVA）。

用法：python gen_rva_map.py <dump.cs> <out.txt>
行格式与 wulin-mod/tools/rva_map.txt 一致：`<rva_hex> <Namespace.Class::Method>`
"""
import re
import sys

CLS_RE = re.compile(r"^public (?:sealed |abstract |static )?(?:class|struct|enum|interface) ([\w`<>\[\], .]+)")
NS_RE = re.compile(r"^// Namespace: (.*)$")
RVA_RE = re.compile(r"// RVA: (0x[0-9A-Fa-f]+)")
SIG_RE = re.compile(r"^\t(?:public|private|protected|internal|static|virtual|override|sealed|abstract|\s)*[\w<>\[\],\?\. ]+ ([\w<>`]+)\(")


def main():
    src, out = sys.argv[1], sys.argv[2]
    ns = ""
    cls = ""
    prev_rva = None
    rows = []
    for line in open(src, encoding="utf-8", errors="replace"):
        m = NS_RE.match(line)
        if m:
            ns = m.group(1).strip()
            continue
        m = CLS_RE.match(line)
        if m:
            cls = m.group(1).strip()
            prev_rva = None
            continue
        m = RVA_RE.search(line)
        if m:
            prev_rva = m.group(1)
            continue
        if prev_rva and line.startswith("\tpublic") or (prev_rva and line.startswith("\tprivate")):
            mm = SIG_RE.match(line)
            if mm:
                name = mm.group(1)
                full = (ns + "." if ns and ns != "" else "") + cls.replace(" ", "") + "::" + name
                rows.append((int(prev_rva, 16), full))
                prev_rva = None
    rows.sort()
    with open(out, "w", encoding="utf-8") as fh:
        for rva, full in rows:
            fh.write("%X %s\n" % (rva, full))
    print("写出 %s：%d 条" % (out, len(rows)))


if __name__ == "__main__":
    main()
