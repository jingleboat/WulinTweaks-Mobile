/*
 * WulinTweaks · 水面可走 / 入水形态（仅限烤表地图）  —— v183 收口版
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
 * 烤表只有 504(679) / 2160(177) / 2169(0)，2169 是空表 → **实际生效就 504 和 2160 两个场景**。
 * 没在表里的地图 → 一行代码都不干预。
 *
 * ------------------------------------------------------------------
 * v183 收口后，本模块只剩下这些行为：
 *   ① MapManager.IsBlock / MapEntity.IsBlock       —— 烤表命中且原版说"挡" → 改成可走
 *   ② MapManager.IsSwimGrid / MapEntity.IsSwimGrid —— 烤表命中且原版说 false → 改成 true
 *   ③ Role.CheckInBathWater                        —— **只观察**（计数/日志），不改参数、不改返回
 *   ④ 504 落点：每秒轮询一次，名单内每个 uid 按 (uid,504) 哈希落到 x[16,31]×y[23,36]
 *      内的一格（每个 uid 每次启动只强制一次），随后无条件发一个同矩形内的游动终点。
 *      **其它地图（含 2160）坐标一行不动。**
 * 已移除（都是死代码，结论留在下面「试过但走不通」注释里，别再走回头路）：
 *   v113 ForceNPCSwim 覆写 / v115 CreateNpc 钩 / v119 SetPos 钩 / v120 SetMapId 钩。
 * 诊断日志默认关闭（g_dbg = 0），启动时的落点自检仍保留。
 * ------------------------------------------------------------------
 */
#include "common.h"
#include "hook.h"
#include "water.h"
#include "water_mask.h"

#include <dlfcn.h>
#include <time.h>
#include <string.h>
#include <stdio.h>    /* snprintf（v181 落点自检） */
#include <stdlib.h>   /* abs */

/* ---- 入水名单（uid）--------------------------------------------------------
 * 这是**唯一**驱动落点的名单：504 里每个 uid 按 (uid,map) 哈希落到自己的矩形格。
 * 已查：曼陀罗/洛千雪/华青青/叶银瓶/定海棠/曲忘忧/绛紫烟/凌梦蝶/韩红玉/
 * 福箐郡主/一枝花/哑琴/完颜昭宁/苗彩蝶/付瑶琴/顾倾城/鱼薇儿/唐婉儿/海棠秋/凤璇玑/月咏樱羽
 * + 纪茯苓(10136) + 花四娘(20025，待确认)
 * 严如蕊=20063（形象 255，有 Wash 动作 + 255w 贴图）
 * 丁小姐=20127 **不加入**：形象 269 没有 Wash 动作、也没有 269w 贴图，入了水也只能播走路 */
#define FORCE_SWIM_IDS "10021,10084,10092,10009,10039,10064,10056,10008,10045,10071,10094,10081,10028,10005,10075,10052,10098,10088,10108,10105,10049,10136,20025,20063"

/* ---- 试过但走不通的落点通道（v183 已从代码里移除，只留结论，别再走回头路）----
 *   v113  覆写 GameConfig.GetConfigStr("ForceNPCSwim") → **手机端从不读这个 key**，实测 0 次调用
 *   v115  钩 CreateRoleMar.CreateNpc 改落点 → 8 参数转调会传垃圾指针，**加载存档闪退**
 *   v119  钩 GameCharacterInstance.SetPos  → `跳板构造失败`，从未装上
 *   v120  钩 GameCharacterInstance.SetMapId → `跳板构造失败`，从未装上
 * 现在唯一生效的落点通道：每 500ms 轮询 → Role.NpcTP（v122）+ Role.SetEndPos（v123）
 */

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
static int g_dbg = 0;              /* v183：默认关。要诊断时改 1（每秒一行状态 + 各钩子前 40 条明细） */
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
/* v190：「不可通行终点保底改写」总开关。用户要求暂时停用；改成 1 即可恢复。
 * （必须放在第一次使用之前，否则 #if 里按 0 处理，静默失效。） */
#define SPAWN_BLOCKED_GUARD 0

#if SPAWN_BLOCKED_GUARD
static void* g_mapmgr = nullptr;   /* 缓存实例，供 walkable() 查原版 IsBlock 真值 */
#endif

