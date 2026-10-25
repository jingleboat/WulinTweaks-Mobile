/*
 * WulinTweaks · 水面可走 / 入水形态（仅限烤表地图）  —— v112 诊断版
 * ================================================================
 * 用户要求：只有 PC 版本来就能下水的那两个场景（map_504 / map_2160）开放水面，
 * 其它地图一律保持原样 —— 不放行、能站水面上这种副作用也不要有。
 *
 * 结论来源（已在设备上逐层验证）：
 *   1) 漫游里能不能走进一格只看 IsBlock（被挡 → Role.GetNoPathEnd，停在岸边）。
 *   2) 「站在水里」只有一条路：MapManager.IsSwimGrid 为真 → Role.CheckInBathWater
 *      → GameCharacterInstance.SetBathing(true) + RoamingManager.SetInBath(true)
 *      + GameUtil.CreateRoleGhost（水中倒影）+ Animator 参数 IsBath
 *      + 隐藏影子 + 移速换 CharMoveSpeedInWater。
 *   3) 手机版运行期地图实例里 `_SwimGrid` / `_SwimLadder` 两个图层根本不存在，
 *      所以 mSwimGrid 全零、IsSwimGrid 恒 false。PC 上能下水靠的就是 prefab 里那份数据。
 *
 * 因此本模块的水面判定**只认** water_mask.h（构建期从 APK 的地图 prefab 抽出 `_SwimGrid`）：
 * 504 = 760 格、2160 = 177 格、2169 = 0 格。没在表里的地图 → 一行代码都不干预。
 *
 * ------------------------------------------------------------------
 * v112 变更（只为查「手机端角色走不到水面上」）：
 *   ① MapEntity.IsBlock / MapEntity.IsSwimGrid 也挂一份 —— MapManager 那两个只是转调它们，
 *      有些调用方（寻路/建图）直接走 MapEntity，只挂 MapManager 可能整条路都拦不到。
 *   ② 四个钩子 + Role.CheckInBathWater 全部打点：只在烤表地图里、且只记前若干条，
 *      打印「被查询的坐标 / 原值 / 我们是否命中烤表」，用来区分三种情形：
 *        A 水面格从来没被查询过      → 能不能走不由 IsBlock 决定（A* 网格 / 碰撞体）
 *        B 查询了、也放行了但走不进  → 移动/碰撞另一条路
 *        C 烤表一张没命中            → 当前图 id 不对 / 坐标空间不对
 *   ③ 每秒打一行状态：当前图、F_WATER、各钩子调用/命中计数。
 * ------------------------------------------------------------------
 */
#include "common.h"
#include "hook.h"
#include "water.h"
#include "water_mask.h"

#include <dlfcn.h>
#include <time.h>
#include <string.h>

/* ---- v113：让指定 NPC 的落点被放进水域格 ----------------------------------
 * 依据（PC 端实测）：游戏进入水域图时会用 `GameConfig.GetConfigStr("ForceNPCSwim")`
 * 读这个环境常量字符串；把 NPC id 填进去，该 NPC 的落点会被安排到水域格里
 * （实测 10033 从陆地 (36,17) → 水域 (21,32)）。官方值为空 = 一个都不强制。
 *
 * 这里就是把这个「读」的结果换掉：名单写在下面一行，逗号分隔；留空 = 完全不干预。
 * 名单里的 id 必须是「角色池 uid」（例如 10033 叶云）。
 */
#define FORCE_SWIM_IDS "10033"

#define RVA_GC_GETCONFIGSTR 0x2271D50 /* string GameConfig.GetConfigStr(string) */

/* ---- RVA（国际版 V1.2.0820B131） ---- */
#define RVA_MM_ISBLOCK 0x2395B74 /* bool MapManager.IsBlock(Vector3Int)        —— 转调 MapEntity */
#define RVA_MM_ISSWIM 0x2395AB8  /* bool MapManager.IsSwimGrid(Vector3Int)     —— 转调 MapEntity */
#define RVA_ME_ISBLOCK 0x2388864 /* bool MapEntity.IsBlock(Vector3Int)         —— 真正查格子 */
#define RVA_ME_ISSWIM 0x2388684  /* bool MapEntity.IsSwimGrid(Vector3Int)      —— 真正查格子 */
#define RVA_CIW 0x2186B64        /* void Role.CheckInBathWater(Nullable<Vector3Int>) */

