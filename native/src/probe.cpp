/*
 * WulinTweaks · 水面/阻挡诊断探针 v2（排查用，不进正式包）
 * ======================================================
 * v2 相比 v1 补的东西：
 *   1) 玩家格子改用 Player.get_Pos() 取值（同时打 0x1B4 原始值做交叉验证）
 *   2) 打 Role.IsInBath(0x171) / IsGhost(0x173)，看"在水里"状态到底有没有置上
 *   3) 挂 GameCharacterInstance.SetBathing(bool) 与 RoamingManager.SetInBath(bool)
 *      → 入水表现到底有没有被触发、触发成什么值
 *   4) 挂 Role.PathMoveOnRoam(...) → 点地寻路返回的路径长度与首尾格，看路径是否止步岸边
 *   5) 挂 MapManager.IsBlock(Vector3Int) → 只记录"被判定为阻挡、且该格是水面"的情况
 */
#include "common.h"
#include "hook.h"
#include "probe.h"

#include <pthread.h>
#include <unistd.h>

/* ---- RVA（国际版 V1.2.0820B131） ---- */
#define RVA_PFM_INIT_PATH 0x21C3038 /* List<Vector3Int> PathFindingManager.InitPathfindingData(MapEntity) */
#define RVA_BITDATA_GET 0x237CD48   /* int BitData.GetBitData(int) */
#define RVA_PLAYER_INST 0x21C3D3C   /* static Player Player.get_Instance() */
#define RVA_PLAYER_POS 0x21C3DDC    /* Vector3Int Player.get_Pos() */
#define RVA_ISBLOCK 0x2395B74       /* bool MapManager.IsBlock(Vector3Int) */
#define RVA_PATHMOVE_ROAM 0x218779C  /* void Role.PathMoveOnRoam(List<Vector3Int>, Action, Action<bool>) */
#define RVA_SET_BATHING 0x224A3E0    /* void GameCharacterInstance.SetBathing(bool) */
#define RVA_ROAM_SETINBATH 0x22C40DC /* void RoamingManager.SetInBath(bool) */

/* ---- 偏移 ---- */
#define OFF_ME_MAPID 0x10
#define OFF_ME_MINX 0x14
#define OFF_ME_MINY 0x18
#define OFF_ME_W 0x1C
#define OFF_ME_H 0x20
#define OFF_ME_BLOCK 0x48
#define OFF_ME_SWIM 0x58
#define OFF_ME_LADDER 0x60
#define OFF_PFM_LAND 0x30
#define OFF_PLAYER_CELL 0x1B4
#define OFF_ROLE_LASTPOS 0xC8
#define OFF_ROLE_INBATH 0x171
#define OFF_ROLE_ISGHOST 0x173
#define OFF_LIST_SIZE 0x18
#define OFF_LIST_DATA 0x10
#define OFF_ARR_LEN 0x18
#define OFF_ARR_DATA 0x20

typedef struct { int x, y, z; } V3I;

static void* g_map = nullptr;
static void* g_pfm = nullptr;
static int g_last_x = 0x7fffffff;
static int g_last_y = 0x7fffffff;
static volatile int g_blocklog_budget = 30;

static int bit(void* bd, int idx) {
    if (!bd) return -1;
    typedef int (*fn_bit)(void*, int);
    return ((fn_bit)(g_base + RVA_BITDATA_GET))(bd, idx);
}

static int cell_index(void* m, int x, int y) {
    if (!m) return -1;
    int minx = *(int*)((char*)m + OFF_ME_MINX);
    int miny = *(int*)((char*)m + OFF_ME_MINY);
    int w = *(int*)((char*)m + OFF_ME_W);
    int h = *(int*)((char*)m + OFF_ME_H);
    int lx = x - minx, ly = y - miny;
    if (lx < 0 || ly < 0 || lx >= w || ly >= h) return -1;
    return w * ly + lx;
}

static int swim_at(int x, int y) {
    if (!g_map) return -1;
    void* smap = *(void**)((char*)g_map + OFF_ME_SWIM);
    int idx = cell_index(g_map, x, y);
    return idx < 0 ? -1 : bit(smap, idx);
}

static int block_at(int x, int y) {
    if (!g_map) return -1;
    void* bmap = *(void**)((char*)g_map + OFF_ME_BLOCK);
    int idx = cell_index(g_map, x, y);
    return idx < 0 ? -1 : bit(bmap, idx);
}