static bool hook_mm_isblock(void* self, uint64_t xy, uint32_t z) {
    bool r = orig_mm_block ? orig_mm_block(self, xy, z) : false;
    if (!self) return r;
#if SPAWN_BLOCKED_GUARD
    g_mapmgr = self;
#endif
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

/* ---- v180：504 落点改为「固定矩形内按 uid 哈希取整数」 ----------------------
 * 用户指定的矩形 x[16,31] × y[23,36]：从烤表实测 **224 格全部是水面**、且单连通，
 * 所以这里是纯粹的整数取模 —— 不需要「落空重试」，也不需要「退回最近水域格」兜底。
 * 名单 24 个 uid 在该矩形内哈希后互不碰撞（实测算过：占用 24/24 格、0 冲突），
 * 因此不会再像 v179 那样全部叠在同一个 (36,34) 上。
 *
 * 其余地图（2160 澡堂 / 2169 空表）**一律不做落点覆盖**，保持原版生成坐标，
 * 只保留「水面可走 / 入水形态」两层伪造 —— v180 用户要求。
 */
#define SPAWN_RECT_MAP 504
#define SPAWN_RECT_X0  16
#define SPAWN_RECT_X1  31
#define SPAWN_RECT_Y0  23
#define SPAWN_RECT_Y1  36
#define SPAWN_RECT_W   (SPAWN_RECT_X1 - SPAWN_RECT_X0 + 1)
#define SPAWN_RECT_H   (SPAWN_RECT_Y1 - SPAWN_RECT_Y0 + 1)

/* 只有这张图做落点覆盖；其它图（含 2160）原版生成坐标不动 */
static int spawn_rect_map(int map) { return map == SPAWN_RECT_MAP; }

/* Thomas Wang 32bit integer hash：同一 (uid, map, salt) 永远得到同一格 */
static uint32_t spawn_hash(uint32_t a) {
    a = (a ^ 61u) ^ (a >> 16);
    a = a + (a << 3);
    a = a ^ (a >> 4);
    a = a * 0x27d4eb2du;
    a = a ^ (a >> 15);
    return a;
}

/* 取落点：矩形内的随机整数。salt=0 是生成坐标，其它值用于「游动目标」。 */
static void pick_spawn_cell(int uid, int map, uint32_t salt, int* ox, int* oy) {
    uint32_t h = spawn_hash((uint32_t)uid ^ (uint32_t)map ^ salt);
    *ox = SPAWN_RECT_X0 + (int)(h % (uint32_t)SPAWN_RECT_W);
    *oy = SPAWN_RECT_Y0 + (int)((h / (uint32_t)SPAWN_RECT_W) % (uint32_t)SPAWN_RECT_H);
}

/* ---- v181：每个 uid 在 504 的**第一次落点必须换成矩形格** ------------------
 * v180 的逻辑是「不在水里才拉进水域」，于是原版已经把人放进池子（比如固定在
 * 华清泉某个点）时，mask_has 命中 → 直接跳过 → 落点永远不会变成随机格。
 * 现在改成：本次启动内第一次见到她在 504，无论当前在哪，都 NpcTP 到她的矩形格；
 * 之后只在她自己跑出水面时才拉回（不再每 500ms 和 AI 抢位置）。
 */
#define SPAWN_PLACED_MAX 64
static int g_placed[SPAWN_PLACED_MAX];
static int g_placed_n = 0;

static int spawn_placed(int uid) {
    for (int i = 0; i < g_placed_n; ++i)
        if (g_placed[i] == uid) return 1;
    return 0;
}
static void spawn_place_mark(int uid) {
    if (g_placed_n < SPAWN_PLACED_MAX) g_placed[g_placed_n++] = uid;
}

/* ---- v121：轮询强制定位 ----------------------------------------------------
 * 理由：SetPos(0x2245B58) 与 SetMapId(0x2245B48) 的**跳板都构造失败**（日志实证，两者都已从代码移除），
 * CreateNpc 的参数表又太长（v115 因此闪退）。所以改用轮询：借 MapManager.IsBlock（每帧都调、
 * 已挂）做节拍，每秒查一遍名单：
 *   GetCharacter(uid) → 是否在 504 → 就该落到她自己的矩形格（每个 uid 只强制一次）→ Role.NpcTP。
 */
#define RVA_GCM_GETCHAR  0x2263114  /* GameCharacterInstance GameCharacterManager.GetCharacter(int) */
#define RVA_GCI_CUR_MAP  0x223C1A8  /* int GameCharacterInstance.get_CurMapId() */
#define RVA_GCI_CUR_POS  0x223C1B8  /* Vector3Int GameCharacterInstance.get_CurPos() */
#define RVA_ROLE_NPCTP   0x2184F80  /* void Role.NpcTP(Vector3Int pos) —— 真正把角色传送到某格 */
#define RVA_ROLE_SETEND  0x2184D50  /* void Role.SetEndPos(Vector3Int end) —— 设终点，角色会自己走过去 */

static void* g_gcm = nullptr;
static time_t g_last_poll = 0;
static int g_force_hits2 = 0;

#if SPAWN_BLOCKED_GUARD
/* 名单判断（只被保底改写用到；停用后一起不编） */
static int in_swim_list(int uid) {
    if (!FORCE_SWIM_IDS[0] || uid <= 0) return 0;
    const char* p = FORCE_SWIM_IDS;
    while (*p) {
        while (*p == ',' || *p == ' ') ++p;
        if (!*p) break;
        int v = 0;
        while (*p >= '0' && *p <= '9') { v = v * 10 + (*p - '0'); ++p; }
        if (v == uid) return 1;
    }
    return 0;
}
#endif  /* SPAWN_BLOCKED_GUARD */


/* ---- v185 诊断：观察「谁在给她设移动终点」 --------------------------------
 * 只打印、不改行为。目的有三个：
 *   ① Role.SetEndPos 的跳板到底能不能构造（决定"改写终点"方案成不成立）；
 *   ② AI 给的终点是不是全部落在陆地（若是 → 水面格不在游戏的漫游目标里）；
 *   ③ 我们 NpcTP 之后她到底有没有被挪过去（回读一次 CurPos）。
 */
static void (*orig_setend)(void*, uint64_t, uint32_t, void*) = nullptr;
static int g_setend_calls = 0;   /* v187：只计数，不再逐条打日志（路径日志里有决策结果） */

/* v186：回读槽改成数组 —— v185 只留一个，被后来的放置覆盖，只能验证到最后一个 uid */
static int g_verify[32];
static int g_verify_n = 0;
static int verify_take(int uid) {
    for (int i = 0; i < g_verify_n; ++i) {
        if (g_verify[i] == uid) {
            g_verify[i] = g_verify[--g_verify_n];
            return 1;
        }
    }
    return 0;
}
static void verify_add(int uid) {
    if (g_verify_n < 32) g_verify[g_verify_n++] = uid;
}

/* v186：位置轨迹 —— 每次轮询对比上一次，变了就记一行，看清水↔陆的切换时机 */
typedef struct { int uid; int x; int y; } PosSlot;
static PosSlot g_pos_trace[32];
static int g_pos_trace_n = 0;
static int g_move_log = 0;

static void trace_move(int uid, int x, int y, const WaterMaskEntry* e) {
    for (int i = 0; i < g_pos_trace_n; ++i) {
        if (g_pos_trace[i].uid != uid) continue;
        if (g_pos_trace[i].x == x && g_pos_trace[i].y == y) return;
        int w0 = (e && mask_has(e, g_pos_trace[i].x, g_pos_trace[i].y)) ? 1 : 0;
        int w1 = (e && mask_has(e, x, y)) ? 1 : 0;
        if (g_move_log++ < 120)
            LOGI("[water] 移动 uid=%d (%d,%d)->(%d,%d) 水%d->水%d", uid,
                 g_pos_trace[i].x, g_pos_trace[i].y, x, y, w0, w1);
        g_pos_trace[i].x = x;
        g_pos_trace[i].y = y;
        return;
    }
    if (g_pos_trace_n < 32) {
        g_pos_trace[g_pos_trace_n].uid = uid;
        g_pos_trace[g_pos_trace_n].x = x;
        g_pos_trace[g_pos_trace_n].y = y;
        g_pos_trace_n++;
    }
}

#if SPAWN_BLOCKED_GUARD   /* 用户要求暂时停用；改成 1 恢复 */
/* ---- v186/v187 的实测结论（保留作依据，代码只剩「不可通行」保底）-------------
 *   · Role.PathMoveOnRoam 是 NPC 漫游的驱动者（每走一格重算一次剩余路径，格数递减）；
 *   · AI 自己给的终点 55/55 全在陆地、0 次落在水域 —— 它的候选池里根本没有水面格，
 *     所以她一旦上岸就再也不会自己下水；
 *   · 我们调 Role.SetEndPos 指到水域格，会原样出现在 PathMoveOnRoam 的终点里，
 *     而且游戏不会立刻覆盖回去 → 「改目标」这条路是通的。
 * 试过并放弃的做法：
 *   · 按概率中途改道（v187）—— 干扰原版漫游，已删除；
 *   · 往 PFM 的「陆地格列表」注入水域格（v188）—— 该字段偏移(0x30)从未被验证过，
 *     写错会破坏游戏内存，风险不值得，已删除。
 * v189 最终只保留两条：① 不可通行终点保底改写；② 每秒 1% 概率重指派水域终点（见 force_poll）。
 */
#define OFF_ARRAY_LEN      0x18
#define RVA_PATHMOVE_ROAM  0x218779C

static void (*orig_pathmove)(void*, void*, void*, void*) = nullptr;
static int g_pm_calls = 0, g_pm_log = 0;
static int g_rw_blocked = 0;       /* 因「不可通行」改写次数 */
static int g_in_pm = 0;            /* 防重入：hook 里调 SetEndPos 会再触发本 hook */

/* 每个 uid 记住「上次评估过的目的地」，同一个目的地不重复处理 */
typedef struct { int uid; int lx, ly; } EvalSlot;
static EvalSlot g_eval[32];
static int g_eval_n = 0;

static int eval_new(int uid, int x, int y) {
    for (int i = 0; i < g_eval_n; ++i) {
        if (g_eval[i].uid != uid) continue;
        if (g_eval[i].lx == x && g_eval[i].ly == y) return 0;
        g_eval[i].lx = x;
        g_eval[i].ly = y;
        return 1;
    }
    if (g_eval_n < 32) {
        g_eval[g_eval_n].uid = uid;
        g_eval[g_eval_n].lx = x;
        g_eval[g_eval_n].ly = y;
        g_eval_n++;
        return 1;
    }
    return 0;
}

/* 可通行吗：先用**原版** MapManager.IsBlock 求真值，再看烤表放行规则。
 * 必须调 orig_：直接调我们的钩子会递归，而且会把水面判成"挡"，正好反了。
 * 返回：1 可通行 / 0 不可通行 / -1 判不了（不动手） */
static int walkable(const WaterMaskEntry* e, int x, int y) {
    if (!g_mapmgr || !orig_mm_block) return -1;
    if (*(unsigned char*)((char*)g_mapmgr + OFF_MM_BATTLE)) return -1;
    uint64_t xy = ((uint64_t)(uint32_t)y << 32) | (uint64_t)(uint32_t)x;
    if (!orig_mm_block(g_mapmgr, xy, 0)) return 1;
    return (e && mask_has(e, x, y)) ? 1 : 0;
}

/* 只保留「不可通行 → 必改写」这条保底，不再按概率改道（v188 起用原版漫游） */
static void hook_pathmove(void* self, void* list, void* endAction, void* posAction) {
    g_pm_calls++;
    if ((uintptr_t)self > 0x10000 && (uintptr_t)list > 0x10000
        && F_WATER && g_cur_map == SPAWN_RECT_MAP && !g_in_pm) {
        void* gci = *(void**)((char*)self + OFF_ROLE_GCI);
        int uid = 0;
        if ((uintptr_t)gci > 0x10000) uid = *(int*)((char*)gci + OFF_GCI_TPL_UID);
        if (uid > 0 && in_swim_list(uid)) {
            void* arr = *(void**)((char*)list + OFF_LIST_ITEMS);
            int n = *(int*)((char*)list + OFF_LIST_SIZE);
            int tx = -1, ty = -1;
            if ((uintptr_t)arr > 0x10000 && n > 0 && n < 20000) {
                int alen = *(int*)((char*)arr + OFF_ARRAY_LEN);
                if (alen > 0 && alen < n) n = alen;
                const int* p = (const int*)((char*)arr + OFF_ARRAY_DATA);
                tx = p[(n - 1) * 3];
                ty = p[(n - 1) * 3 + 1];
            }
            if (tx != -1 && eval_new(uid, tx, ty)) {
                const WaterMaskEntry* e = cur_mask();
                if (walkable(e, tx, ty) == 0) {
                    int cx = SPAWN_RECT_X0 + (int)(g_pm_calls % (uint32_t)SPAWN_RECT_W);
                    int cy = SPAWN_RECT_Y0 + (int)((g_pm_calls / (uint32_t)SPAWN_RECT_W) % (uint32_t)SPAWN_RECT_H);
                    g_rw_blocked++;
                    if (g_pm_log++ < 100)
                        LOGI("[water] 改写 uid=%d 终点(%d,%d) 不可通行 -> (%d,%d)", uid, tx, ty, cx, cy);
                    g_in_pm = 1;
                    if (orig_setend)
                        orig_setend(self,
                                    ((uint64_t)(uint32_t)cx) | ((uint64_t)(uint32_t)cy << 32),
                                    0, nullptr);
                    g_in_pm = 0;
                    return;
                }
            }
        }
    }
    if (orig_pathmove) orig_pathmove(self, list, endAction, posAction);
}

#endif  /* SPAWN_BLOCKED_GUARD */

/* ---- v189：每秒按概率把她的目标再换成水域随机格 ---------------------------
 * 用户方案：加载 504 时落点 + 一个水域随机终点；之后每秒 1% 概率再触发一次。
 * 不做水陆拦截、不改陆地格列表、不读她的位置 —— 只用已验证可用的 Role.SetEndPos。
 */
/* v191：累积式入水判定 —— 每 5 秒查一次，未命中 +5%，命中重置为 5%。
 * 初始的 100% 由「落点那一刻的 SetEndPos」实现，所以落点之后从 5% 起算。 */
#define SPAWN_RT_PERIOD_MS 5000   /* 查询周期 */
#define SPAWN_RT_START_PCT 5      /* 命中后（以及落点之后）的起始概率 */
#define SPAWN_RT_STEP_PCT  5      /* 未命中每次累加 */
#define SPAWN_RT_MAX_PCT   100    /* 概率上限（到 100% 必中） */

static uint32_t rnd32(void) {
    static uint32_t st = 0;
    uint32_t x = st ? st : (st = (uint32_t)time(nullptr) ^ 0x9E3779B9u);
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    return (st = x);
}

/* 矩形内取一格水域（该矩形实测 224 格全为水面，故直接取模即可） */
static void pick_random_water_cell(int* ox, int* oy) {
    *ox = SPAWN_RECT_X0 + (int)(rnd32() % (uint32_t)SPAWN_RECT_W);
    *oy = SPAWN_RECT_Y0 + (int)((rnd32() / (uint32_t)SPAWN_RECT_W) % (uint32_t)SPAWN_RECT_H);
}

static int g_retarget_log = 0;

/* 单调毫秒时钟（用于 5 秒周期） */
static long long now_ms(void) {
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0) return (long long)time(nullptr) * 1000;
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

/* v191：每个 uid 一份累积式概率状态 */
typedef struct { int uid; int prob; long long next_ms; } RtSlot;
static RtSlot g_rt[32];
static int g_rt_n = 0;

static RtSlot* rt_get(int uid, long long now) {
    for (int i = 0; i < g_rt_n; ++i)
        if (g_rt[i].uid == uid) return &g_rt[i];
    if (g_rt_n >= 32) return nullptr;
    RtSlot* s = &g_rt[g_rt_n++];
    s->uid = uid;
    s->prob = SPAWN_RT_START_PCT;
    s->next_ms = now + SPAWN_RT_PERIOD_MS;
    return s;
}

/* 落点那一刻已经 SetEndPos 过一次（相当于「初始 100% 命中」）→ 概率回到起始值并重置计时 */
static void rt_after_spawn(int uid) {
    long long now = now_ms();
    RtSlot* s = rt_get(uid, now);
    if (!s) return;
    s->prob = SPAWN_RT_START_PCT;
    s->next_ms = now + SPAWN_RT_PERIOD_MS;
}

static void hook_setend(void* self, uint64_t xy, uint32_t z, void* extra) {
    g_setend_calls++;
    (void)xy; (void)z;
    if (orig_setend) orig_setend(self, xy, z, extra);
}

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
    if (!spawn_rect_map(map)) return;   /* v180：2160 等不做轮询强制，坐标保持原版 */
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

        /* v186 诊断：位置轨迹 + 落点后回读（改数组后每个 uid 都能验证一次） */
        trace_move(uid, x, y, e);
        if (verify_take(uid))
            LOGI("[water] 回读 uid=%d map=%d 位置(%d,%d) 在水域=%d",
                 uid, map, x, y, mask_has(e, x, y) ? 1 : 0);

        /* v184：落点只做一次（不拉回、不读她的位置，避免瞬移闪烁）。
         * v191：之后**每 5 秒**才判定一次是否把终点重指到水域：
         *   未命中 → 概率 +5%；命中 → 重指水域随机格，概率重置为 5%。
         * 初始的 100% 由「落点那一刻的 SetEndPos」实现。
         * 没到判定点就什么都不做，平时零开销。 */
        if (spawn_placed(uid)) {
            long long now = now_ms();
            RtSlot* rs = rt_get(uid, now);
            if (rs && now >= rs->next_ms) {
                rs->next_ms = now + SPAWN_RT_PERIOD_MS;
                int pct = rs->prob;
                if (pct > SPAWN_RT_MAX_PCT) pct = SPAWN_RT_MAX_PCT;
                if ((int)(rnd32() % 100u) < pct) {
                    rs->prob = SPAWN_RT_START_PCT;          /* 命中 → 重置 */
                    void* r2 = role_of_uid(uid);
                    if (r2) {
                        int cx = 0, cy = 0;
                        pick_random_water_cell(&cx, &cy);
                        typedef void (*fn_end2)(void*, uint64_t, uint32_t, void*);
                        ((fn_end2)(g_base + RVA_ROLE_SETEND))(
                            r2, ((uint64_t)(uint32_t)cx) | ((uint64_t)(uint32_t)cy << 32), 0, nullptr);
                        if (g_retarget_log++ < 100)
                            LOGI("[water] 重指水域 uid=%d -> (%d,%d) 命中概率=%d%%",
                                 uid, cx, cy, pct);
                    } else if (g_retarget_log++ < 100) {
                        LOGI("[water] 重指水域 uid=%d 命中概率=%d%% 但找不到 Role", uid, pct);
                    }
                } else {
                    rs->prob = pct + SPAWN_RT_STEP_PCT;      /* 未命中 → 累加 */
                    if (g_retarget_log++ < 100)
                        LOGI("[water] 未命中 uid=%d 概率 %d%% -> %d%%", uid, pct, rs->prob);
                }
            }
            continue;
        }

        int ox = 0, oy = 0;
        pick_spawn_cell(uid, map, 0, &ox, &oy);   /* v180：矩形内哈希取整数（必为水面） */
        uint64_t cell_xy = ((uint64_t)(uint32_t)ox) | ((uint64_t)(uint32_t)oy << 32);
        void* role = role_of_uid(uid);
        if (role) {
            /* v182：落点后**无条件**发一个游动终点。
             * 依据 README「站桩不触发」：CheckInBathWater 只在 Role.OnMove（每走一格）、
             * CreateNpc/CreatePlayer、剧情 MoveNode 这几处被调用 —— 只把人放进水格而不让她动，
             * IsInBath 一直是 false、IsBath 一直是 0，连泡水坐姿都出不来。
             * 旧代码是 (tick % 2) == 0 才发终点，而 tick 是所有 uid 共用的静态计数，
             * 于是约一半角色只站不游 —— v182 去掉这个条件。 */
            typedef void (*fn_tp)(void*, uint64_t, uint32_t, void*);
            ((fn_tp)(g_base + RVA_ROLE_NPCTP))(role, cell_xy, 0, nullptr);

            /* v189：终点取矩形内的**随机**水域格（旧代码跨到另一块水塘会走不过去） */
            int tx = 0, ty = 0;
            pick_random_water_cell(&tx, &ty);
            if (tx == ox && ty == oy)   /* 别把终点设成原点，那样她不会动 */
                tx = SPAWN_RECT_X0 + (tx - SPAWN_RECT_X0 + 1) % SPAWN_RECT_W;
            uint64_t tgt = ((uint64_t)(uint32_t)tx) | ((uint64_t)(uint32_t)ty << 32);
            typedef void (*fn_end)(void*, uint64_t, uint32_t, void*);
            ((fn_end)(g_base + RVA_ROLE_SETEND))(role, tgt, 0, nullptr);

            if (g_force_hits2++ < 15)
                LOGI("[water] 落点 uid=%d map=%d 原(%d,%d) -> 落点(%d,%d) 终点(%d,%d)",
                     uid, map, x, y, ox, oy, tx, ty);
            spawn_place_mark(uid);
            verify_add(uid);   /* v186：下一拍回读她的位置 */
            rt_after_spawn(uid);   /* v191：落点已 SetEndPos 过一次 = 初始 100% 命中 → 概率回到 5% */
        } else {
            if (g_force_hits2++ < 15)
                LOGI("[water] 找不到 Role uid=%d（不在场景里？）", uid);
        }
    }
}

