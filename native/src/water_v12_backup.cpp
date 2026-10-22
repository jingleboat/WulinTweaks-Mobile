/*
 * WulinTweaks · 水面可走 / 入水形态
 * ================================
 * 调查结论（已在设备上逐层验证）：
 *   1) 漫游里能不能走进一格只看 MapManager.IsBlock；被挡就 GetNoPathEnd，人停在岸边。
 *   2) "站在水里"只有一条路：MapManager.IsSwimGrid 为真 → Role.CheckInBathWater
 *      → GameCharacterInstance.SetBathing(true) + RoamingManager.SetInBath(true)
 *      + GameUtil.CreateRoleGhost（水中倒影）+ Animator 参数 Bath/InWater/IsBath
 *      + 隐藏影子 + 移速换 CharMoveSpeedInWater。
 *   3) 手机版运行期地图实例里 `_SwimGrid` / `_SwimLadder` 两个图层**根本不存在**
 *      （实测运行期图层只有 24 个：SpecialLayer/Ground0-12/Wall/Building/Gateway/.../_Block），
 *      所以游戏自己的 IsSwimGrid 在手机版永远 false，mSwimGrid 位数 = 0。
 *      PC 上能"在水里"，靠的是 prefab 里那份 `_SwimGrid` 数据。
 *
 * 本模块：
 *   · water_mask.h（构建期由 gen_water_mask.py 从 APK 的地图 prefab 烤出来）
 *     提供 504 / 2160 两张图的水面格坐标表 —— 与 PC 数据完全一致；
 *   · hook MapManager.IsBlock     ：命中水面 → false（可走进水里）
 *   · hook MapManager.IsSwimGrid  ：命中水面 → true（触发游戏自己的入水状态）
 *   · 没有烤表的图，额外用"瓦片是 RuleTile_Water"放行 IsBlock（能踩水上，但不给入水形态，
 *     因为那份数据在手机版运行期已被剔除，强判会在陆地上误触发）。
 *   · 战斗态（MapManager.mBattleState）一律原样返回，避免影响战场寻路与 AI。
 */
#include "common.h"
#include "hook.h"
#include "water.h"
#include "water_mask.h"

#include <string.h>
#include <stdlib.h>
#include <dlfcn.h>

/* ---- RVA（国际版 V1.2.0820B131） ---- */
#define RVA_MM_ISBLOCK 0x2395B74  /* bool MapManager.IsBlock(Vector3Int) */
#define RVA_MM_ISSWIM 0x2395AB8   /* bool MapManager.IsSwimGrid(Vector3Int) */
#define RVA_MAP_GETINFO 0x238C644 /* MapEntity MapManager.GetMapInfo(int) */
#define RVA_TILE_GET 0x4B5A6A0    /* TileBase Tilemap.GetTile(Vector3Int) */

#define OFF_MM_CURMAP 0x94 /* MapManager.mCurMapID */
#define OFF_MM_BATTLE 0xC0 /* MapManager.mBattleState */
#define OFF_ME_OBJMAP 0x28
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

/* 入水形态开关（只对烤表地图生效） */
static int F_WATER_ANIM = 1;

/* ---- 烤表查询 ---- */
static const WaterMaskEntry* mask_for(int mapid) {
    for (int i = 0; i < WATER_MASK_COUNT; ++i) {
        if (WATER_MASKS[i].mapid == mapid) return &WATER_MASKS[i];
    }
    return nullptr;
}

static int mask_has(const WaterMaskEntry* e, int x, int y) {
    if (!e || e->count <= 0) return 0;
    int64_t key = ((int64_t)(uint32_t)y << 32) | (int64_t)(uint32_t)x;
    int lo = 0, hi = e->count - 1;
    while (lo <= hi) {
        int mid = (lo + hi) >> 1;
        if (e->keys[mid] == key) return 1;
        if (e->keys[mid] < key) lo = mid + 1; else hi = mid - 1;
    }
    return 0;
}

/* ---- 瓦片兜底（只给没有烤表的图用） ---- */
static void* g_me = nullptr;
static void* g_tm[MAX_TM];
static int g_ntm = 0;
static void* g_type_tilemap = nullptr;
static int g_tm_map = -1;
static unsigned char* g_cells = nullptr;
static int g_cw = 0, g_ch = 0, g_minx = 0, g_miny = 0;
static int g_log_block = 0, g_log_swim = 0;
typedef void* (*fn_obj_class)(void*);
typedef const char* (*fn_cls_name)(void*);
static fn_obj_class A_obj_class = nullptr;
static fn_cls_name A_cls_name = nullptr;

