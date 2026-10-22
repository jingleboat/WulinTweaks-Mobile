# -*- coding: utf-8 -*-
"""v8：诊断改成「延时 + 全图精确率统计」，不再在地图刚加载时取样（那时水面数据还没铺）。"""
p = 'src/water.cpp'
s = open(p, encoding='utf-8').read()

# 1) map_load 里不再调诊断
s = s.replace('         mid, w, h, g_minx, g_miny, g_ntm, g_cells ? "ok" : "null");\n    diag_layers();\n}',
              '         mid, w, h, g_minx, g_miny, g_ntm, g_cells ? "ok" : "null");\n}')

# 2) 用 diag_run 替换 diag_layers：先等水面位图铺好，再取样 + 全图对账
start = s.index('static void diag_layers(void) {')
end = s.index('static int water_at(int x, int y) {')
new_fn = r'''static int g_diag_done = 0;

static void diag_run(void) {
    if (g_diag_done || !g_me || !g_cells) return;
    void* sw = *(void**)((char*)g_me + OFF_ME_SWIM);
    if (!sw) return;

    /* 先确认水面位图已经铺好：至少有一格是水，否则说明地图还没初始化完 */
    int any = 0;
    for (int i = 0; i < g_cw * g_ch; ++i) {
        if (bit_at(sw, i) > 0) { any = 1; break; }
    }
    if (!any) return; /* 没铺好，等下一轮 */
    g_diag_done = 1;

    /* ---- 全图对账：游戏真值(_SwimGrid) vs 我们的瓦片判定 ---- */
    int nswim = 0, nour = 0, both = 0;
    for (int ly = 0; ly < g_ch; ++ly) {
        for (int lx = 0; lx < g_cw; ++lx) {
            int idx = ly * g_cw + lx;
            int s_ = bit_at(sw, idx) > 0;
            int u_ = water_at(g_minx + lx, g_miny + ly);
            if (s_) ++nswim;
            if (u_) ++nour;
            if (s_ && u_) ++both;
        }
    }
    LOGI("[water:diag] 对账 真值水格=%d 我们判定水格=%d 交集=%d 漏检=%d 误判=%d",
         nswim, nour, both, nswim - both, nour - both);

    /* ---- 取样：2 个真水格 + 2 个真陆地格，逐层打印瓦片信息 ---- */
    int nw = 0, nl = 0;
    for (int ly = 0; ly < g_ch && (nw < 2 || nl < 2); ++ly) {
        for (int lx = 0; lx < g_cw && (nw < 2 || nl < 2); ++lx) {
            int idx = ly * g_cw + lx;
            int isw = bit_at(sw, idx) > 0;
            if (isw && nw >= 2) continue;
            if (!isw && nl >= 2) continue;
            int x = g_minx + lx, y = g_miny + ly;
            if (isw) ++nw; else ++nl;
            uint64_t xy = ((uint64_t)(uint32_t)y << 32) | (uint64_t)(uint32_t)x;
            LOGI("[water:diag] %s格(%d,%d) 我们的判定=%d", isw ? "水" : "陆", x, y, water_at(x, y));
            for (int i = 0; i < g_ntm; ++i) {
                void* tile = ((fn_tile)(g_base + RVA_TILE_GET))(g_tm[i], xy, 0u, nullptr);
                if (!tile) continue;
                char ln[48] = "?", cn[64] = "?", an[64] = "?";
                obj_name(g_tm[i], ln, sizeof(ln));
                if (A_obj_class && A_cls_name) {
                    const char* c = A_cls_name(A_obj_class(tile));
                    if (c) snprintf(cn, sizeof(cn), "%s", c);
                }
                obj_name(tile, an, sizeof(an));
                LOGI("[water:diag]   层#%d %s : class=%s asset=%s", i, ln, cn, an);
            }
        }
    }
}

'''
s = s[:start] + new_fn + s[end:]

# 3) 原型 + 触发线程
s = s.replace('static void diag_layers(void);\n', 'static void diag_run(void);\n')

s = s.replace('''    if (hook_install("MapManager.IsSwimGrid", (void*)(g_base + RVA_MM_ISSWIM),''',
              '''    pthread_t t;
    if (pthread_create(&t, nullptr, diag_thread, nullptr) == 0) pthread_detach(t);

    if (hook_install("MapManager.IsSwimGrid", (void*)(g_base + RVA_MM_ISSWIM),''')

thread_fn = r'''
/* 诊断线程：等地图与水面位图都就绪后跑一次（地图刚加载时 _SwimGrid 还没铺） */
static void* diag_thread(void*) {
    for (int i = 0; i < 240; ++i) {
        sleep(1);
        if (g_diag_done) return nullptr;
        if (!g_me || !g_cells) continue;
        diag_run();
    }
    return nullptr;
}

'''
s = s.replace('void water_install() {', thread_fn + 'void water_install() {')

if '#include <pthread.h>' not in s:
    s = s.replace('#include <stdio.h>', '#include <stdio.h>\n#include <pthread.h>\n#include <unistd.h>')

# g_diag_done 复位：换图时重新诊断
s = s.replace('    g_water_hits = 0;', '    g_water_hits = 0;\n    g_diag_done = 0;')

open(p, 'w', encoding='utf-8').write(s)
print('patched v8')
print('diag_run in file:', 'diag_run' in s, '| thread:', 'diag_thread' in s)
