import sys
from capstone import *

SO = r"C:\Users\User\Documents\deepseek-harness\default-workspace\wulin-apk\libil2cpp.so"
# VA -> file offset delta for this binary (from Il2CppDumper output)
DELTA = 0x4000  # offset = va - delta

data = open(SO, "rb").read()
md = Cs(CS_ARCH_ARM64, CS_MODE_ARM)
md.detail = False

def dis(va, size):
    off = va - DELTA
    print(f"=== VA {va:#x} off {off:#x} size {size:#x} ===")
    for i in md.disasm(data[off:off + size], va):
        print(f"{i.address:08x}: {i.mnemonic:8} {i.op_str}")

if __name__ == "__main__":
    va = int(sys.argv[1], 16)
    size = int(sys.argv[2], 16) if len(sys.argv) > 2 else 0x200
    dis(va, size)
