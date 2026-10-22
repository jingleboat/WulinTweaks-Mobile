#!/usr/bin/env python3
"""极简 UnityFS（AssetBundle）解包 + 文本检索工具。

用途：游戏的表/剧情等资源都在 LZ4 压缩的 bundle 里，直接 grep APK 只能命中未被压缩的碎片。
本脚本解析 UnityFS 头部、逐块解压（纯 Python 的 LZ4 block 解码，无第三方依赖），
拼出序列化文件后在解压后的内容里检索关键词并打印上下文。

用法：
  python tools_unityfs.py extract <bundle 路径> <输出文件>     # 解出解压后的序列化内容
  python tools_unityfs.py find    <bundle 路径> <关键词> [上下文=400]
  python tools_unityfs.py nodes   <bundle 路径>               # 打印内部资源路径列表
"""
import sys, struct, io, os


def lz4_block_decompress(src: bytes, expected: int) -> bytes:
    """LZ4 block format（Unity 用的是 liblz4 默认块，不带帧头）"""
    out = bytearray()
    i = 0
    n = len(src)
    while i < n:
        token = src[i]
        i += 1
        lit = token >> 4
        if lit == 15:
            while True:
                b = src[i]
                i += 1
                lit += b
                if b != 255:
                    break
        out += src[i:i + lit]
        i += lit
        if i >= n:
            break
        off = src[i] | (src[i + 1] << 8)
        i += 2
        mlen = token & 0x0F
        if mlen == 15:
            while True:
                b = src[i]
                i += 1
                mlen += b
                if b != 255:
                    break
        mlen += 4
        start = len(out) - off
        if start < 0:
            raise ValueError("LZ4 回溯越界")
        for k in range(mlen):
            out.append(out[start + k])
    if expected and len(out) != expected:
        # 长度不符时不报错，交给上层判断
        pass
    return bytes(out)


def _cstr(buf, pos):
    end = buf.index(b'\0', pos)
    return buf[pos:end].decode('utf-8', errors='replace'), end + 1


def parse(path):
    data = open(path, 'rb').read()
    pos = 0
    sig = data[pos:pos + 8]
    if sig[:7] != b'UnityFS':
        raise ValueError(f"不是 UnityFS 包: {sig!r}")
    pos = 8
    version = struct.unpack_from('>I', data, pos)[0]; pos += 4
    unity_ver, pos = _cstr(data, pos)
    unity_rev, pos = _cstr(data, pos)
    size = struct.unpack_from('>q', data, pos)[0]; pos += 8
    cbis = struct.unpack_from('>I', data, pos)[0]; pos += 4
    ubis = struct.unpack_from('>I', data, pos)[0]; pos += 4
    flags = struct.unpack_from('>I', data, pos)[0]; pos += 4

    if version >= 7:
        pos = (pos + 15) & ~15

    bi_raw = data[pos:pos + cbis]
    pos += cbis
    comp_type = flags & 0x3F
    if flags & 0x80:
        hash16 = bi_raw[:16]
        bi = lz4_block_decompress(bi_raw[16:], ubis - 16) if comp_type else bi_raw[16:]
    else:
        bi = lz4_block_decompress(bi_raw, ubis) if comp_type else bi_raw

    p = 0
    _hash = bi[p:p + 16]; p += 16
    block_count = struct.unpack_from('>I', bi, p)[0]; p += 4
    blocks = []
    for _ in range(block_count):
        usz, csz, bflags = struct.unpack_from('>IIH', bi, p); p += 10
        blocks.append((usz, csz, bflags))
    node_count = struct.unpack_from('>I', bi, p)[0]; p += 4
    nodes = []
    for _ in range(node_count):
        off, sz, nflags = struct.unpack_from('>qqI', bi, p); p += 20
        nm, p = _cstr(bi, p)
        nodes.append((off, sz, nflags, nm))

    # 数据区：紧接 blocksInfo 之后（UnityFS 只在头部之后做 16 字节对齐）
    dpos = pos
    total_csz = sum(b[1] for b in blocks)
    if dpos + total_csz != len(data):
        for cand in ((pos + 15) & ~15, (pos + 3) & ~3):
            if cand + total_csz == len(data):
                dpos = cand
                break

    out = bytearray()
    cur = dpos
    for usz, csz, bflags in blocks:
        chunk = data[cur:cur + csz]
        cur += csz
        ct = bflags & 0x3F
        if ct == 0:
            out += chunk
        elif ct in (1, 2, 3):
            out += lz4_block_decompress(chunk, usz)
        else:
            raise ValueError(f"不支持的压缩类型 {ct}")
    return {
        'version': version, 'unity': unity_ver, 'rev': unity_rev,
        'flags': flags, 'blocks': blocks, 'nodes': nodes, 'data': bytes(out)
    }


def main(argv):
    if len(argv) < 3:
        print(__doc__)
        return 1
    cmd = argv[1]
    pkg = parse(argv[2])
    if cmd == 'nodes':
        print(f"version={pkg['version']} unity={pkg['unity']} 解压后 {len(pkg['data'])} 字节, 资源 {len(pkg['nodes'])} 个")
        for off, sz, fl, nm in pkg['nodes']:
            print(f"  {off:>12} {sz:>10} {nm}")
        return 0
    if cmd == 'extract':
        open(argv[3], 'wb').write(pkg['data'])
        print(f"写出 {argv[3]} ({len(pkg['data'])} 字节)")
        return 0
    if cmd == 'find':
        needle = argv[3].encode('utf-8')
        ctx = int(argv[4]) if len(argv) > 4 else 400
        d = pkg['data']
        print(f"解压后 {len(d)} 字节，检索 {argv[3]}")
        cnt = 0
        start = 0
        while True:
            i = d.find(needle, start)
            if i < 0:
                break
            cnt += 1
            seg = d[max(0, i - ctx):i + ctx]
            txt = ''.join(c if (32 <= c < 127 or c > 127) else '.' for c in seg.decode('utf-8', errors='replace'))
            print(f"--- #{cnt} offset={i} ---")
            print(txt)
            start = i + 1
            if cnt >= 30:
                print("...")
                break
        if cnt == 0:
            print("未命中")
        return 0
    print(__doc__)
    return 1


if __name__ == '__main__':
    sys.exit(main(sys.argv))
