# -*- coding: utf-8 -*-
"""v6：接入「水面可走」—— common.h 开关、main 安装、面板第 7 行、字形、构建脚本。"""
import io
import re

# ---------- common.h ----------
p = 'src/common.h'
s = open(p, encoding='utf-8').read()
if 'F_WATER' not in s:
    s = s.replace('extern volatile int F_STORY;        /* 剧情/队伍条件放宽 */',
                  'extern volatile int F_STORY;        /* 剧情/队伍条件放宽 */\n'
                  'extern volatile int F_WATER;        /* 水面可走（进水池 + 触发入水状态） */', 1)
    open(p, 'w', encoding='utf-8').write(s)
print('common.h F_WATER:', 'F_WATER' in open(p, encoding='utf-8').read())

# ---------- wulin_main.cpp ----------
p = 'src/wulin_main.cpp'
s = open(p, encoding='utf-8').read()
if '#include "water.h"' not in s:
    s = s.replace('#include "probe.h"', '#include "probe.h"\n#include "water.h"', 1)
if 'F_WATER' not in s:
    s = s.replace('volatile int F_STORY = 0;',
                  'volatile int F_STORY = 0;\nvolatile int F_WATER = 1;', 1)
if 'water_install()' not in s:
    s = s.replace('    /* ---- 大地图 / 马车 ---- */',
                  '    /* ---- 水面可走 ---- */\n    water_install();\n\n    /* ---- 大地图 / 马车 ---- */', 1)
open(p, 'w', encoding='utf-8').write(s)
s2 = open(p, encoding='utf-8').read()
print('main: include', '#include "water.h"' in s2, '| F_WATER', 'volatile int F_WATER' in s2,
      '| install', 'water_install();' in s2)

# ---------- ui.cpp ----------
p = 'src/ui.cpp'
s = open(p, encoding='utf-8').read()
s = s.replace('#define PH 440', '#define PH 500')
if 'case 6:' not in s:
    s = s.replace('        case 5: return &F_STORY;',
                  '        case 5: return &F_STORY;\n'
                  '        case 6: return &F_WATER;', 1)
open(p, 'w', encoding='utf-8').write(s)
s2 = open(p, encoding='utf-8').read()
print('ui: PH500', '#define PH 500' in s2, '| case6', 'case 6: return &F_WATER;' in s2)

# ---------- gen_glyphs.py ----------
p = '../gen_glyphs.py'
s = open(p, encoding='utf-8').read()
if '水面可走' not in s:
    s = s.replace("        '剧情条件放宽']", "        '剧情条件放宽',\n        '水面可走']", 1)
    open(p, 'w', encoding='utf-8').write(s)
print('gen_glyphs:', '水面可走' in open(p, encoding='utf-8').read())

# ---------- build.ps1 ----------
p = 'build.ps1'
s = open(p, encoding='utf-8').read()
if 'water.cpp' not in s:
    s = s.replace('src\\hook.cpp src\\probe.cpp', 'src\\hook.cpp src\\probe.cpp src\\water.cpp', 1)
    open(p, 'w', encoding='utf-8').write(s)
print('build.ps1:', 'water.cpp' in open(p, encoding='utf-8').read())
