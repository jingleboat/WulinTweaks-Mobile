import sys
from capstone import *

SO = r"F:\SteamLibrary\steamapps\common\WulinSH\GameAssembly.dll"
data = open(SO, "rb").read()
IMAGE_BASE = 0x180000000
PE_DELTA = 0x1400  # file offset = va - IMAGE_BASE - PE_DELTA
md = Cs(CS_ARCH_X86, CS_MODE_64)

def dis(va, size):
    off = va - IMAGE_BASE - PE_DELTA
    print(f"=== VA {va:#x} off {off:#x} size {size:#x} ===")
    for i in md.disasm(data[off:off + size], va):
        print(f"{i.address:012x}: {i.mnemonic:8} {i.op_str}")

if __name__ == "__main__":
    va = int(sys.argv[1], 16)
    size = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x60
    dis(va, size)
