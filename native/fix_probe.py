# -*- coding: utf-8 -*-
p = 'native/src/probe.cpp'
s = open(p, encoding='utf-8').read()
bad = "#define RVA_ROAM_PATHMOVE 0x21854      /* placeholder, replaced below */\n#undef RVA_ROAM_PATHMOVE\n"
s = s.replace(bad, '')
s = s.replace('#define RVA_PATHMOVE_ROAM 0x2185A80', '#define RVA_PATHMOVE_ROAM 0x218779C')
open(p, 'w', encoding='utf-8').write(s)
print('patched')
for line in s.split('\n'):
    if 'PATHMOVE' in line or 'ROAM_PATH' in line:
        print('   ', line)
