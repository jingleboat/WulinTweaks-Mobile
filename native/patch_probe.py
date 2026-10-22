# -*- coding: utf-8 -*-
p = 'src/wulin_main.cpp'
s = open(p, encoding='utf-8').read()

if 'probe.h' not in s:
    s = s.replace('#include "ui.h"', '#include "ui.h"\n#include "probe.h"', 1)

if 'WULIN_PROBE' not in s:
    s = s.replace('#define WULIN_PANEL 1',
                  '#define WULIN_PANEL 1\n\n'
                  '/* 排查探针：1 = 往 wulin.log 打水面/阻挡诊断，正式包改回 0 */\n'
                  '#define WULIN_PROBE 1', 1)

anchor = '    /* ---- 面板（最后装：它自己会建 UI） ---- */'
if 'probe_install()' not in s:
    s = s.replace(anchor, '#if WULIN_PROBE\n    probe_install();\n#endif\n\n' + anchor, 1)

open(p, 'w', encoding='utf-8').write(s)
print('patched ok')
