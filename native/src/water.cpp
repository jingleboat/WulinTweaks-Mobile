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
#include <stdlib.h>   /* abs */

/* ---- v113：让指定 NPC 的落点被放进水域格 ----------------------------------
 * 依据（PC 端实测）：游戏进入水域图时会用 `GameConfig.GetConfigStr("ForceNPCSwim")`
 * 读这个环境常量字符串；把 NPC id 填进去，该 NPC 的落点会被安排到水域格里
 * （实测 10033 从陆地 (36,17) → 水域 (21,32)）。官方值为空 = 一个都不强制。
 *
 * 这里就是把这个「读」的结果换掉：名单写在下面一行，逗号分隔；留空 = 完全不干预。
 * 名单里的 id 必须是「角色池 uid」（例如 10033 叶云）。
 */
/* v121：用户指定的名单（uid）。已查：曼陀罗/洛千雪/华青青/叶银瓶/定海棠/曲忘忧/绛紫烟/凌梦蝶/韩红玉/
 * 福箐郡主/一枝花/哑琴/完颜昭宁/苗彩蝶/付瑶琴/顾倾城/鱼薇儿/唐婉儿/海棠秋/凤璇玑/月咏樱羽 + 纪茯苓(10136) + 花四娘(20025，待确认)
 * 严如蕊=20063（形象 255，有 Wash 动作 + 255w 贴图）
 * 丁小姐=20127 **不加入**：形象 269 没有 Wash 动作、也没有 269w 贴图，入了水也只能播走路 */
#define FORCE_SWIM_IDS "10021,10084,10092,10009,10039,10064,10056,10008,10045,10071,10094,10081,10028,10005,10075,10052,10098,10088,10108,10105,10049,10136,20025,20063"

#define RVA_GC_GETCONFIGSTR 0x2271D50 /* string GameConfig.GetConfigStr(string) */

/* ---- RVA（国际版 V1.2.0820B131） ---- */
#define RVA_MM_ISBLOCK 0x2395B74 /* bool MapManager.IsBlock(Vector3Int)        —— 转调 MapEntity */
#define RVA_MM_ISSWIM 0x2395AB8  /* bool MapManager.IsSwimGrid(Vector3Int)     —— 转调 MapEntity */
#define RVA_ME_ISBLOCK 0x2388864 /* bool MapEntity.IsBlock(Vector3Int)         —— 真正查格子 */
#define RVA_ME_ISSWIM 0x2388684  /* bool MapEntity.IsSwimGrid(Vector3Int)      —— 真正查格子 */
#define RVA_CIW 0x2186B64        /* void Role.CheckInBathWater(Nullable<Vector3Int>) */
/* v115：把名单里的 NPC 直接放到水域格上（手机端不读 ForceNPCSwim，只能在创建那一刻改落点） */
#define RVA_CRN_A 0x234EAD4      /* GameObject* CreateRoleMar.CreateNpc(GameCharacterInstance, Vector3Int, ...) */
#define RVA_CRN_B 0x234F4FC      /* 同上，另一个重载 */

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
static volatile int g_cur_map = -1;   /* 最近一次从 MapManager 读到的当前图，供落点覆盖用 */
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
static void force_poll(void);   /* 前置声明：IsBlock 钩子里驱动 */
static fn_block orig_mm_block = nullptr, orig_mm_swim = nullptr;
static fn_block orig_me_block = nullptr, orig_me_swim = nullptr;
static fn_ciw orig_ciw = nullptr;