static int in_landlist(int x, int y) {
    void* pfm = g_pfm;
    if (!pfm) return -1;
    void* lst = *(void**)((char*)pfm + OFF_PFM_LAND);
    if (!lst) return -1;
    void* arr = *(void**)((char*)lst + OFF_LIST_DATA);
    if (!arr) return -1;
    int n = *(int*)((char*)arr + OFF_ARR_LEN);
    if (n <= 0 || n > 200000) return -1;
    const int* p = (const int*)((char*)arr + OFF_ARR_DATA);
    for (int i = 0; i < n; ++i) {
        if (p[i * 3] == x && p[i * 3 + 1] == y) return 1;
    }
    return 0;
}

static void dump_map(const char* tag) {
    void* m = g_map;
    if (!m) { LOGI("[probe:%s] MapEntity 为空", tag); return; }
    int id = *(int*)((char*)m + OFF_ME_MAPID);
    int minx = *(int*)((char*)m + OFF_ME_MINX);
    int miny = *(int*)((char*)m + OFF_ME_MINY);
    int w = *(int*)((char*)m + OFF_ME_W);
    int h = *(int*)((char*)m + OFF_ME_H);
    void* bmap = *(void**)((char*)m + OFF_ME_BLOCK);
    void* smap = *(void**)((char*)m + OFF_ME_SWIM);
    void* lad = *(void**)((char*)m + OFF_ME_LADDER);
    int lad_n = lad ? *(int*)((char*)lad + OFF_LIST_SIZE) : -1;
    LOGI("[probe:%s] map=%d rect x=%d..%d y=%d..%d W=%d H=%d mBlock=%p mSwimGrid=%p swimLadder=%d",
         tag, id, minx, minx + w - 1, miny, miny + h - 1, w, h, bmap, smap, lad_n);
    if (!bmap || !smap || w <= 0 || h <= 0 || w > 4096 || h > 4096) {
        LOGI("[probe:%s] 网格对象缺失或尺寸异常，跳过扫描", tag);
        return;
    }
    int nblock = 0, nswim = 0, nswimfree = 0, fx = 0, fy = 0, havefirst = 0;
    for (int ly = 0; ly < h; ++ly) {
        for (int lx = 0; lx < w; ++lx) {
            int idx = w * ly + lx;
            int b = bit(bmap, idx), s = bit(smap, idx);
            if (b > 0) ++nblock;
            if (s > 0) {
                ++nswim;
                if (b <= 0) { ++nswimfree; if (!havefirst) { havefirst = 1; fx = minx + lx; fy = miny + ly; } }
            }
        }
    }
    LOGI("[probe:%s] block=%d swim=%d swim&free=%d first_free_water=(%d,%d) land=%d",
         tag, nblock, nswim, nswimfree, fx, fy, havefirst ? in_landlist(fx, fy) : -9);
}

/* ------------------------------------------------------- PFM.InitPathfindingData */
static void* (*orig_ipd)(void*, void*) = nullptr;
static void* hook_ipd(void* self, void* mapEntity) {
    g_pfm = self;
    g_map = mapEntity;
    void* r = orig_ipd ? orig_ipd(self, mapEntity) : nullptr;
    g_last_x = 0x7fffffff;
    g_last_y = 0x7fffffff;
    g_blocklog_budget = 30;
    dump_map("load");
    return r;
}

/* ------------------------------------------------------- MapManager.IsBlock */
static bool (*orig_isblock)(void*, uint64_t, uint32_t) = nullptr;
static bool hook_isblock(void* self, uint64_t xy, uint32_t z) {
    bool r = orig_isblock ? orig_isblock(self, xy, z) : false;
    if (r && g_map && g_blocklog_budget > 0) {
        int x = (int)(uint32_t)(xy & 0xffffffffu);
        int y = (int)(uint32_t)(xy >> 32);
        if (swim_at(x, y) > 0) {
            --g_blocklog_budget;
            LOGI("[probe:block] IsBlock(%d,%d)=true 但该格是水面(swim=1)", x, y);
        }
    }
    return r;
}

/* ------------------------------------------------------- 入水状态被触发了吗 */
static void (*orig_setbathing)(void*, bool) = nullptr;
static void hook_setbathing(void* self, bool v) {
    LOGI("[probe:bath] GameCharacterInstance.SetBathing(%d)", (int)v);
    if (orig_setbathing) orig_setbathing(self, v);
}

