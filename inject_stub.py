# -*- coding: utf-8 -*-
"""
把引导桩注入 libil2cpp.so（阶段 0：加载通道）
=============================================
做法（不改任何既有代码布局，只动一个字段 + 追加一个页面）：

  1. 在可执行段 RX（file 0x1CD2624..0x4BE07B0 / vaddr 0x1CD6624..0x4BE47B0）的**末尾**
     插入 0x1000 字节（全 0），把 RX 段的 filesz/memsz 各 +0x1000；
     因为插入量是 0x1000 的整数倍，后面所有段的 (p_offset % p_align) 关系保持不变。
  2. 插入区起始 vaddr = 0x4BE47B0，桩放在 0x4BE5000（页对齐，方便以后放带 adrp 的代码）。
     新段末尾 0x4BE57B0 与下一个 LOAD(0x4BE87B0) 之间还有 0x3000 余量，不会重叠。
  3. 把 `il2cpp_init` 的首条指令（stp x30,x19,[sp,#-0x10]!）替换成 B 到桩；
     桩里 dlopen("libwulin.so")，然后原样执行被偷走的那条指令，再 B 回 il2cpp_init+4。

所有关键地址都做前置校验，任何一项对不上就整体放弃。
"""
import argparse
import struct
import sys

# ---- 目标版本常量（grep 自 V1.2.0820B131 的 dump.cs / 动态符号表）
IL2CPP_INIT_VA = 0x1F66178      # il2cpp_init
DLOPEN_PLT_VA = 0x4BE3700       # dlopen 的 PLT 桩
STUB_IN_PAGE = 0x850            # 插入区内偏移：0x4BE47B0 + 0x850 = 0x4BE5000（页对齐）
INSERT_LEN = 0x4000   # 必须是 p_align(0x4000) 的整数倍，否则段对齐关系被破坏
# 三个回填位置不写死，改为在 stub.bin 里按占位指令定位（见 locate_slots）


def rd_u32(d, o):
    return struct.unpack_from('<I', d, o)[0]


def enc_b(pc, target_va):
    delta = target_va - (pc + 0)
    if delta % 4:
        raise ValueError('未对齐')
    imm = delta >> 2
    if not -(1 << 25) <= imm < (1 << 25):
        raise ValueError('超出 ±128MB: 0x%X' % delta)
    return 0x14000000 | (imm & 0x3FFFFFF)