#define OFF_MM_CURMAP 0x94 /* MapManager.mCurMapID */
#define OFF_MM_BATTLE 0xC0 /* MapManager.mBattleState */
#define OFF_ROLE_GCI 0x20  /* Role.gameCharacterInstance */
#define OFF_GCI_TPL_UID 0xA0
#define OFF_ROLE_INBATH 0x169 /* Role.IsInBath（与 PC 一致，仅用于日志） */

typedef bool (*fn_block)(void*, uint64_t, uint32_t);
typedef void (*fn_ciw)(void*, uint64_t, uint32_t);

/* 入水形态开关（只对烤表地图生效，恒开；面板块控制的是 F_WATER，总闸） */
static int F_WATER_ANIM = 1;

/* ---- 诊断计数 ---- */
static int g_dbg = 1;              /* 1 = 打诊断（v112 默认开；正式版改 0） */
static int g_log_mm_block = 0, g_log_mm_swim = 0;
static int g_log_me_block = 0, g_log_me_swim = 0, g_log_ciw = 0;
static int g_n_mm_block = 0, g_n_mm_swim = 0, g_n_me_block = 0, g_n_me_swim = 0, g_n_ciw = 0;
static int g_hit_mm_block = 0, g_hit_mm_swim = 0, g_hit_me_block = 0, g_hit_me_swim = 0;
static time_t g_last_tick = 0;

/* ------------------------------------------------------------- 烤表查询 */
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

/* Vector3Int 在 ARM64 上：x1 = x | y<<32，x2 = z */
static inline int vec_x(uint64_t xy) { return (int)(uint32_t)(xy & 0xffffffffu); }
static inline int vec_y(uint64_t xy) { return (int)(uint32_t)(xy >> 32); }

/* 每秒一行状态（顺带把累计计数打出来） */
static void dbg_tick(int map) {
    if (!g_dbg) return;
    time_t now = time(nullptr);
    if (now == g_last_tick) return;
    g_last_tick = now;
    LOGI("[water] 状态 map=%d F_WATER=%d | MM.IsBlock 调%d/放行%d | MM.IsSwim 调%d/伪造%d | "
         "ME.IsBlock 调%d/放行%d | ME.IsSwim 调%d/伪造%d | CheckInBathWater 调%d",
         map, (int)F_WATER, g_n_mm_block, g_hit_mm_block, g_n_mm_swim, g_hit_mm_swim,
         g_n_me_block, g_hit_me_block, g_n_me_swim, g_hit_me_swim, g_n_ciw);
}

/* ------------------------------------------------------------------- hooks */
static fn_block orig_mm_block = nullptr, orig_mm_swim = nullptr;
static fn_block orig_me_block = nullptr, orig_me_swim = nullptr;
static fn_ciw orig_ciw = nullptr;

/* ---- MapManager.IsBlock ---- */
static bool hook_mm_isblock(void* self, uint64_t xy, uint32_t z) {
    bool r = orig_mm_block ? orig_mm_block(self, xy, z) : false;
    if (!self) return r;
    int map = *(int*)((char*)self + OFF_MM_CURMAP);
    dbg_tick(map);
    g_n_mm_block++;
    if (!F_WATER) return r;
    if (*(unsigned char*)((char*)self + OFF_MM_BATTLE)) return r;

    const WaterMaskEntry* e = mask_for(map);
    if (!e) return r;
    int x = vec_x(xy), y = vec_y(xy);
    int hit = mask_has(e, x, y);
    if (g_dbg && g_log_mm_block < 40) {
        g_log_mm_block++;
        LOGI("[water] MM.IsBlock(%d,%d) map=%d 原值=%d 烤表=%d%s", x, y, map, (int)r, hit,
             (!r && !hit) ? "  ← 原本就能走（不是我们放的）" : "");
    }
    if (!r) return r; /* 原本就能走：不是我们的活 */
    if (hit) { g_hit_mm_block++; return false; }
    return r;
}

