# -*- coding: utf-8 -*-
"""v10：MapEntity 换成「GetCurMapInfo 优先」，并在诊断里把三个来源都打印出来 + 各自的
水面位数，找出到底哪个对象才是当前活着的那份（之前拿到的很可能是已被 ClearData 清空的旧实体）。"""
p = 'src/water.cpp'
s = open(p, encoding='utf-8').read()

if 'RVA_CURMAP_GETINFO' not in s:
    s = s.replace('#define RVA_PFM_INIT_PATH 0x21C3038',
                  '#define RVA_CURMAP_GETINFO 0x238DEEC /* MapEntity MapManager.GetCurMapInfo() */\n'
                  '#define RVA_PFM_INIT_PATH 0x21C3038', 1)

# map_load：优先 GetCurMapInfo
s = s.replace('''    void* me = g_me_alt;
    if (!me) me = ((fn2i)(g_base + RVA_MAP_GETINFO))(mgr, mid, nullptr);
    if (!me) return;
    g_me = me;''',
              '''    void* me_cur = ((void* (*)(void*, void*))(g_base + RVA_CURMAP_GETINFO))(mgr, nullptr);
    void* me_byid = ((fn2i)(g_base + RVA_MAP_GETINFO))(mgr, mid, nullptr);
    void* me = me_cur ? me_cur : (me_byid ? me_byid : g_me_alt);
    LOGI("[water] entity 候选: GetCurMapInfo=%p GetMapInfo(%d)=%p InitPathfinding=%p -> 采用 %p",
         me_cur, mid, me_byid, g_me_alt, me);
    if (!me) return;
    g_me = me;''')

# 诊断里补一段：三个候选实体的水面位数
s = s.replace('''    LOGI("[water:diag] 对账 真值水格=%d 我们判定水格=%d 交集=%d 漏检=%d 误判=%d",''',
              '''    LOGI("[water:diag] 对账 真值水格=%d 我们判定水格=%d 交集=%d 漏检=%d 误判=%d",''')

# 在 diag_run 开头加候选枚举（需要 mgr）
s = s.replace('''static void diag_run(void) {
    if (g_diag_done) return;''',
              '''static int count_swim(void* me) {
    if (!me) return -1;
    void* sw = *(void**)((char*)me + OFF_ME_SWIM);
    if (!sw) return -2;
    int w = *(int*)((char*)me + OFF_ME_W), h = *(int*)((char*)me + OFF_ME_H);
    if (w <= 0 || h <= 0 || w > 512 || h > 512) return -3;
    int n = 0;
    for (int i = 0; i < w * h; ++i) if (bit_at(sw, i) > 0) ++n;
    return n;
}

static void diag_run(void) {
    if (g_diag_done) return;''')

s = s.replace('''    if (!any) { if (g_diag_tries++ < 12) LOGI("[water:diag] 水面位图还是空的(grid=%p)，继续等", sw); return; }''',
              '''    if (!any) {
        if (g_diag_tries++ < 12) {
            void* m2 = nullptr, *m3 = g_me_alt;
            if (g_mgr) m2 = ((fn2i)(g_base + RVA_MAP_GETINFO))(g_mgr, g_cur_map, nullptr);
            LOGI("[water:diag] 水面位图空(grid=%p) 当前实体 swim=%d | GetMapInfo实体=%p swim=%d | InitPathfinding实体=%p swim=%d",
                 sw, count_swim(g_me), m2, count_swim(m2), m3, count_swim(m3));
        }
        return;
    }''')

open(p, 'w', encoding='utf-8').write(s)
print('v10 patched')