static void (*orig_setinbath)(void*, bool) = nullptr;
static void hook_setinbath(void* self, bool v) {
    LOGI("[probe:bath] RoamingManager.SetInBath(%d)", (int)v);
    if (orig_setinbath) orig_setinbath(self, v);
}

/* ------------------------------------------------------- 点地寻路返回的路径 */
static void (*orig_pathmove)(void*, void*, void*, void*) = nullptr;
static void hook_pathmove(void* self, void* list, void* endAction, void* posAction) {
    if (list && g_map) {
        void* arr = *(void**)((char*)list + OFF_LIST_DATA);
        int n = *(int*)((char*)list + OFF_LIST_SIZE);
        LOGI("[probe:path] PathMoveOnRoam 路径格数=%d", n);
        if (arr && n > 0 && n < 20000) {
            const int* p = (const int*)((char*)arr + OFF_ARR_DATA);
            int x0 = p[0], y0 = p[1];
            int xn = p[(n - 1) * 3], yn = p[(n - 1) * 3 + 1];
            LOGI("[probe:path]   起点(%d,%d) block=%d swim=%d | 终点(%d,%d) block=%d swim=%d land=%d",
                 x0, y0, block_at(x0, y0), swim_at(x0, y0),
                 xn, yn, block_at(xn, yn), swim_at(xn, yn), in_landlist(xn, yn));
        }
    }
    if (orig_pathmove) orig_pathmove(self, list, endAction, posAction);
}

/* ---------------------------------------------------------------- 轮询线程 */
static void* poller(void*) {
    sleep(12);
    while (1) {
        sleep(2);
        if (!g_base || !g_map) continue;
        typedef void* (*fn_inst)();
        void* pl = ((fn_inst)(g_base + RVA_PLAYER_INST))();
        if (!pl) continue;
        int rawx = *(int*)((char*)pl + OFF_PLAYER_CELL);
        int rawy = *(int*)((char*)pl + OFF_PLAYER_CELL + 4);
        typedef V3I (*fn_pos)(void*);
        V3I pc = ((fn_pos)(g_base + RVA_PLAYER_POS))(pl);
        if (pc.x == g_last_x && pc.y == g_last_y) continue;
        g_last_x = pc.x;
        g_last_y = pc.y;
        int inbath = *(unsigned char*)((char*)pl + OFF_ROLE_INBATH);
        int isghost = *(unsigned char*)((char*)pl + OFF_ROLE_ISGHOST);
        LOGI("[probe:cell] 站位(%d,%d)[raw %d,%d] block=%d swim=%d land=%d inBath=%d ghost=%d",
             pc.x, pc.y, rawx, rawy, block_at(pc.x, pc.y), swim_at(pc.x, pc.y),
             in_landlist(pc.x, pc.y), inbath, isghost);
    }
    return nullptr;
}

void probe_install() {
    if (hook_install("PFM.InitPathfindingData", (void*)(g_base + RVA_PFM_INIT_PATH),
                     (void*)hook_ipd, (void**)&orig_ipd))
        LOGI("[probe] 已挂 PFM.InitPathfindingData");
    else
        LOGE("[probe] 挂 InitPathfindingData 失败");

    if (hook_install("MapManager.IsBlock", (void*)(g_base + RVA_ISBLOCK),
                     (void*)hook_isblock, (void**)&orig_isblock))
        LOGI("[probe] 已挂 MapManager.IsBlock");
    else
        LOGE("[probe] 挂 IsBlock 失败");

    if (hook_install("GCI.SetBathing", (void*)(g_base + RVA_SET_BATHING),
                     (void*)hook_setbathing, (void**)&orig_setbathing))
        LOGI("[probe] 已挂 SetBathing");
    else
        LOGE("[probe] 挂 SetBathing 失败");

    if (hook_install("RoamingManager.SetInBath", (void*)(g_base + RVA_ROAM_SETINBATH),
                     (void*)hook_setinbath, (void**)&orig_setinbath))
        LOGI("[probe] 已挂 SetInBath");
    else
        LOGE("[probe] 挂 SetInBath 失败");

    if (hook_install("Role.PathMoveOnRoam", (void*)(g_base + RVA_PATHMOVE_ROAM),
                     (void*)hook_pathmove, (void**)&orig_pathmove))
        LOGI("[probe] 已挂 PathMoveOnRoam");
    else
        LOGE("[probe] 挂 PathMoveOnRoam 失败");

    pthread_t t;
    if (pthread_create(&t, nullptr, poller, nullptr) == 0) pthread_detach(t);
    LOGI("[probe] 轮询线程已起");
}
