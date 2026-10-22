import sys, struct, re
from capstone import Cs, CS_ARCH_X86, CS_MODE_64, x86_const

DLL = r"F:\SteamLibrary\steamapps\common\WulinSH\GameAssembly.dll"
DUMP = r"C:\Users\User\Documents\deepseek-harness\default-workspace\wulin-apk\pcdump\dump.cs"
data = open(DLL, 'rb').read()

e_lfanew = struct.unpack_from('<I', data, 0x3C)[0]
nsec = struct.unpack_from('<H', data, e_lfanew + 6)[0]
optsz = struct.unpack_from('<H', data, e_lfanew + 20)[0]
base = struct.unpack_from('<Q', data, e_lfanew + 24 + 24)[0]
sec = e_lfanew + 24 + optsz
secs = []
for i in range(nsec):
    o = sec + i * 40
    vsize, vaddr, rsize, raddr = struct.unpack_from('<IIII', data, o + 8)
    secs.append((vaddr, vsize, raddr, rsize))

def off2rva(off):
    for vaddr, vsize, raddr, rsize in secs:
        if raddr <= off < raddr + rsize:
            return vaddr + (off - raddr)
    return None

# build va->name index from dump.cs
name_by_va = {}
pat = re.compile(r'// RVA: 0x([0-9A-Fa-f]+) Offset: 0x([0-9A-Fa-f]+) VA: 0x([0-9A-Fa-f]+)')
cur_rva = None
with open(DUMP, 'r', encoding='utf-8', errors='replace') as f:
    for line in f:
        m = pat.search(line)
        if m:
            cur_rva = int(m.group(1), 16) + base
        elif line.strip().startswith('|-') and cur_rva:
            nm = line.strip()[2:]
            if name_by_va.get(cur_rva) is None:
                name_by_va[cur_rva] = nm

def name_of(va):
    n = name_by_va.get(va)
    if n:
        return n
    return ''

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True

def dis(off, count=120, label=''):
    rva = off2rva(off)
    print(f"===== {label} fileoff=0x{off:X} rva=0x{rva:X} va=0x{base+rva:X} =====")
    code = data[off:off + count * 10]
    n = 0
    for insn in md.disasm(code, base + rva):
        note = ''
        for op in insn.operands:
            if op.type == x86_const.X86_OP_IMM:
                t = op.imm
                nm = name_of(t) if t > base else ''
                note += f'   imm=0x{t:X} {nm}'
            elif op.type == x86_const.X86_OP_MEM and insn.reg_name(op.mem.base) == 'rip':
                t = insn.address + insn.size + op.mem.disp
                nm = name_of(t)
                note += f'   rip->0x{t:X} {nm}'
        print(f"0x{insn.address:X}  {insn.mnemonic} {insn.op_str}{note}")
        n += 1
        if n >= count:
            break

if __name__ == '__main__':
    for arg in sys.argv[1:]:
        dis(int(arg, 0), 120, arg)
