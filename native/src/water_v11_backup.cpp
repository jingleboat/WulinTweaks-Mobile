/*
 * WulinTweaks · 水面可走（水潭/水池入水）
 * ======================================
 * 为什么需要它（调查结论，v5 之前逐层验过）：
 *   1) 漫游里能不能走进一格，只看 MapManager.IsBlock —— Player.MoveOneGrid 与
 *      Role.StartSeek 都拿它做闸门（挡了就 GetNoPathEnd，人停在岸边）。
 *   2) 「站在水里」只有一条路：MapManager.IsSwimGrid 为真 → Role.CheckInBathWater
 *      → GameCharacterInstance.SetBathing(true) + RoamingManager.SetInBath +
 *      GameUtil.CreateRoleGhost（倒影）+ 隐藏影子 + 改 walkspeed。
 *   3) 但全包 418 张地图里只有 map_504 / map_2160 带 `_SwimGrid` 图层（2169 是空图层），
 *      其它地图（如 map_508 的水潭）压根没有水面数据 —— 所以水面只是贴图，
 *      进不去、也没有入水表现。
 *
 * 本模块的做法：
 *   · 水面格的判定不用地图数据，而是**直接问 Tilemap**：地图 prefab 里水用的瓦片是
 *     UnityEngine.RuleTile_Water，所以对目标格遍历地图下的所有 Tilemap 调
 *     Tilemap.GetTile(pos)，命中 RuleTile_Water 就是水。结果按图缓存（1=陆地 2=水），
 *     只在 IsBlock 已经为 true 的格子上做这次探测，代价很小。
 *   · hook MapManager.IsBlock  ：命中水面 → 返回 false（可走）
 *   · hook MapManager.IsSwimGrid：命中水面 → 返回 true（触发游戏自己的入水状态）
 *   · 战斗态（MapManager.mBattleState）一律不动，避免影响战场寻路/AI。
 *   · 受运行时开关 F_WATER 控制，面板「水面可走」一行可随时开关。
 */
#include "common.h"
#include "hook.h"
#include "water.h"

#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>
#include <stdio.h>
#include <pthread.h>
#include <unistd.h>

/* ---- RVA（国际版 V1.2.0820B131） ---- */
#define RVA_TILE_GET 0x4B5A6A0    /* TileBase Tilemap.GetTile(Vector3Int) */
#define RVA_MAP_GETINFO 0x238C644 /* MapEntity MapManager.GetMapInfo(int) */
#define RVA_MM_ISBLOCK 0x2395B74  /* bool MapManager.IsBlock(Vector3Int) */
#define RVA_MM_ISSWIM 0x2395AB8   /* bool MapManager.IsSwimGrid(Vector3Int) */
#define RVA_CURMAP_GETINFO 0x238DEEC /* MapEntity MapManager.GetCurMapInfo() */
#define RVA_PFM_INIT_PATH 0x21C3038 /* List<Vector3Int> PathFindingManager.InitPathfindingData(MapEntity) */

/* ---- 偏移 ---- */
#define OFF_MM_CURMAP 0x94 /* MapManager.mCurMapID */
#define OFF_MM_BATTLE 0xC0 /* MapManager.mBattleState */
#define OFF_ME_OBJMAP 0x28 /* MapEntity.objMap */
#define OFF_ME_MINX 0x14
#define OFF_ME_MINY 0x18
#define OFF_ME_W 0x1C
#define OFF_ME_H 0x20

#define MAX_TM 48

typedef void* (*fn1)(void*, void*);
typedef void* (*fn2i)(void*, int, void*);
typedef void* (*fn3)(void*, void*, void*);
typedef void* (*fn_tile)(void*, uint64_t, uint32_t, void*);
typedef bool (*fn_block)(void*, uint64_t, uint32_t);

typedef void* (*fn_obj_class)(void*);
typedef const char* (*fn_cls_name)(void*);

static fn_obj_class A_obj_class = nullptr;
static fn_cls_name A_cls_name = nullptr;

static void* g_mgr = nullptr;
static void* g_me = nullptr;
static void* g_me_alt = nullptr; /* InitPathfindingData 传入的实体（已初始化完的那种） */
static void* g_tm[MAX_TM];
static int g_ntm = 0;
static void* g_klass_water = nullptr;
static void* g_type_tilemap = nullptr;
static int g_cur_map = -1;
static unsigned char* g_cells = nullptr; /* 0 未知 / 1 陆地 / 2 水 */
static int g_cw = 0, g_ch = 0, g_minx = 0, g_miny = 0;
static int g_water_hits = 0;

/* ------------------------------------------------------------ 每张图的状态 */
static void cells_free() {
    if (g_cells) { free(g_cells); g_cells = nullptr; }
    g_cw = g_ch = 0;
}

static void diag_run(void);
static int g_diag_done;
static int water_at(int x, int y);