def enc_bl(pc, target_va):
    delta = target_va - pc
    if delta % 4:
        raise ValueError('未对齐')
    imm = delta >> 2
    if not -(1 << 25) <= imm < (1 << 25):
        raise ValueError('超出 ±128MB: 0x%X' % delta)
    return 0x94000000 | (imm & 0x3FFFFFF)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--in', dest='src', required=True)
    ap.add_argument('--out', dest='dst', required=True)
    ap.add_argument('--stub', required=True, help='stub.bin（68 字节）')
    ap.add_argument('--lib', default='libwulin.so')
    ap.add_argument('--dry', action='store_true')
    a = ap.parse_args()

    d = bytearray(open(a.src, 'rb').read())
    stub = open(a.stub, 'rb').read()
    if len(stub) % 4 or len(stub) < 40:
        sys.exit('stub.bin 大小异常: %d' % len(stub))

    # ---------- 在桩里定位三个回填槽（不写死偏移）
    words = [rd_u32(stub, i) for i in range(0, len(stub), 4)]
    bl_idx = [k for k, w in enumerate(words) if w == 0x94000000]
    orig_idx = [k for k, w in enumerate(words) if w == 0x00000000]
    b_idx = [k for k, w in enumerate(words) if w == 0x14000000]
    if len(bl_idx) != 1 or len(orig_idx) != 1 or len(b_idx) != 1:
        sys.exit('桩占位符定位失败 bl=%s orig=%s b=%s' % (bl_idx, orig_idx, b_idx))
    STUB_BL_OFF = bl_idx[0] * 4
    STUB_ORIG_OFF = orig_idx[0] * 4
    STUB_B_OFF = b_idx[0] * 4
    STUB_LEN = len(stub)
    print('桩大小 %d B，回填槽 bl@+%d orig@+%d b@+%d'
          % (STUB_LEN, STUB_BL_OFF, STUB_ORIG_OFF, STUB_B_OFF))

    # ---------- ELF 头
    e_phoff = struct.unpack_from('<Q', d, 0x20)[0]
    e_phentsize = struct.unpack_from('<H', d, 0x36)[0]
    e_phnum = struct.unpack_from('<H', d, 0x38)[0]
    e_shoff = struct.unpack_from('<Q', d, 0x28)[0]
    e_shentsize = struct.unpack_from('<H', d, 0x3a)[0]
    e_shnum = struct.unpack_from('<H', d, 0x3c)[0]

    phs = []
    for i in range(e_phnum):
        o = e_phoff + i * e_phentsize
        t, fl, off, va, pa, fsz, msz, al = struct.unpack_from('<IIQQQQQQ', d, o)
        phs.append(dict(i=i, t=t, fl=fl, off=off, va=va, fsz=fsz, msz=msz, al=al))

    loads = [p for p in phs if p['t'] == 1]
    rx = None
    for p in loads:
        if p['fl'] & 1:                     # PF_X
            rx = p
            break
    if rx is None:
        sys.exit('找不到可执行 LOAD 段')
    ins_at = rx['off'] + rx['fsz']
    ins_va = rx['va'] + rx['fsz']
    print('RX 段: off 0x%08X..0x%08X  va 0x%08X..0x%08X  align 0x%X'
          % (rx['off'], ins_at, rx['va'], ins_va, rx['al']))
    if ins_at != 0x4BE07B0:
        sys.exit('插入点与预期不符: 0x%X != 0x4BE07B0' % ins_at)

    stub_va = ins_va + STUB_IN_PAGE
    if stub_va % 0x1000:
        sys.exit('桩地址未页对齐: 0x%X' % stub_va)
    nxt = min((p['va'] for p in loads if p['va'] > rx['va']), default=None)
    if nxt is None or ins_va + INSERT_LEN > nxt:
        sys.exit('新段会与下一个 LOAD 重叠（下一个 va=0x%X）' % (nxt or 0))
    print('桩 va = 0x%08X，新 RX 段尾 0x%08X，下一个 LOAD va 0x%08X（余量 0x%X）'
          % (stub_va, ins_va + INSERT_LEN, nxt, nxt - ins_va - INSERT_LEN))

    # ---------- il2cpp_init 校验
    i2_off = IL2CPP_INIT_VA - 0x4000        # RX 段 vaddr→file 偏移差值
    if not (rx['off'] <= i2_off < ins_at):
        sys.exit('il2cpp_init 不在 RX 文件区间内')
    orig = rd_u32(d, i2_off)
    print('il2cpp_init@0x%X off 0x%X 首指令 0x%08X' % (IL2CPP_INIT_VA, i2_off, orig))
    if orig != 0xA9BF4FFE:                  # stp x30,x19,[sp,#-0x10]!
        sys.exit('il2cpp_init 首指令指纹不符（期望 0xA9BF4FFE）')

    # ---------- 组装桩
    stub = bytearray(stub)
    struct.pack_into('<I', stub, STUB_BL_OFF, enc_bl(stub_va + STUB_BL_OFF, DLOPEN_PLT_VA))
    struct.pack_into('<I', stub, STUB_ORIG_OFF, orig)
    struct.pack_into('<I', stub, STUB_B_OFF, enc_b(stub_va + STUB_B_OFF, IL2CPP_INIT_VA + 4))
    print('桩: bl@+%d -> 0x%08X ; orig@+%d = 0x%08X ; b@+%d -> 0x%08X'
          % (STUB_BL_OFF, DLOPEN_PLT_VA, STUB_ORIG_OFF, orig, STUB_B_OFF, IL2CPP_INIT_VA + 4))

    if a.dry:
        print('dry-run，未写出')
        return

    # ---------- 插入页面
    d[ins_at:ins_at] = bytes(INSERT_LEN)
    d[ins_at + STUB_IN_PAGE:ins_at + STUB_IN_PAGE + STUB_LEN] = stub

    # ---------- 修程序头
    for p in phs:
        o = e_phoff + p['i'] * e_phentsize
        if p['i'] == rx['i']:
            struct.pack_into('<Q', d, o + 32, p['fsz'] + INSERT_LEN)   # p_filesz
            struct.pack_into('<Q', d, o + 40, p['msz'] + INSERT_LEN)   # p_memsz
        elif p['off'] >= ins_at:
            struct.pack_into('<Q', d, o + 8, p['off'] + INSERT_LEN)    # p_offset（是 +8，别写成 +16）

    # ---------- 修节头
    struct.pack_into('<Q', d, 0x28, e_shoff + INSERT_LEN)
    for i in range(e_shnum):
        o = e_shoff + INSERT_LEN + i * e_shentsize
        sh_type = rd_u32(d, o + 4)
        sh_off = struct.unpack_from('<Q', d, o + 24)[0]
        if sh_type != 8 and sh_off >= ins_at:          # SHT_NOBITS 没有文件偏移
            struct.pack_into('<Q', d, o + 24, sh_off + INSERT_LEN)

    # ---------- 装跳转
    struct.pack_into('<I', d, i2_off, enc_b(IL2CPP_INIT_VA, stub_va))

    open(a.dst, 'wb').write(bytes(d))
    print('输出 %s  (+0x%X B, 共 %d B)' % (a.dst, INSERT_LEN, len(d)))
    print('入口: il2cpp_init@0x%X  --B-->  桩@0x%X' % (IL2CPP_INIT_VA, stub_va))
    print('记得把 %s 一起放进 APK 的 lib/arm64-v8a/' % a.lib)


if __name__ == '__main__':
    main()
