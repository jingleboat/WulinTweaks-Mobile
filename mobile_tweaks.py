# -*- coding: utf-8 -*-
"""
WulinTweaks · 手机端静态补丁（阶段 1）
=====================================
在 libil2cpp.so 上就地写入 arm64 指令，实现「不需要运行时的」那几项功能：

  ② 大地图解锁        UIWorldMapManager.IsMapEnable(int)      -> return true
  ② 马车地图解锁      UICabMapManager.IsPassIcon(int)         -> return true
  ③ 剧情条件放宽      IsJoinedTeamCondition.CheckAllConnectNode
                      HavePartnerCondition.CheckAllConnectNode
                      -> B 基类 ConditionNode.CheckAllConnectNode
  ④ 易物作价恒占优    TradingWithNpcManager.ValueOffset()      -> return 0

每条补丁都带「原始字节指纹」校验：与预期不符（游戏更新 / 版本不对）就整体放弃，绝不盲写。

用法：
  python -X utf8 mobile_tweaks.py --list
  python -X utf8 mobile_tweaks.py --in libil2cpp_intl_ui.so --out libil2cpp_intl_v2.so
  python -X utf8 mobile_tweaks.py --in ... --out ... --only map,story
"""
import argparse
import hashlib
import struct
import sys

# ---------------------------------------------------------------- 常量

# 目标游戏版本（决定下面所有偏移的有效性）
GAME_VERSION = 'V1.2.0820B131'
SO_SIZE = 88668240  # 作为兜底校验：大小不符直接拒绝

# 指令编码
MOV_W0_1 = bytes.fromhex('20008052')   # movz w0, #1
MOV_X0_0 = bytes.fromhex('000080d2')   # movz x0, #0  (注意 Rd 必须是 0，不能写成 XZR)
RET = bytes.fromhex('c0035fd6')        # ret

# 基类 ConditionNode.CheckAllConnectNode，③ 的跳转目标
STORY_BASE_RVA = 0x3D7E2A0


def b_insn(pc_rva, target_rva):
    """编码 arm64 无条件 B（±128 MB）。"""
    delta = target_rva - pc_rva
    if delta % 4:
        raise ValueError('跳转目标未对齐: %d' % delta)
    imm26 = delta >> 2
    if not -(1 << 25) <= imm26 < (1 << 25):
        raise ValueError('跳转超出 ±128MB: 0x%X' % delta)
    return struct.pack('<I', 0x14000000 | (imm26 & 0x3FFFFFF))


# 补丁表：(组, 名称, 描述, 文件内偏移, RVA, 原始指纹, 补丁字节)
PATCHES = [
    ('map', 'IsMapEnable',
     'UIWorldMapManager.IsMapEnable(int) -> true（大地图地点全可见/可前往）',
     0x2574030, 0x2578030,
     'fe57bea9f44f01a9',
     MOV_W0_1 + RET),

    ('map', 'IsPassIcon',
     'UICabMapManager.IsPassIcon(int) -> true（马车地图图标全部放开）',
     0x241C448, 0x2420448,
     'ef3bb96ded33016d',
     MOV_W0_1 + RET),

    ('story', 'IsJoinedTeamCondition',
     'IsJoinedTeamCondition.CheckAllConnectNode -> 基类实现（忽略「角色是否在队」）',
     0x3D8F6EC, 0x3D936EC,
     'fe5fbda9f65701a9',
     None),  # 运行期按 RVA 计算

    ('story', 'HavePartnerCondition',
     'HavePartnerCondition.CheckAllConnectNode -> 基类实现（忽略「是否有伙伴」）',
     0x3D8E3FC, 0x3D923FC,
     'fe5fbda9f65701a9',
     None),

    ('trade', 'ValueOffset',
     'TradingWithNpcManager.ValueOffset() -> 0（作价恒不亏，NPC 必接受）',
     0x23BCAD8, 0x23C0AD8,
     'fe67bca9f85f01a9',
     MOV_X0_0 + RET),

    ('trade', 'ConfirmEmptyGate',
     'ConfirmTrading 里「玩家交易区总价值 < 1 就拒绝」那条 b.lt 改成 nop（空手也能确认）',
     0x23BC7D4, 0x23C07D4,
     'eb080054',
     bytes.fromhex('1f2003d5')),   # nop
]


def sha256(b):
    return hashlib.sha256(b).hexdigest()


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('--in', dest='src')
    ap.add_argument('--out', dest='dst')
    ap.add_argument('--only', default='map,story,trade',
                    help='只打某些组：map,story,trade（逗号分隔，默认全部）')
    ap.add_argument('--list', action='store_true')
    ap.add_argument('--force', action='store_true', help='跳过指纹校验（危险）')
    args = ap.parse_args()

    only = {x.strip() for x in args.only.split(',') if x.strip()}

    if args.list:
        print('游戏版本 %s，共 %d 条静态补丁：' % (GAME_VERSION, len(PATCHES)))
        for grp, name, desc, off, rva, fp, pay in PATCHES:
            kind = 'B 跳转' if pay is None else ('%d 字节常量' % len(pay))
            print('  [%-5s] %-26s off=0x%08X rva=0x%08X  %-12s %s'
                  % (grp, name, off, rva, kind, desc))
        return

    if not (args.src and args.dst):
        ap.error('需要 --in 和 --out')

    d = bytearray(open(args.src, 'rb').read())
    print('输入: %s  (%d B, sha256 %s)' % (args.src, len(d), sha256(d)[:16]))
    if len(d) != SO_SIZE:
        print('!! 大小与预期不符（%d != %d），可能是别的版本，终止' % (len(d), SO_SIZE))
        if not args.force:
            sys.exit(2)

    changed = 0
    for grp, name, desc, off, rva, fp, pay in PATCHES:
        if grp not in only:
            continue
        cur = bytes(d[off:off + len(fp) // 2])
        if cur.hex() != fp and not args.force:
            print('!! %-26s 指纹不符：%s != %s，终止（未写入任何字节）'
                  % (name, cur.hex(), fp))
            sys.exit(3)
        if pay is None:
            pay = b_insn(rva, STORY_BASE_RVA)
            kind = 'B -> 基类'
        else:
            kind = '%d B' % len(pay)
        old = bytes(d[off:off + len(pay)])
        if old == pay:
            print('== %-26s 已是目标状态，跳过' % name)
            continue
        d[off:off + len(pay)] = pay
        changed += 1
        print('ok %-26s off 0x%08X  %s -> %s  (%s)'
              % (name, off, old.hex(), pay.hex(), kind))

    if not changed:
        print('没有任何改动')
    out = bytes(d)
    open(args.dst, 'wb').write(out)
    print('输出: %s  (%d B, sha256 %s)  改动 %d 处'
          % (args.dst, len(out), sha256(out)[:16], changed))


if __name__ == '__main__':
    main()
