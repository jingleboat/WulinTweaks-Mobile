# -*- coding: utf-8 -*-
"""列出 map_504 bundle 的 externals（依赖 bundle 名）。"""
import UnityPy

env = UnityPy.load('tmp_bundles/map_504.bundle')
for obj in env.objects:
    af = getattr(obj, 'assets_file', None)
    if af is None:
        continue
    print('assets_file type:', type(af))
    attrs = [a for a in dir(af) if 'extern' in a.lower()]
    print('extern attrs:', attrs)
    ext = getattr(af, 'externals', None)
    if ext:
        for i, e in enumerate(ext):
            print('  ', i, e)
    else:
        # 常见属性名兜底
        for a in ('_externals', 'externals_list'):
            v = getattr(af, a, None)
            if v:
                print(a, ':', v[:8])
    break