static void cells_free() {
    if (g_cells) { free(g_cells); g_cells = nullptr; }
    g_cw = g_ch = 0;
}

static void tile_scan_load(void* mgr, int mapid) {
    g_tm_map = mapid;
    cells_free();
    g_me = nullptr;
    g_ntm = 0;
    if (!g_type_tilemap) return;
    void* me = ((fn2i)(g_base + RVA_MAP_GETINFO))(mgr, mapid, nullptr);
    if (!me) return;
    g_me = me;
    int w = *(int*)((char*)me + OFF_ME_W);
    int h = *(int*)((char*)me + OFF_ME_H);
    g_minx = *(int*)((char*)me + OFF_ME_MINX);
    g_miny = *(int*)((char*)me + OFF_ME_MINY);
    if (w <= 0 || h <= 0 || w > 512 || h > 512) return;
    g_cw = w; g_ch = h;
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
}

static int tile_water_at(int x, int y) {
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
        if (A_obj_class && A_cls_name) {
            const char* nm = A_cls_name(A_obj_class(tile));
            if (nm && strstr(nm, "Water")) { hit = 1; break; }
        }
    }
    g_cells[idx] = hit ? 2 : 1;
    return hit;
}

/* ------------------------------------------------------------------- hooks */
static fn_block orig_isblock = nullptr;
static fn_block orig_isswim = nullptr;

static bool hook_isblock(void* self, uint64_t xy, uint32_t z) {
    bool r = orig_isblock ? orig_isblock(self, xy, z) : false;
    if (!r || !F_WATER || !self) return r;
    if (*(unsigned char*)((char*)self + OFF_MM_BATTLE)) return r;
    int mapid = *(int*)((char*)self + OFF_MM_CURMAP);
    int x = (int)(uint32_t)(xy & 0xffffffffu);
    int y = (int)(uint32_t)(xy >> 32);

    const WaterMaskEntry* e = mask_for(mapid);
    if (e && mask_has(e, x, y)) {
        if (g_log_block++ < 10) LOGI("[water] IsBlock(%d,%d)=true→false（烤表水面 map=%d）", x, y, mapid);
        return false;
    }
    if (!e) {
        if (mapid != g_tm_map) tile_scan_load(self, mapid);
        if (tile_water_at(x, y)) {
            if (g_log_block++ < 10) LOGI("[water] IsBlock(%d,%d)=true→false（瓦片判水 map=%d）", x, y, mapid);
            return false;
        }
    }
    return r;
}

static bool hook_isswim(void* self, uint64_t xy, uint32_t z) {
    bool r = orig_isswim ? orig_isswim(self, xy, z) : false;
    if (r) return true;
    if (!F_WATER || !F_WATER_ANIM || !self) return false;
    if (*(unsigned char*)((char*)self + OFF_MM_BATTLE)) return false;
    int mapid = *(int*)((char*)self + OFF_MM_CURMAP);
    const WaterMaskEntry* e = mask_for(mapid);
    if (!e) return false;
    int x = (int)(uint32_t)(xy & 0xffffffffu);
    int y = (int)(uint32_t)(xy >> 32);
    if (mask_has(e, x, y)) {
        if (g_log_swim++ < 10) LOGI("[water] IsSwimGrid(%d,%d)=true（入水形态 map=%d）", x, y, mapid);
        return true;
    }
    return false;
}

void water_install() {
    if (!g_i2cpp) g_i2cpp = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    if (g_i2cpp) {
        A_obj_class = (fn_obj_class)dlsym(g_i2cpp, "il2cpp_object_get_class");
        A_cls_name = (fn_cls_name)dlsym(g_i2cpp, "il2cpp_class_get_name");
    }
    void* kt = find_class("UnityEngine.Tilemaps", "Tilemap");
    g_type_tilemap = kt ? class_type(kt) : nullptr;

    for (int i = 0; i < WATER_MASK_COUNT; ++i) {
        LOGI("[water] 烤表 map=%d 水面格=%d", WATER_MASKS[i].mapid, WATER_MASKS[i].count);
    }

    if (hook_install("MapManager.IsBlock", (void*)(g_base + RVA_MM_ISBLOCK),
                     (void*)hook_isblock, (void**)&orig_isblock))
        LOGI("[water] 已挂 MapManager.IsBlock");
    else
        LOGE("[water] 挂 IsBlock 失败");

    if (hook_install("MapManager.IsSwimGrid", (void*)(g_base + RVA_MM_ISSWIM),
                     (void*)hook_isswim, (void**)&orig_isswim))
        LOGI("[water] 已挂 MapManager.IsSwimGrid");
    else
        LOGE("[water] 挂 IsSwimGrid 失败");
}