/* ---- MapManager.IsSwimGrid ---- */
static bool hook_mm_isswim(void* self, uint64_t xy, uint32_t z) {
    bool r = orig_mm_swim ? orig_mm_swim(self, xy, z) : false;
    if (r) return true;
    if (!self) return false;
    int map = *(int*)((char*)self + OFF_MM_CURMAP);
    g_n_mm_swim++;
    if (!F_WATER || !F_WATER_ANIM) return false;
    if (*(unsigned char*)((char*)self + OFF_MM_BATTLE)) return false;

    const WaterMaskEntry* e = mask_for(map);
    if (!e) return false;
    int x = vec_x(xy), y = vec_y(xy);
    int hit = mask_has(e, x, y);
    if (g_dbg && g_log_mm_swim < 40) {
        g_log_mm_swim++;
        LOGI("[water] MM.IsSwimGrid(%d,%d) map=%d 原值=%d 烤表=%d", x, y, map, (int)r, hit);
    }
    if (hit) { g_hit_mm_swim++; return true; }
    return false;
}

/* 任意一张烤表里有没有这一格（MapEntity 层拿不到 mapid，用坐标反查即可：
   504/2160/2169 三张表的水域格不重叠） */
static int mask_any(int x, int y, int* which) {
    for (int i = 0; i < WATER_MASK_COUNT; ++i) {
        if (mask_has(&WATER_MASKS[i], x, y)) {
            if (which) *which = WATER_MASKS[i].mapid;
            return 1;
        }
    }
    return 0;
}

/* ---- MapEntity.IsBlock（真正查格子的那一层，v112 新挂） ---- */
static bool hook_me_isblock(void* self, uint64_t xy, uint32_t z) {
    bool r = orig_me_block ? orig_me_block(self, xy, z) : false;
    g_n_me_block++;
    if (!self) return r;
    int x = vec_x(xy), y = vec_y(xy);
    int which = -1;
    int hit = mask_any(x, y, &which);
    if (g_dbg && g_log_me_block < 40) {
        g_log_me_block++;
        LOGI("[water] ME.IsBlock(%d,%d) 原值=%d 烤表命中=%d(map=%d)%s", x, y, (int)r, hit, which,
             (!r && hit) ? "  ← 原本就能走" : "");
    }
    if (hit && r) { g_hit_me_block++; return false; }
    return r;
}

/* ---- MapEntity.IsSwimGrid ---- */
static bool hook_me_isswim(void* self, uint64_t xy, uint32_t z) {
    bool r = orig_me_swim ? orig_me_swim(self, xy, z) : false;
    if (r) return true;
    g_n_me_swim++;
    if (!self) return r;
    int x = vec_x(xy), y = vec_y(xy);
    int which = -1;
    int hit = mask_any(x, y, &which);
    if (g_dbg && g_log_me_swim < 40) {
        g_log_me_swim++;
        LOGI("[water] ME.IsSwimGrid(%d,%d) 原值=%d 烤表命中=%d(map=%d)", x, y, (int)r, hit, which);
    }
    if (hit) { g_hit_me_swim++; return true; }
    return r;
}

/* ---- GameConfig.GetConfigStr：把 ForceNPCSwim 换成我们的名单 ---- */
typedef void* (*fn_getstr)(void*, void*);
static fn_getstr orig_getstr = nullptr;
static int g_force_hits = 0;

static void* hook_getconfigstr(void* self, void* key) {
    void* r = orig_getstr ? orig_getstr(self, key) : nullptr;
    if (!FORCE_SWIM_IDS[0]) return r;
    if ((uintptr_t)key < 0x10000) return r;
    char buf[64];
    if (!str_to_buf(key, buf, sizeof(buf))) return r;
    if (strcmp(buf, "ForceNPCSwim") != 0) return r;

    void* s = A_string_new ? A_string_new(FORCE_SWIM_IDS) : nullptr;
    if (g_force_hits < 8) {
        g_force_hits++;
        char old[64] = {0};
        if ((uintptr_t)r > 0x10000) str_to_buf(r, old, sizeof(old));
        LOGI("[water] ForceNPCSwim 覆写：\"%s\" -> \"%s\"", old, FORCE_SWIM_IDS);
    }
    return ((uintptr_t)s > 0x10000) ? s : r;
}