static void map_load(void* mgr) {
    g_mgr = mgr;
    int mid = *(int*)((char*)mgr + OFF_MM_CURMAP);
    g_cur_map = mid;
    cells_free();
    g_me = nullptr;
    g_ntm = 0;
    g_water_hits = 0;
    g_diag_done = 0;
    if (!g_type_tilemap) return;

    void* me_cur = ((void* (*)(void*, void*))(g_base + RVA_CURMAP_GETINFO))(mgr, nullptr);
    void* me_byid = ((fn2i)(g_base + RVA_MAP_GETINFO))(mgr, mid, nullptr);
    void* me = me_cur ? me_cur : (me_byid ? me_byid : g_me_alt);
    LOGI("[water] entity 候选: GetCurMapInfo=%p GetMapInfo(%d)=%p InitPathfinding=%p -> 采用 %p",
         me_cur, mid, me_byid, g_me_alt, me);
    if (!me) return;
    g_me = me;

    int w = *(int*)((char*)me + OFF_ME_W);
    int h = *(int*)((char*)me + OFF_ME_H);
    g_minx = *(int*)((char*)me + OFF_ME_MINX);
    g_miny = *(int*)((char*)me + OFF_ME_MINY);
    if (w <= 0 || h <= 0 || w > 512 || h > 512) return;
    g_cw = w;
    g_ch = h;
    g_cells = (unsigned char*)calloc((size_t)w * (size_t)h, 1);

    void* objMap = *(void**)((char*)me + OFF_ME_OBJMAP);
    if (!objMap) return;
    void* tr = ((fn1)(g_base + RVA_GO_GET_TRANSFORM))(objMap, nullptr);
    if (!tr) return;
    int n = ((int (*)(void*, void*))(g_base + RVA_T_CHILD_COUNT))(tr, nullptr);
    if (n > MAX_TM) n = MAX_TM;
    for (int i = 0; i < n; ++i) {
        void* child = ((fn2i)(g_base + RVA_T_GET_CHILD))(tr, i, nullptr);
        if (!child) continue;
        void* go = ((fn1)(g_base + RVA_COMP_GET_GO))(child, nullptr);
        if (!go) continue;
        void* comp = ((fn3)(g_base + RVA_GO_GET_COMPONENT))(go, g_type_tilemap, nullptr);
        if (comp) g_tm[g_ntm++] = comp;
    }
    void* lad = *(void**)((char*)me + 0x60);
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
    }
}

/* --------------------------------------------------- 诊断：图层 / 瓦片取样
 * 只有带 `_SwimGrid` 的地图（504 / 2160）才有游戏给的真值，拿它当标尺：
 * 取 3 个「游戏认定是水」的格 + 3 个「游戏认定是陆地」的格，
 * 逐层打印 图层名 / 瓦片类名 / 瓦片资产名，用来校准水面判定规则。
 */
#define OFF_ME_SWIM 0x58
#define RVA_BIT_GET 0x237CD48

static int bit_at(void* bd, int idx) {
    if (!bd) return 0;
    return ((int (*)(void*, int))(g_base + RVA_BIT_GET))(bd, idx);
}

static void obj_name(void* o, char* buf, int cap) {
    if (buf && cap > 0) buf[0] = 0;
    if (!o || !buf) return;
    void* s = ((void* (*)(void*, void*))(g_base + RVA_GET_NAME))(o, nullptr);
    str_to_buf(s, buf, cap);
}



static int g_diag_tries = 0;
static int g_block_calls = 0;

