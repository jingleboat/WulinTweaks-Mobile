# -*- coding: utf-8 -*-
"""v11：把地图运行期图层名打出来 + mSwimLadder 数量 + 用离线已知的 504 水/陆坐标做样本对照。"""
p = 'src/water.cpp'
s = open(p, encoding='utf-8').read()

# 1) map_load 里打图层名与 swimLadder 数量
s = s.replace('''    LOGI("[water] map=%d %dx%d 原点(%d,%d) Tilemap层=%d 缓存=%s",
         mid, w, h, g_minx, g_miny, g_ntm, g_cells ? "ok" : "null");''',
              '''    void* lad = *(void**)((char*)me + 0x60);
    int lad_n = lad ? *(int*)((char*)lad + 0x18) : -1;
    LOGI("[water] map=%d %dx%d 原点(%d,%d) Tilemap层=%d swimLadder=%d 缓存=%s",
         mid, w, h, g_minx, g_miny, g_ntm, lad_n, g_cells ? "ok" : "null");
    {
        char names[512];
        int off = 0;
        names[0] = 0;
        for (int i = 0; i < g_ntm && off < (int)sizeof(names) - 24; ++i) {
            char ln[32] = "?";
            void* tr = ((fn1)(g_base + RVA_COMP_GET_TRANSFORM))(g_tm[i], nullptr);
            if (tr) str_to_buf(((void* (*)(void*, void*))(g_base + RVA_GET_NAME))(tr, nullptr), ln, sizeof(ln));
            off += snprintf(names + off, sizeof(names) - off, "%s%s", i ? "," : "", ln);
        }
        LOGI("[water] 运行期图层: %s", names);
    }''')

# 2) 用离线真值做样本对照（504 的水格/陆格坐标来自 prefab 的 _SwimGrid 位图）
s = s.replace('''    int nswim = 0, nour = 0, both = 0;''',
              '''    if (g_cur_map == 504) {
        static const int W5[3][2] = {{18, 18}, {19, 18}, {13, 23}};  /* prefab _SwimGrid 里的水格 */
        static const int L5[3][2] = {{0, 0}, {3, 3}, {30, 40}};      /* 陆地格 */
        for (int k = 0; k < 6; ++k) {
            int x = (k < 3) ? W5[k][0] : L5[k - 3][0];
            int y = (k < 3) ? W5[k][1] : L5[k - 3][1];
            uint64_t xy = ((uint64_t)(uint32_t)y << 32) | (uint64_t)(uint32_t)x;
            LOGI("[water:sample] %s格(%d,%d) 我们的判定=%d", (k < 3) ? "真水" : "真陆", x, y, water_at(x, y));
            for (int i = 0; i < g_ntm; ++i) {
                void* tile = ((fn_tile)(g_base + RVA_TILE_GET))(g_tm[i], xy, 0u, nullptr);
                if (!tile) continue;
                char ln[32] = "?", cn[64] = "?", an[64] = "?";
                void* tr = ((fn1)(g_base + RVA_COMP_GET_TRANSFORM))(g_tm[i], nullptr);
                if (tr) str_to_buf(((void* (*)(void*, void*))(g_base + RVA_GET_NAME))(tr, nullptr), ln, sizeof(ln));
                if (A_obj_class && A_cls_name) {
                    const char* c = A_cls_name(A_obj_class(tile));
                    if (c) snprintf(cn, sizeof(cn), "%s", c);
                }
                obj_name(tile, an, sizeof(an));
                LOGI("[water:sample]   %s class=%s asset=%s", ln, cn, an);
            }
        }
    }

    int nswim = 0, nour = 0, both = 0;''')

# 3) 水面位图为空时也照样跑（不再 return），让样本对照能出结果
s = s.replace('''        return;
    }

    /* ---- 全图对账''', '''        /* 位图为空也继续：下面还有「离线真值样本对照」能说明问题 */
    }

    /* ---- 全图对账''')

open(p, 'w', encoding='utf-8').write(s)
print('v11 patched')
