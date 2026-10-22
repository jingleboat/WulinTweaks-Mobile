# -*- coding: utf-8 -*-
"""探针：ELF 平台属性(BTI/PAC) + 目标函数前导字节。"""
import struct, sys

def load(p):
    d = open(p, 'rb').read()
    e_shoff = struct.unpack_from('<Q', d, 0x28)[0]
    e_shentsize = struct.unpack_from('<H', d, 0x3a)[0]
    e_shnum = struct.unpack_from('<H', d, 0x3c)[0]
    e_shstrndx = struct.unpack_from('<H', d, 0x3e)[0]
    secs = []
    for i in range(e_shnum):
        o = e_shoff + i * e_shentsize
        name, typ, flags, addr, off, size, link, info, align, ent = struct.unpack_from('<IIQQQQIIQQ', d, o)
        secs.append(dict(n=name, typ=typ, addr=addr, off=off, size=size, link=link))
    st = secs[e_shstrndx]
    for s in secs:
        e = d.index(b'\0', st['off'] + s['n'])
        s['s'] = d[st['off'] + s['n']:e].decode()
    return d, secs

d, secs = load(sys.argv[1] if len(sys.argv) > 1 else 'libil2cpp_intl.so')
by = {s['s']: s for s in secs}
print('sections:', [s['s'] for s in secs][:40])

# GNU property notes (BTI/PAC)
def notes(off, size):
    out = []
    p = off
    while p < off + size:
        namesz, descsz, ntyp = struct.unpack_from('<III', d, p)
        name = d[p + 12:p + 12 + namesz].rstrip(b'\0').decode(errors='replace')
        p2 = p + 12 + ((namesz + 3) & ~3)
        desc = d[p2:p2 + descsz]
        out.append((name, ntyp, desc.hex()))
        p = p2 + ((descsz + 3) & ~3)
    return out
for nm in ('.note.gnu.property', '.note.android.ident', '.note.gnu.build-id'):
    if nm in by:
        print(nm, notes(by[nm]['off'], by[nm]['size'])[:4])

def rva2off(rva):
    for s in secs:
        if s['s'] == '.text' and s['addr'] <= rva < s['addr'] + s['size']:
            return s['off'] + (rva - s['addr'])
    return None

TARGETS = {
    'UIWorldMapManager.IsMapEnable': 0x2578030,
    'UICabMapManager.IsPassIcon': 0x2420448,
    'TradingWithNpcManager.ValueOffset': 0x23C0AD8,
    'GameItemPack.GetTotalValue': 0x227F70C,
    'IsJoinedTeamCondition.CheckAllConnectNode': 0x3D936EC,
    'HavePartnerCondition.CheckAllConnectNode': 0x3D923FC,
    'ConditionNode.CheckAllConnectNode(base)': 0x3D7E2A0,
    'InteractiveObject.SetOutLine(bool)': 0x236F8AC,
    'MapManager.OnLoadMapComplated': 0x2391D64,
}
for k, rva in TARGETS.items():
    o = rva2off(rva)
    print('%-45s RVA 0x%08X off 0x%08X : %s' % (k, rva, o or 0,
          d[o:o + 16].hex(' ') if o else 'NOT-IN-TEXT'))