static int count_swim(void* me) {
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
    if (g_diag_done) return;
    if (!g_me || !g_cells) { if (g_diag_tries++ < 3) LOGI("[water:diag] 等待地图(entity=%p cache=%p)", g_me, g_cells); return; }
    void* sw = *(void**)((char*)g_me + OFF_ME_SWIM);
    if (!sw) { if (g_diag_tries++ < 6) LOGI("[water:diag] mSwimGrid 为空(entity=%p)", g_me); return; }

    /* 先确认水面位图已经铺好：至少有一格是水，否则说明地图还没初始化完 */
    int any = 0;
    for (int i = 0; i < g_cw * g_ch; ++i) {
        if (bit_at(sw, i) > 0) { any = 1; break; }
    }
    if (!any) {
        if (g_diag_tries++ < 12) {
            void* m2 = nullptr, *m3 = g_me_alt;
            if (g_mgr) m2 = ((fn2i)(g_base + RVA_MAP_GETINFO))(g_mgr, g_cur_map, nullptr);
            LOGI("[water:diag] 水面位图空(grid=%p) 当前实体 swim=%d | GetMapInfo实体=%p swim=%d | InitPathfinding实体=%p swim=%d",
                 sw, count_swim(g_me), m2, count_swim(m2), m3, count_swim(m3));
        }
        return;
    }
    g_diag_done = 1;

    /* ---- 全图对账：游戏真值(_SwimGrid) vs 我们的瓦片判定 ---- */
    if (g_cur_map == 504) {
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

static int water_at(int x, int y) {
    if (!g_cells) return 0;
    int lx = x - g_minx, ly = y - g_miny;
    if (lx < 0 || ly < 0 || lx >= g_cw || ly >= g_ch) return 0;
    int idx = ly * g_cw + lx;
    unsigned char v = g_cells[idx];
    if (v) return v == 2;

    int hit = 0;
    uint64_t xy = ((uint64_t)(uint32_t)y << 32) | (uint64_t)(uint32_t)x;
    for (int i = 0; i < g_ntm; ++i) {
        void* tile = ((fn_tile)(g_base + RVA_TILE_GET))(g_tm[i], xy, 0u, nullptr);
        if (!tile) continue;
        if (A_obj_class && g_klass_water) {
            if (A_obj_class(tile) == g_klass_water) { hit = 1; break; }
        }
        if (A_obj_class && A_cls_name) {
            const char* nm = A_cls_name(A_obj_class(tile));
            if (nm && strstr(nm, "Water")) { hit = 1; break; }
        }
    }
    g_cells[idx] = hit ? 2 : 1;
    return hit;
}

/* ------------------------------------------------------------------- hooks */
/* 是否强制 IsSwimGrid=true（入水形态）。水面判定未校准前先关，只保留"能走进去" */
static int F_WATER_ANIM = 0;

static fn_block orig_isblock = nullptr;
static fn_block orig_isswim = nullptr;

static bool hook_isblock(void* self, uint64_t xy, uint32_t z) {
    bool r = orig_isblock ? orig_isblock(self, xy, z) : false;
    if (!r || !F_WATER || !self) return r;
    if (*(unsigned char*)((char*)self + OFF_MM_BATTLE)) return r; /* 战斗态不动 */
    int mid = *(int*)((char*)self + OFF_MM_CURMAP);
    if (mid != g_cur_map || !g_cells) map_load(self);
    if (!g_diag_done && ++g_block_calls > 400) diag_run();
    int x = (int)(uint32_t)(xy & 0xffffffffu);
    int y = (int)(uint32_t)(xy >> 32);
    if (water_at(x, y)) {
        if (g_water_hits++ < 8) LOGI("[water] IsBlock(%d,%d)=true→false（水面放行）", x, y);
        return false;
    }
    return r;
}

static void* (*orig_pfm_init)(void*, void*) = nullptr;
static void* hook_pfm_init(void* self, void* mapEntity) {
    if (mapEntity) g_me_alt = mapEntity;
    void* r = orig_pfm_init ? orig_pfm_init(self, mapEntity) : nullptr;
    g_diag_done = 0;
    g_diag_tries = 0;
    g_block_calls = 0;
    return r;
}

static bool hook_isswim(void* self, uint64_t xy, uint32_t z) {
    bool r = orig_isswim ? orig_isswim(self, xy, z) : false;
    if (r) return true;
    if (!F_WATER || !F_WATER_ANIM || !self) return false;
    if (*(unsigned char*)((char*)self + OFF_MM_BATTLE)) return false;
    int mid = *(int*)((char*)self + OFF_MM_CURMAP);
    if (mid != g_cur_map || !g_cells) map_load(self);
    if (!g_diag_done && ++g_block_calls > 400) diag_run();
    int x = (int)(uint32_t)(xy & 0xffffffffu);
    int y = (int)(uint32_t)(xy >> 32);
    if (water_at(x, y)) return true;
    return false;
}


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

void water_install() {
    if (!g_i2cpp) g_i2cpp = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    if (g_i2cpp) {
        A_obj_class = (fn_obj_class)dlsym(g_i2cpp, "il2cpp_object_get_class");
        A_cls_name = (fn_cls_name)dlsym(g_i2cpp, "il2cpp_class_get_name");
    }
    void* kw = find_class("UnityEngine", "RuleTile_Water");
    g_klass_water = kw;
    void* kt = find_class("UnityEngine.Tilemaps", "Tilemap");
    g_type_tilemap = kt ? class_type(kt) : nullptr;
    LOGI("[water] RuleTile_Water=%p TilemapType=%p obj_class=%p cls_name=%p",
         kw, g_type_tilemap, (void*)A_obj_class, (void*)A_cls_name);

    if (hook_install("MapManager.IsBlock", (void*)(g_base + RVA_MM_ISBLOCK),
                     (void*)hook_isblock, (void**)&orig_isblock))
        LOGI("[water] 已挂 MapManager.IsBlock");
    else
        LOGE("[water] 挂 IsBlock 失败");

    if (hook_install("PFM.InitPathfindingData", (void*)(g_base + RVA_PFM_INIT_PATH),
                     (void*)hook_pfm_init, (void**)&orig_pfm_init))
        LOGI("[water] 已挂 PFM.InitPathfindingData（取 MapEntity）");
    else
        LOGE("[water] 挂 InitPathfindingData 失败");

    if (hook_install("MapManager.IsSwimGrid", (void*)(g_base + RVA_MM_ISSWIM),
                     (void*)hook_isswim, (void**)&orig_isswim))
        LOGI("[water] 已挂 MapManager.IsSwimGrid");
    else
        LOGE("[water] 挂 IsSwimGrid 失败");
}
