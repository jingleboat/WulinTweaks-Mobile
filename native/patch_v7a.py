# -*- coding: utf-8 -*-
"""v7：关掉 IsSwimGrid 强制（先修误判），并加一次性的「图层/瓦片取样」诊断。"""
p = 'src/water.cpp'
s = open(p, encoding='utf-8').read()

# 1) 加一个内部开关：是否强制 IsSwimGrid（先关掉，避免陆地上也进入入水形态）
if 'F_WATER_ANIM' not in s:
    s = s.replace('static fn_block orig_isblock = nullptr;',
                  '/* 是否强制 IsSwimGrid=true（入水形态）。水面判定未校准前先关，只保留"能走进去" */\n'
                  'static int F_WATER_ANIM = 0;\n\n'
                  'static fn_block orig_isblock = nullptr;', 1)

s = s.replace('''    if (!F_WATER || !self) return false;
    if (*(unsigned char*)((char*)self + OFF_MM_BATTLE)) return false;''',
              '''    if (!F_WATER || !F_WATER_ANIM || !self) return false;
    if (*(unsigned char*)((char*)self + OFF_MM_BATTLE)) return false;''')

open(p, 'w', encoding='utf-8').write(s)
print('anim gate:', 'F_WATER_ANIM' in s)