void water_install() {
    if (!g_i2cpp) g_i2cpp = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    for (int i = 0; i < WATER_MASK_COUNT; ++i) {
        LOGI("[water] 烤表 map=%d 水面格=%d（其它地图不干预）", WATER_MASKS[i].mapid, WATER_MASKS[i].count);
    }
    if (FORCE_SWIM_IDS[0]) LOGI("[water] 入水名单 = \"%s\"", FORCE_SWIM_IDS);
    LOGI("[water] v191 落点：仅 map=%d 在 x[%d,%d] y[%d,%d] 内取水域格 —— "
         "每个 uid 首次出现时落一次点 + 发一个水域随机终点（= 初始 100%%）；"
         "之后每 %d 毫秒判定一次：未命中 +%d%%，命中重置 %d%%。其它图（含 2160）坐标不动",
         SPAWN_RECT_MAP, SPAWN_RECT_X0, SPAWN_RECT_X1, SPAWN_RECT_Y0, SPAWN_RECT_Y1,
         SPAWN_RT_PERIOD_MS, SPAWN_RT_STEP_PCT, SPAWN_RT_START_PCT);
    /* v181 自检：把名单每个 uid 在 504 算出来的落点直接打出来 —— 不依赖场景里有没有 Role，
     * 一眼就能看出「是不是随机、会不会撞格」。 */
    if (FORCE_SWIM_IDS[0]) {
        char line[512];
        int len = 0;
        const char* p = FORCE_SWIM_IDS;
        while (*p && len < (int)sizeof(line) - 24) {
            while (*p == ',' || *p == ' ') ++p;
            if (!*p) break;
            int uid = 0, neg = 0;
            if (*p == '-') { neg = 1; ++p; }
            while (*p >= '0' && *p <= '9') { uid = uid * 10 + (*p - '0'); ++p; }
            if (neg) uid = -uid;
            if (uid <= 0) continue;
            int ox = 0, oy = 0;
            pick_spawn_cell(uid, SPAWN_RECT_MAP, 0, &ox, &oy);
            len += snprintf(line + len, sizeof(line) - (size_t)len, "%d:(%d,%d) ", uid, ox, oy);
        }
        line[len < (int)sizeof(line) ? len : (int)sizeof(line) - 1] = 0;
        LOGI("[water] v191 504 落点自检（uid:坐标）：%s", line);
    }

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

    /* v185 诊断：只观察 Role.SetEndPos（谁在给她设移动终点、目标在不在水域） */
    if (hook_install("Role.SetEndPos", (void*)(g_base + RVA_ROLE_SETEND),
                     (void*)hook_setend, (void**)&orig_setend))
        LOGI("[water] 已挂 Role.SetEndPos（v190：落点/重指派用）");
    else
        LOGE("[water] 挂 Role.SetEndPos 失败");

#if SPAWN_BLOCKED_GUARD
    /* 「不可通行终点保底改写」——用户要求暂时停用，整块（含钩子安装）都不编进来 */
    if (hook_install("Role.PathMoveOnRoam", (void*)(g_base + RVA_PATHMOVE_ROAM),
                     (void*)hook_pathmove, (void**)&orig_pathmove))
        LOGI("[water] 已挂 Role.PathMoveOnRoam（保底改写不可通行终点）");
    else
        LOGE("[water] 挂 Role.PathMoveOnRoam 失败");
#else
    LOGI("[water] Role.PathMoveOnRoam 保底改写：已停用（SPAWN_BLOCKED_GUARD=0）");
#endif


    /* v116：CreateRoleMar.CreateNpc 的钩子已摘除 —— 0x234EAD4 那个重载实际用到 w7（第 8 个参数），
       我们的转调只保留到 x6，丢参会把垃圾指针传进游戏 → 加载存档闪退。等确认参数表再挂。 */
    /* v183：GameCharacterInstance.SetPos / SetMapId 的钩子、以及 ForceNPCSwim 覆写，全部移除
       （前者跳板构造失败从未装上，后者手机端从不读那个 key），只留下面的结论注释。 */
}
