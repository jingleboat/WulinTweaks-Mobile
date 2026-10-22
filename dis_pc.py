import sys, struct
from capstone import Cs, CS_ARCH_X86, CS_MODE_64

DLL = r"F:\SteamLibrary\steamapps\common\WulinSH\GameAssembly.dll"
data = open(DLL, 'rb').read()

# PE sections -> rva->off
pe = data
e_lfanew = struct.unpack_from('<I', pe, 0x3C)[0]
nsec = struct.unpack_from('<H', pe, e_lfanew + 6)[0]
optsz = struct.unpack_from('<H', pe, e_lfanew + 20)[0]
base = struct.unpack_from('<Q', pe, e_lfanew + 24 + 24)[0]
sec = e_lfanew + 24 + optsz
secs = []
for i in range(nsec):
    o = sec + i * 40
    name = pe[o:o+8].rstrip(b'\0').decode()
    vsize, vaddr, rsize, raddr = struct.unpack_from('<IIII', pe, o + 8)
    secs.append((name, vaddr, vsize, raddr, rsize))

def rva2off(rva):
    for name, vaddr, vsize, raddr, rsize in secs:
        if vaddr <= rva < vaddr + max(vsize, rsize):
            return raddr + (rva - vaddr)
    return None

def off2rva(off):
    for name, vaddr, vsize, raddr, rsize in secs:
        if raddr <= off < raddr + rsize:
            return vaddr + (off - raddr)
    return None

md = Cs(CS_ARCH_X86, CS_MODE_64)
md.detail = True

def dis(off, count=200, label=''):
    rva = off2rva(off)
    print(f"===== {label} fileoff=0x{off:X} rva=0x{rva:X} va=0x{base+rva:X} =====")
    code = data[off:off + count * 8]
    n = 0
    for insn in md.disasm(code, base + rva):
        extra = ''
        for op in insn.operands:
            if op.type == 3:  # X86_OP_IMM
                extra += f'  ; ->0x{op.imm:X}'
            if op.type == 2 and insn.reg_name(op.mem.base) in ('rip',):
                tgt = insn.address + insn.size + op.mem.disp
                extra += f'  ; [rip]->0x{tgt:X}'
        print(f"0x{insn.address:X}  {insn.mnemonic} {insn.op_str}{extra}")
        n += 1
        if insn.mnemonic in ('ret',) and n > 3:
            break
        if n >= count:
            break

if __name__ == '__main__':
    for arg in sys.argv[1:]:
        off = int(arg, 0)
        dis(off, 200, arg)