/* ---- Role.CheckInBathWater（只观察，不干预） ---- */
static void hook_ciw(void* self, uint64_t xy, uint32_t zh) {
    g_n_ciw++;
    if (g_dbg && g_log_ciw < 60 && self) {
        g_log_ciw++;
        void* gci = *(void**)((char*)self + OFF_ROLE_GCI);
        int uid = 0;
        if ((uintptr_t)gci > 0x10000) uid = *(int*)((char*)gci + OFF_GCI_TPL_UID);
        int before = *(unsigned char*)((char*)self + OFF_ROLE_INBATH);
        LOGI("[water] CheckInBathWater uid=%d 实参格=(%d,%d) HasValue=%u InBath(前)=%d",
             uid, vec_x(xy), vec_y(xy), (unsigned)((zh >> 32) & 1), before);
    }
    if (orig_ciw) orig_ciw(self, xy, zh);
    if (g_dbg && g_log_ciw <= 60 && self) {
        int after = *(unsigned char*)((char*)self + OFF_ROLE_INBATH);
        LOGI("[water] CheckInBathWater → InBath(后)=%d", after);
    }
}

void water_install() {
    if (!g_i2cpp) g_i2cpp = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    for (int i = 0; i < WATER_MASK_COUNT; ++i) {
        LOGI("[water] 烤表 map=%d 水面格=%d（其它地图不干预）", WATER_MASKS[i].mapid, WATER_MASKS[i].count);
    }
    if (FORCE_SWIM_IDS[0]) LOGI("[water] ForceNPCSwim 名单 = \"%s\"（进入水域图时覆写配置读取）", FORCE_SWIM_IDS);

    if (hook_install("MapManager.IsBlock", (void*)(g_base + RVA_MM_ISBLOCK),
                     (void*)hook_mm_isblock, (void**)&orig_mm_block))
        LOGI("[water] 已挂 MapManager.IsBlock");
    else
        LOGE("[water] 挂 IsBlock 失败");

    if (hook_install("MapManager.IsSwimGrid", (void*)(g_base + RVA_MM_ISSWIM),
                     (void*)hook_mm_isswim, (void**)&orig_mm_swim))
        LOGI("[water] 已挂 MapManager.IsSwimGrid");
    else
        LOGE("[water] 挂 IsSwimGrid 失败");

    /* v112：同时挂 MapEntity 那一层（MapManager 只是转调它，有些调用方直接走这里） */
    if (hook_install("MapEntity.IsBlock", (void*)(g_base + RVA_ME_ISBLOCK),
                     (void*)hook_me_isblock, (void**)&orig_me_block))
        LOGI("[water] 已挂 MapEntity.IsBlock");
    else
        LOGE("[water] 挂 MapEntity.IsBlock 失败");

    if (hook_install("MapEntity.IsSwimGrid", (void*)(g_base + RVA_ME_ISSWIM),
                     (void*)hook_me_isswim, (void**)&orig_me_swim))
        LOGI("[water] 已挂 MapEntity.IsSwimGrid");
    else
        LOGE("[water] 挂 MapEntity.IsSwimGrid 失败");

    if (hook_install("Role.CheckInBathWater", (void*)(g_base + RVA_CIW),
                     (void*)hook_ciw, (void**)&orig_ciw))
        LOGI("[water] 已挂 Role.CheckInBathWater（只观察）");
    else
        LOGE("[water] 挂 CheckInBathWater 失败");

    /* v113：覆写 ForceNPCSwim，让名单里的 NPC 落进水域格 */
    if (hook_install("GameConfig.GetConfigStr", (void*)(g_base + RVA_GC_GETCONFIGSTR),
                     (void*)hook_getconfigstr, (void**)&orig_getstr))
        LOGI("[water] 已挂 GameConfig.GetConfigStr（ForceNPCSwim 覆写）");
    else
        LOGE("[water] 挂 GameConfig.GetConfigStr 失败");
}