/* ---- MapManager.IsBlock ---- */
static bool hook_mm_isblock(void* self, uint64_t xy, uint32_t z) {
    bool r = orig_mm_block ? orig_mm_block(self, xy, z) : false;
    if (!self) return r;
    int map = *(int*)((char*)self + OFF_MM_CURMAP);
    g_cur_map = map;
    force_poll();
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

/* 取当前地图的烤表。
 * ⚠️ v119 修 bug：以前 MapEntity 层用「任意表命中就算」（mask_any），
 * 结果在 2160 里，504 池子的坐标（如 (26,34)）也被当成了水域 —— 2160 凭空多出一大片水域。
 * 现在统一按 g_cur_map（从 MapManager 钩子缓存的当前图 id）取表。 */
static const WaterMaskEntry* cur_mask(void) {
    if (g_cur_map <= 0) return nullptr;
    return mask_for(g_cur_map);
}

/* ---- MapEntity.IsBlock（真正查格子的那一层，v112 新挂） ---- */
static bool hook_me_isblock(void* self, uint64_t xy, uint32_t z) {
    bool r = orig_me_block ? orig_me_block(self, xy, z) : false;
    g_n_me_block++;
    if (!self) return r;
    int x = vec_x(xy), y = vec_y(xy);
    const WaterMaskEntry* e = cur_mask();
    int hit = (e && mask_has(e, x, y)) ? 1 : 0;
    if (g_dbg && g_log_me_block < 40) {
        g_log_me_block++;
        LOGI("[water] ME.IsBlock(%d,%d) map=%d 原值=%d 烤表命中=%d%s", x, y, g_cur_map, (int)r, hit,
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
    const WaterMaskEntry* e = cur_mask();
    int hit = (e && mask_has(e, x, y)) ? 1 : 0;
    if (g_dbg && g_log_me_swim < 40) {
        g_log_me_swim++;
        LOGI("[water] ME.IsSwimGrid(%d,%d) map=%d 原值=%d 烤表命中=%d", x, y, g_cur_map, (int)r, hit);
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
        LOGI("[water] CheckInBathWater uid=%d", uid);
    }
    if (orig_ciw) orig_ciw(self, xy, zh);
}

/* 名单解析：逗号/空格分隔的 uid */
static int uid_in_force_list(int uid) {
    if (!FORCE_SWIM_IDS[0] || uid <= 0) return 0;
    const char* p = FORCE_SWIM_IDS;
    while (*p) {
        while (*p == ',' || *p == ' ') ++p;
        if (!*p) break;
        int v = 0, neg = 0;
        if (*p == '-') { neg = 1; ++p; }
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
        if (neg) v = -v;
        if (v == uid) return 1;
    }
    return 0;
}

/* 在烤表里找离 (x,y) 最近的水域格（曼哈顿距离） */
static int pick_water_cell(const WaterMaskEntry* e, int x, int y, int* ox, int* oy) {
    if (!e || e->count <= 0) return 0;
    int best = 0x7fffffff, bx = 0, by = 0;
    for (int i = 0; i < e->count; ++i) {
        int64_t k = e->keys[i];
        int cx = (int)(uint32_t)(k & 0xffffffffu);
        int cy = (int)(uint32_t)(k >> 32);
        int d = abs(cx - x) + abs(cy - y);
        if (d < best) { best = d; bx = cx; by = cy; }
    }
    *ox = bx; *oy = by;
    return 1;
}

#define RVA_GCI_SETPOS 0x2245B58   /* void SetPos(Vector3Int, bool=true) —— 直接调用（钩子跳板构造失败） */
#define RVA_GCI_SETMAPID 0x2245B48 /* void SetMapId(int mapId, bool alsoChangeTarget=true) */
typedef void (*fn_setpos)(void*, uint64_t, uint32_t, int);
static fn_setpos orig_setpos = nullptr;

/* v120：SetPos 的跳板构造不出来（日志 "hook GameCharacterInstance.SetPos: 跳板构造失败"），
 * 改为挂同族的 SetMapId —— 角色上图/换图时一定会调它；我们只观察，动手在它之后直接调 SetPos。 */

typedef void (*fn_setmapid)(void*, int, int);
static fn_setmapid orig_setmapid = nullptr;
static int g_mapid_hits = 0;

static void hook_setmapid(void* self, int map_id, int also) {
    if (orig_setmapid) orig_setmapid(self, map_id, also);
    if (!FORCE_SWIM_IDS[0] || (uintptr_t)self < 0x10000) return;

    int uid = *(int*)((char*)self + OFF_GCI_TPL_UID);
    int all = (strcmp(FORCE_SWIM_IDS, "all") == 0);
    if (!all && !uid_in_force_list(uid)) return;

    const WaterMaskEntry* e = mask_for(map_id);
    if (!e || e->count <= 0) return;

    /* 当前坐标：SetPos 之后游戏自己知道，这里只想判断"是否已经在某张表的水格里"，
       保守做法——无条件挪到离 (0,0) 最近的入口不行，改成：先读 gci 里的位置字段不可靠，
       所以直接给一个「按 map 取的中心水域格」= 表里排序后的中间那个，位置稳定且一定在水里。 */
    int64_t k = e->keys[e->count / 2];
    int wx = (int)(uint32_t)(k & 0xffffffffu);
    int wy = (int)(uint32_t)(k >> 32);

    if (g_mapid_hits++ < 12)
        LOGI("[water] SetMapId 强制定位 uid=%d map=%d -> 水域 (%d,%d)", uid, map_id, wx, wy);

    if (orig_setpos)
        orig_setpos(self, ((uint64_t)(uint32_t)wx) | ((uint64_t)(uint32_t)wy << 32), 0, 1);
}

/* ---- v119：名单内角色 SetPos 时，把坐标换成当前地图烤表里最近的水域格 ----
 * 手机端不读 ForceNPCSwim（v113 实测零调用），CreateNpc 的参数表又太长（v115 因此闪退），
 * 所以改钩这个签名最简单的：GameCharacterInstance.SetPos(Vector3Int pos, bool alsoChangeTarget)。
 */
static int g_pos_hits = 0;

static void hook_setpos(void* self, uint64_t xy, uint32_t z, int also) {
    if (FORCE_SWIM_IDS[0] && (uintptr_t)self > 0x10000) {
        int uid = *(int*)((char*)self + OFF_GCI_TPL_UID);
        if (uid_in_force_list(uid)) {
            const WaterMaskEntry* e = mask_for(g_cur_map);
            int x = vec_x(xy), y = vec_y(xy);
            if (e && !mask_has(e, x, y)) {
                int ox = 0, oy = 0;
                if (pick_water_cell(e, x, y, &ox, &oy)) {
                    xy = ((uint64_t)(uint32_t)ox) | ((uint64_t)(uint32_t)oy << 32);
                    z = 0;
                    if (g_pos_hits++ < 12)
                        LOGI("[water] SetPos 强制定位 uid=%d map=%d (%d,%d) -> 水域 (%d,%d)",
                             uid, g_cur_map, x, y, ox, oy);
                }
            }
        }
    }
    if (orig_setpos) orig_setpos(self, xy, z, also);
}

/* ---- v121：轮询强制定位 ----------------------------------------------------
 * 理由：SetPos(0x2245B58) 与 SetMapId(0x2245B48) 的**跳板都构造失败**（日志实证），
 * 这两个函数钩不上；而 CreateNpc 的参数表太长（v115 因此闪退）。
 * 所以改成不用钩子：借 MapManager.IsBlock（每帧都调、已挂）做节拍，每 500ms 查一遍名单：
 *   GetCharacter(uid) → 是否在当前图 → CurPos 是否水域格 → 不是就 SetPos(最近的烤表水域格)。
 */
#define RVA_GCM_GETCHAR  0x2263114  /* GameCharacterInstance GameCharacterManager.GetCharacter(int) */
#define RVA_GCI_CUR_MAP  0x223C1A8  /* int GameCharacterInstance.get_CurMapId() */
#define RVA_GCI_CUR_POS  0x223C1B8  /* Vector3Int GameCharacterInstance.get_CurPos() */
#define RVA_ROLE_NPCTP   0x2184F80  /* void Role.NpcTP(Vector3Int pos) —— 真正把角色传送到某格 */

static void* g_gcm = nullptr;
static time_t g_last_poll = 0;
static int g_force_hits2 = 0;

static void* gcm(void) {
    if (g_gcm && (uintptr_t)g_gcm > 0x10000) return g_gcm;
    void* k = find_class("WuLin", "GameCharacterManager");
    void* inst = k ? find_object_of_type(k) : nullptr;
    g_gcm = inst;
    return inst;
}

/* 枚举场景里所有 Role，按 uid 找她的 Role —— SetPos 只改 GameCharacterInstance 的数据，
 * 画面上不动；要真挪人必须拿 Role 对象调 NpcTP。 */
static void* role_of_uid(int uid) {
    void* k = find_class("WuLin", "Role");
    if (!k) return nullptr;
    void* t = class_type(k);
    if (!t) return nullptr;
    typedef void* (*fn_all)(void*, void*);
    void* arr = ((fn_all)(g_base + RVA_FIND_OBJECTS_ALL))(t, nullptr);
    if ((uintptr_t)arr < 0x10000) return nullptr;
    int n = *(int*)((char*)arr + 0x18);
    if (n <= 0 || n > 4000) return nullptr;
    for (int i = 0; i < n; ++i) {
        void* r = *(void**)((char*)arr + 0x20 + (size_t)i * 8);
        if ((uintptr_t)r < 0x10000) continue;
        void* gci = *(void**)((char*)r + OFF_ROLE_GCI);
        if ((uintptr_t)gci < 0x10000) continue;
        if (*(int*)((char*)gci + OFF_GCI_TPL_UID) == uid) return r;
    }
    return nullptr;
}

static void force_poll(void) {
    if (!FORCE_SWIM_IDS[0]) return;
    time_t now = time(nullptr);
    if (now == g_last_poll) return;
    g_last_poll = now;

    int map = g_cur_map;
    const WaterMaskEntry* e = mask_for(map);
    if (!e || e->count <= 0) return;
    void* m = gcm();
    if (!m) return;

    typedef void* (*fn_getc)(void*, int, void*);
    typedef int   (*fn_map)(void*, void*);
    typedef uint64_t (*fn_pos)(void*, void*);
    fn_getc getc_ = (fn_getc)(g_base + RVA_GCM_GETCHAR);
    fn_map  gmap  = (fn_map)(g_base + RVA_GCI_CUR_MAP);
    fn_pos  gpos  = (fn_pos)(g_base + RVA_GCI_CUR_POS);

    /* 逐个 uid 处理 */
    const char* p = FORCE_SWIM_IDS;
    while (*p) {
        while (*p == ',' || *p == ' ') ++p;
        if (!*p) break;
        int uid = 0, neg = 0;
        if (*p == '-') { neg = 1; ++p; }
        while (*p >= '0' && *p <= '9') { uid = uid * 10 + (*p - '0'); ++p; }
        if (neg) uid = -uid;
        if (uid <= 0) continue;

        void* gci = getc_(m, uid, nullptr);
        if ((uintptr_t)gci < 0x10000) continue;
        if (gmap(gci, nullptr) != map) continue;          /* 不在当前图 */

        uint64_t xy = gpos(gci, nullptr);
        int x = (int)(uint32_t)(xy & 0xffffffffu);
        int y = (int)(uint32_t)(xy >> 32);
        if (mask_has(e, x, y)) continue;                  /* 已经在水里 */

        int ox = 0, oy = 0;
        if (!pick_water_cell(e, x, y, &ox, &oy)) continue;
        uint64_t cell_xy = ((uint64_t)(uint32_t)ox) | ((uint64_t)(uint32_t)oy << 32);
        void* role = role_of_uid(uid);
        if (role) {
            typedef void (*fn_tp)(void*, uint64_t, uint32_t, void*);
            ((fn_tp)(g_base + RVA_ROLE_NPCTP))(role, cell_xy, 0, nullptr);
            if (g_force_hits2++ < 15)
                LOGI("[water] NpcTP 传送 uid=%d map=%d (%d,%d) -> 水域 (%d,%d)", uid, map, x, y, ox, oy);
        } else {
            if (g_force_hits2++ < 15)
                LOGI("[water] 找不到 Role uid=%d（不在场景里？）", uid);
        }
        if (orig_setpos) orig_setpos(gci, cell_xy, 0, 1);
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

    /* v116：CreateRoleMar.CreateNpc 的钩子已摘除 —— 0x234EAD4 那个重载实际用到 w7（第 8 个参数），
       我们的转调只保留到 x6，丢参会把垃圾指针传进游戏 → 加载存档闪退。等确认参数表再挂。 */

    /* v120：SetPos 只取函数指针直接调用（它的跳板构造失败，钩不上） */
    orig_setpos = (fn_setpos)(g_base + RVA_GCI_SETPOS);
    LOGI("[water] SetPos 指针 = %p（直接调用，不挂钩子）", (void*)orig_setpos);

    if (hook_install("GameCharacterInstance.SetMapId", (void*)(g_base + RVA_GCI_SETMAPID),
                     (void*)hook_setmapid, (void**)&orig_setmapid))
        LOGI("[water] 已挂 GameCharacterInstance.SetMapId（强制定位）");
    else
        LOGE("[water] 挂 GameCharacterInstance.SetMapId 失败");

    /* v113：覆写 ForceNPCSwim（PC 端有效；手机端实测没被调用过，保留作对照） */
    if (hook_install("GameConfig.GetConfigStr", (void*)(g_base + RVA_GC_GETCONFIGSTR),
                     (void*)hook_getconfigstr, (void**)&orig_getstr))
        LOGI("[water] 已挂 GameConfig.GetConfigStr（ForceNPCSwim 覆写）");
    else
        LOGE("[water] 挂 GameConfig.GetConfigStr 失败");
}
