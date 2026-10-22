# -*- coding: utf-8 -*-
"""
生成游戏内面板用的字形图集（native/src/glyphs.h）
=================================================
手机端没有可用的 CJK 动态字体（Unity 内置 LegacyRuntime.ttf 只有拉丁），
所以在构建期用 Windows 的微软雅黑把**面板上要显示的每个字**逐个栅格化成
8 位 alpha 位图，编进 libwulin.so。运行期只做查表 + 贴图，不依赖任何字体 API。

用法（要用带 Pillow 的 python）：
  & "C:\\Users\\User\\.dsh\\dsh-runtimes\\dsh-primary-runtime\\dependencies\\python\\python.exe" gen_glyphs.py
"""
import os
import sys

from PIL import Image, ImageDraw, ImageFont

FONT_PATH = r'C:\Windows\Fonts\msyh.ttc'
FONT_SIZE = 30
GW, GH = 32, 34          # 单字位图盒子（宽 / 高）

TITLE = '武林MOD'
ROWS = ['易物·作价恒占优',
        '易物·显示池子',
        '易物·确认搬空',
        '大地图解锁',
        '马车地图解锁',
        '剧情条件放宽',
        '可游泳']
ON = '开'
OFF = '关'

OUT = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'native', 'src', 'glyphs.h')


def cstr(s):
    """UTF-8 字节字面量，避免任何源文件编码问题。"""
    b = s.encode('utf-8')
    return '"' + ''.join('\\x%02x' % x for x in b) + '"'


def main():
    font = ImageFont.truetype(FONT_PATH, FONT_SIZE)
    chars = []
    for s in [TITLE] + ROWS + [ON, OFF]:
        for ch in s:
            if ch not in chars:
                chars.append(ch)
    print('唯一字符 %d 个：%s' % (len(chars), ''.join(chars)))

    advs = []
    bits = []
    for ch in chars:
        img = Image.new('L', (GW, GH), 0)
        d = ImageDraw.Draw(img)
        d.text((GW // 2, GH // 2), ch, font=font, fill=255, anchor='mm')
        bits.append(bytes(img.tobytes()))
        a = int(round(font.getlength(ch)))
        advs.append(max(6, min(GW, a)))

    with open(OUT, 'w', encoding='utf-8', newline='\n') as f:
        f.write('/* 自动生成：gen_glyphs.py —— 不要手改 */\n')
        f.write('#pragma once\n#include <stdint.h>\n\n')
        f.write('#define GLYPH_W %d\n#define GLYPH_H %d\n' % (GW, GH))
        f.write('#define GLYPH_COUNT %d\n\n' % len(chars))

        f.write('static const uint32_t G_CP[GLYPH_COUNT] = {\n')
        for i in range(0, len(chars), 8):
            f.write('    ' + ', '.join('0x%04X' % ord(c) for c in chars[i:i + 8]) + ',\n')
        f.write('};\n\n')

        f.write('static const unsigned char G_ADV[GLYPH_COUNT] = {\n')
        for i in range(0, len(advs), 16):
            f.write('    ' + ', '.join(str(x) for x in advs[i:i + 16]) + ',\n')
        f.write('};\n\n')

        f.write('static const unsigned char G_BITS[GLYPH_COUNT][GLYPH_W * GLYPH_H] = {\n')
        for i, b in enumerate(bits):
            f.write('  { /* %s */\n' % chars[i])
            for y in range(GH):
                row = b[y * GW:(y + 1) * GW]
                f.write('    ' + ','.join(str(x) for x in row) + ',\n')
            f.write('  },\n')
        f.write('};\n\n')

        f.write('static const char G_TITLE[] = %s;\n' % cstr(TITLE))
        f.write('static const char* const G_ROWS[%d] = {\n' % len(ROWS))
        for r in ROWS:
            f.write('    %s,\n' % cstr(r))
        f.write('};\n')
        f.write('#define G_ROW_COUNT %d\n' % len(ROWS))
        f.write('static const char G_ON[] = %s;\n' % cstr(ON))
        f.write('static const char G_OFF[] = %s;\n' % cstr(OFF))

    print('写出 %s  (%.1f KB)' % (OUT, os.path.getsize(OUT) / 1024.0))


if __name__ == '__main__':
    sys.exit(main())
