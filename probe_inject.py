# -*- coding: utf-8 -*-
"""阶段0 探针：找 il2cpp_init、dlopen 的 PLT 桩、以及可用的代码洞（RX 段里的零填充）。"""
import struct, sys

SO = sys.argv[1] if len(sys.argv) > 1 else 'libil2cpp_intl_v2.so'
d = open(SO, 'rb').read()

e_phoff = struct.unpack_from('<Q', d, 0x20)[0]
e_phentsize = struct.unpack_from('<H', d, 0x36)[0]
e_phnum = struct.unpack_from('<H', d, 0x38)[0]
e_shoff = struct.unpack_from('<Q', d, 0x28)[0]
e_shentsize = struct.unpack_from('<H', d, 0x3a)[0]
e_shnum = struct.unpack_from('<H', d, 0x3c)[0]
e_shstrndx = struct.unpack_from('<H', d, 0x3e)[0]

ph = []
for i in range(e_phnum):
    o = e_phoff + i * e_phentsize
    p_type, p_flags, p_offset, p_vaddr, p_paddr, p_filesz, p_memsz, p_align = \
        struct.unpack_from('<IIQQQQQQ', d, o)
    ph.append(dict(type=p_type, flags=p_flags, off=p_offset, vaddr=p_vaddr,
                   filesz=p_filesz, memsz=p_memsz, align=p_align))

secs = []
for i in range(e_shnum):
    o = e_shoff + i * e_shentsize
    f = struct.unpack_from('<IIQQQQIIQQ', d, o)
    secs.append(dict(nm=f[0], typ=f[1], flags=f[2], addr=f[3], off=f[4],
                     size=f[5], link=f[6], info=f[7], align=f[8], ent=f[9]))
st = secs[e_shstrndx]
for s in secs:
    e = d.index(b'\0', st['off'] + s['nm'])
    s['s'] = d[st['off'] + s['nm']:e].decode()
S = {s['s']: s for s in secs}

print('== PT_LOAD 段')
for p in ph:
    if p['type'] == 1:
        print('  flags=%s off=0x%08X vaddr=0x%08X filesz=0x%08X memsz=0x%08X' %
              ('R' * (p['flags'] & 4 > 0) + 'W' * (p['flags'] & 2 > 0) + 'X' * (p['flags'] & 1 > 0),
               p['off'], p['vaddr'], p['filesz'], p['memsz']))

# ---- dynsym
ds, dst = S['.dynsym'], S['.dynstr']
syms = {}
n = ds['size'] // 24
for i in range(n):
    o = ds['off'] + i * 24
    nm, info, other, shndx, val, sz = struct.unpack_from('<IBBHQQ', d, o)
    e = d.index(b'\0', dst['off'] + nm)
    s = d[dst['off'] + nm:e].decode()
    if s:
        syms[i] = (s, shndx, val)
name2idx = {v[0]: k for k, v in syms.items()}

for want in ('il2cpp_init', 'il2cpp_domain_get'):
    i = name2idx.get(want)
    if i:
        s, shndx, val = syms[i]
        print('%s: idx=%d shndx=%d addr=0x%X' % (want, i, shndx, val))

# ---- .rela.plt -> GOT slot + symidx ; .plt 条目 16 字节
rp, plt = S['.rela.plt'], S['.plt']
cnt = rp['size'] // 24
print('== .rela.plt 条目 %d, .plt off 0x%X addr 0x%X size 0x%X' %
      (cnt, plt['off'], plt['addr'], plt['size']))
targets = {}
for k in range(cnt):
    o = rp['off'] + k * 24
    r_off, r_info, r_add = struct.unpack_from('<QQq', d, o)
    idx = r_info >> 32
    nm = syms.get(idx, ('?',))[0]
    slot_addr = plt['addr'] + 16 * (k + 1)   # .plt[0] 是 resolver
    if slot_addr >= plt['addr'] + plt['size']:
        slot_addr = None
    targets[nm] = (r_off, slot_addr)
for want in ('dlopen', 'dlsym', '__android_log_print', 'pthread_create'):
    if want in targets:
        got, slot = targets[want]
        print('%-22s GOT=0x%X  PLT桩=%s' % (want, got, ('0x%X' % slot) if slot else 'n/a'))

# ---- 代码洞：RX 段里连续 0x00
print('== RX 段零填充候选（>=96 字节）')
for p in ph:
    if p['type'] != 1 or not (p['flags'] & 1):
        continue
    seg = d[p['off']:p['off'] + p['filesz']]
    run = 0
    best = []
    for i, b in enumerate(seg):
        if b == 0:
            run += 1
            if run >= 96:
                start = p['off'] + i - run + 1
                best.append((start, run, p['vaddr'] + i - run + 1))
                run = 0
        else:
            run = 0
    for off, ln, va in best[:10]:
        # 判断它属于哪个 section
        owner = '?'
        for s in secs:
            if s['off'] <= off < s['off'] + s['size']:
                owner = s['s']
        print('  off=0x%08X va=0x%08X len>=%d  in %s' % (off, va, ln, owner))
