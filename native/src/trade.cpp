/*
 * WulinTweaks · 易物三件套（手机端，纯 native 实现）
 * ================================================
 * ① 作价恒占优   GameItemPack.GetTotalValue / TradingWithNpcManager.ValueOffset
 *                → 成交两道门槛都过（PC 版行为照搬）
 * ② 显示池子     StartTrading 之后，把 NPC 的「击杀必掉 / 暗取池」内容**复制副本**
 *                塞进 NPC 背包，只给易物面板看；池子原件一个字节都不动。
 * ③ 确认搬空     ConfirmTrading 之后，把交易区残留 + NPC 背包剩余全部搬进队伍背包。
 *
 * 铁律（PC 版验证过，照搬）：
 *   · 搬运一律「新建实例 → 加进目标包 → 删原件」。
 *     RemoveItem 会先改原实例的 Stack，AddItem 又以 Stack>0 为前置，
 *     直接把同一实例加进另一个包会让两边都变空壳。
 *   · **搬运必须用 AddItem（会按 ItemData 合并），不能用 AddItemAsIndependent。**
 *     AddItemAsIndependent 只做 contents.Add，同一种可堆叠物品被搬进去几次就成几堆
 *     （v80 的 bug：易物「搬空」后队伍背包里金疮药分三四堆）。AddItemAsIndependent
 *     只留给「必须按实例删掉的池子显示副本」。
 *   · 只复制、不写 npc.StealBackPack / DropOnDeadBackPack —— 那两个包同时是
 *     暗取与死亡掉落的数据源。
 *   · 复制出来的副本被易物拿走要记台账，否则同一池子可以无限复制。
 *     副本两种形态：NPC 背包没有同 ID → 独立实例（按实例删/记账）；
 *     NPC 背包已有同 ID → 合并进那一堆（按数量删/记账，撤副本时只撤我们加的那部分，
 *     绝不碰 NPC 原货）。合并形态是为了「池子里的东西不因为背包已有同类而漏掉」，
 *     面板不会因此出现两个同 ID 堆。
 *   · 造实例时 craftLevel 传 0（不造 ItemModifyData）。传 -1 会让可锻造装备
 *     滚一份随机附加属性，搬一次物品属性就变了。
 */
#include "common.h"
#include "trade.h"
#include "hook.h"

#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <fcntl.h>
#include <unistd.h>
#include <time.h>

#define MAX_COPIES 512
#define MAX_SEEN 512
#define LEDGER_SLOTS 4096

/* ------------------------------------------------------------------ 缓存 */
static void* k_item = nullptr;      /* GameItemInstance */
static void* k_ui = nullptr;        /* TradingWithNpcUI */
static void* k_ptm = nullptr;       /* PlayerTeamManager */

/* ------------------------------------------------------------------ 复制台账
 * 副本有两种形态：
 *   merged=0  NPC 背包里没有同 ID 的堆 → 独立实例，**按实例**删/记账（干净）
 *   merged=1  NPC 背包已有同 ID 的堆 → 合并进那一堆，**按数量**删/记账
 *             （副本必须独立才能按实例删，但独立就会在面板里变成两个同 ID 堆；
 *               既然开面板/搬空时本来就会并堆，这里改成合并 + 数量记账，
 *               好处是池子里的东西不会因为"背包已有同类"而漏掉。）
 *   npc0 = 合并前那一堆的数量，撤副本时只撤我们加进去的部分，绝不碰 NPC 原货。 */
struct PoolCopy { void* copy; int id; int drop; int stack0; int merged; int npc0; };
static PoolCopy g_copies[MAX_COPIES];
static int g_ncopies = 0;

static void* g_npc = nullptr;       /* 本次交易的 NPC（拿背包用） */
static void* g_bag = nullptr;       /* 本次交易开始时的 NPC 背包 */

/* ------------------------------------------------------------------ 小工具 */
static inline bool okptr(const void* p) { return (uintptr_t)p > 0x10000 && ((uintptr_t)p & 7) == 0; }

static inline void* list_at(void* list) {
    /* List<T>：+0x10 = T[] , +0x18 = _size */
    return list ? *(void**)((char*)list + 0x10) : nullptr;
}
static inline int list_size(void* list) { return list ? *(int*)((char*)list + 0x18) : 0; }
static inline void* arr_item(void* arr, int i) {
    /* 引用类型数组：+0x18 = length，元素从 +0x20 起 */
    if (!arr) return nullptr;
    int n = *(int*)((char*)arr + 0x18);
    if (i < 0 || i >= n) return nullptr;
    return ((void**)((char*)arr + 0x20))[i];
}
static inline void* list_item(void* list, int i) { return arr_item(list_at(list), i); }

static inline int item_stack(void* it) { return okptr(it) ? *(int*)((char*)it + OFF_ITEM_STACK) : 0; }
static inline int item_tid(void* it) { return okptr(it) ? *(int*)((char*)it + OFF_ITEM_TID) : 0; }

static void* pack_contents(void* pack) {
    if (!okptr(pack)) return nullptr;
    return *(void**)((char*)pack + OFF_PACK_CONTENTS);
}

#define CALL(ret, rva, ...) ((ret (*)(__VA_ARGS__, void*))(g_base + (rva)))
#define CALL0(ret, rva) ((ret (*)(void*))(g_base + (rva)))

/* AddItemAsIndependent：直接 contents.Add(item)，**永不合并** → 只用于「必须能按实例删掉的显示副本」 */
static bool pack_add(void* pack, void* item) {
    if (!okptr(pack) || !okptr(item)) return false;
    return CALL(bool, RVA_PACK_ADD_INDEP, void*, void*)(pack, item, nullptr);
}
/* AddItem：可堆叠物品会走去重合并（同 ItemData 的 m_stack += count），搬运一律走这条 */
static bool pack_add_merge(void* pack, void* item) {
    if (!okptr(pack) || !okptr(item)) return false;
    return CALL(bool, RVA_PACK_ADD_MERGE, void*, void*, bool)(pack, item, false, nullptr);
}
/* 目标包里是否已有同一物品 ID 的活堆；找到就把那一堆与它的数量带出来 */
static void* pack_find_id(void* pack, int tid, int* out_stack) {
    void* list = pack_contents(pack);
    int n = list_size(list);
    for (int i = 0; i < n; ++i) {
        void* it = list_item(list, i);
        if (item_stack(it) > 0 && item_tid(it) == tid) {
            if (out_stack) *out_stack = item_stack(it);
            return it;
        }
    }
    return nullptr;
}
static bool pack_remove(void* pack, void* item, int n) {
    if (!okptr(pack) || !okptr(item)) return false;
    return CALL(bool, RVA_PACK_REMOVE, void*, void*, int)(pack, item, n, nullptr);
}
static bool item_valid(void* item) {
    if (!okptr(item)) return false;
    return CALL(bool, RVA_ITEM_ISVALID, void*)(item, nullptr);
}
static bool item_stackable(void* item) {
    if (!okptr(item)) return false;
    return CALL(bool, RVA_ITEM_STACKABLE, void*)(item, nullptr);
}
static bool item_change_stack(void* item, int amount) {
    if (!okptr(item)) return false;
    return CALL(bool, RVA_ITEM_CHANGESTACK, void*, int)(item, amount, nullptr);
}
/* craftLevel 传 0 = 不造 ItemModifyData（原件没有锻造数据时这才是"同一样东西"）。
 * 传 -1 会让 CreateForgeModify 给可锻造装备滚一份随机附加属性 —— 搬运会改物品属性。 */
static void* item_new_by_id(int id, int stack) {
    if (!k_item) {
        k_item = find_class("WuLin", "GameItemInstance");
        if (k_item && A_class_init) A_class_init(k_item);
    }
    if (!k_item) return nullptr;
    void* it = A_object_new(k_item);
    if (!it) return nullptr;
    CALL(void, RVA_ITEM_CTOR_ID, void*, int, int, int)(it, id, stack, 0, nullptr);
    return it;
}
/* 保真克隆：ID/数量一致；原件带 ItemModifyData（锻造过的装备）就共用同一份，避免重造滚随机值。
 * 注意 IsStackable = Templete.IsStackable && modifyData == null，所以可堆叠物品不会走到共用分支。 */
static void* item_new_clone(void* src) {
    if (!okptr(src)) return nullptr;
    void* c = item_new_by_id(item_tid(src), item_stack(src));
    if (!okptr(c)) return nullptr;
    void* md = *(void**)((char*)src + OFF_ITEM_MODIFY);
    if (okptr(md)) *(void**)((char*)c + OFF_ITEM_MODIFY) = md;
    return c;
}
static void* item_new_by_key(void* key_str, int stack) {
    if (!k_item) {
        k_item = find_class("WuLin", "GameItemInstance");
        if (k_item && A_class_init) A_class_init(k_item);
    }
    if (!k_item || !okptr(key_str)) return nullptr;
    void* it = A_object_new(k_item);
    if (!it) return nullptr;
    CALL(void, RVA_ITEM_CTOR_KEY, void*, void*, int, int)(it, key_str, stack, -1, nullptr);
    return it;
}
static void* char_backpack(void* npc) { return CALL(void*, RVA_CHAR_BACKPACK, void*)(npc, nullptr); }
static void* char_droppack(void* npc) { return CALL(void*, RVA_CHAR_DROP_PACK, void*)(npc, nullptr); }
static void* char_stealpack(void* npc) { return CALL(void*, RVA_CHAR_STEAL_PACK, void*)(npc, nullptr); }
static void* char_templete(void* npc) { return CALL(void*, RVA_CHAR_TEMPLETE, void*)(npc, nullptr); }
static void* tr_npc(void) { return CALL0(void*, RVA_TR_GET_NPC)(nullptr); }
static void* tr_player_zone(void) { return CALL0(void*, RVA_TR_PLAYER_ZONE)(nullptr); }
static void* tr_npc_zone(void) { return CALL0(void*, RVA_TR_NPC_ZONE)(nullptr); }

static void* team_inventory() {
    if (!k_ptm) k_ptm = find_class("WuLin", "PlayerTeamManager");
    if (!k_ptm) return nullptr;
    void* ptm = find_object_of_type(k_ptm);
    if (!okptr(ptm)) return nullptr;
    void* inv = CALL(void*, RVA_TEAM_INVENTORY, void*)(ptm, nullptr);
    return okptr(inv) ? inv : nullptr;
}

static void refresh_ui() {
    if (!k_ui) k_ui = find_class("WuLin", "TradingWithNpcUI");
    if (!k_ui) return;
    void* ui = find_object_of_type(k_ui);
    if (!okptr(ui)) return;
    void* bags[2];
    bags[0] = *(void**)((char*)ui + OFF_UI_NPC_BAG);
    bags[1] = *(void**)((char*)ui + OFF_UI_NPC_TRADE);
    for (int i = 0; i < 2; ++i) {
        if (!okptr(bags[i])) continue;
        CALL(void, RVA_SHOWER_SETDIRTY, void*)(bags[i], nullptr);
    }
}

/* 数「真货」：Stack > 0 */
static int count_valid(void* pack) {
    void* list = pack_contents(pack);
    int n = list_size(list), c = 0;
    for (int i = 0; i < n; ++i) if (item_stack(list_item(list, i)) > 0) c++;
    return c;
}

/* 清掉 Stack<=0 的空壳 */
static int purge_invalid(void* pack) {
    void* list = pack_contents(pack);
    if (!list) return 0;
    /* 快照，边遍历边删会动 list */
    void* snap[256];
    int n = list_size(list), m = 0;
    for (int i = 0; i < n && m < 256; ++i) {
        void* it = list_item(list, i);
        if (okptr(it)) snap[m++] = it;
    }
    int killed = 0;
    for (int i = 0; i < m; ++i) {
        void* it = snap[i];
        if (item_stack(it) > 0 && item_valid(it)) continue;
        if (pack_remove(pack, it, 1)) killed++;
        else if (pack_remove(pack, it, 0)) killed++;
    }
    return killed;
}

/* src → dst：**必须走合并版 AddItem**，否则同一种可堆叠物品在 dst 里会变成好几个独立堆
 * （易物「搬空」把 NPC 背包 + 交易区 + 暗取池副本一起搬进队伍背包时最明显）。
 * 顺序上先扣源再加目标：合并写入后拿不到"目标里那一堆"的实例，没法回滚；
 * 先扣源则可以按数量精确补回（合并写回，数量一分不差）。 */
static int transfer(void* src, void* dst) {
    if (!okptr(src) || !okptr(dst) || src == dst) return 0;
    void* list = pack_contents(src);
    if (!list) return 0;
    void* snap[256];
    int n = list_size(list), m = 0;
    for (int i = 0; i < n && m < 256; ++i) {
        void* it = list_item(list, i);
        if (okptr(it)) snap[m++] = it;
    }
    int moved = 0;
    for (int i = 0; i < m; ++i) {
        void* it = snap[i];
        int stack = item_stack(it);
        int tid = item_tid(it);
        if (stack <= 0 || tid <= 0) continue;
        void* c = item_new_clone(it);          /* 先造副本，避免扣掉源之后拿不到属性 */
        if (!okptr(c) || item_stack(c) <= 0) continue;
        if (!pack_remove(src, it, stack)) continue;   /* 源先扣（RemoveItem 会把实例 Stack 归零） */
        if (pack_add_merge(dst, c)) { moved++; continue; }
        /* 目标加不进去（极端情况）→ 把原件数量还原后塞回源 */
        item_change_stack(it, stack);
        pack_add_merge(src, it);
    }
    return moved;
}

/* 把包里同 ID 的可堆叠物堆并回一堆 —— 修复 v80（含 PC 1.11.x）已经拆开的存档。
 * 只动 IsStackable==true 的条目（同 ItemData、无 ItemModifyData），不碰装备。 */
static int merge_dup_piles(void* pack) {
    if (!okptr(pack)) return 0;
    int merged = 0;
    int cap = list_size(pack_contents(pack)) + 2;
    for (int round = 0; round < cap; ++round) {
        void* list = pack_contents(pack);
        int n = list_size(list);
        int hit = 0;
        for (int i = 0; i < n && !hit; ++i) {
            void* a = list_item(list, i);
            if (!okptr(a)) continue;
            int ta = item_tid(a);
            if (ta <= 0 || item_stack(a) <= 0 || !item_stackable(a)) continue;
            for (int j = i + 1; j < n; ++j) {
                void* b = list_item(list, j);
                if (!okptr(b) || b == a) continue;
                if (item_tid(b) != ta || item_stack(b) <= 0) continue;
                int add = item_stack(b);
                if (!item_change_stack(a, add)) break;
                if (pack_remove(pack, b, add)) merged++;
                hit = 1;
                break;
            }
        }
        if (!hit) break;
    }
    return merged;
}

/* 体检：某个包里是否存在「同一物品 ID 的多堆」（可堆叠物品被拆堆的唯一可观测症状） */
static void log_dup_piles(const char* tag, void* pack) {
    void* list = pack_contents(pack);
    int n = list_size(list);
    if (n <= 0) return;
    int bad = 0;
    char buf[192];
    int off = 0;
    buf[0] = 0;
    for (int i = 0; i < n; ++i) {
        void* a = list_item(list, i);
        int ta = item_tid(a);
        if (item_stack(a) <= 0 || ta <= 0) continue;
        /* 只在同 ID 第一次出现时统计 */
        int first = 1;
        for (int k = 0; k < i; ++k) {
            void* p = list_item(list, k);
            if (item_stack(p) > 0 && item_tid(p) == ta) { first = 0; break; }
        }
        if (!first) continue;
        int cnt = 0;
        for (int j = 0; j < n; ++j) {
            void* b = list_item(list, j);
            if (item_stack(b) > 0 && item_tid(b) == ta) cnt++;
        }
        if (cnt > 1) {
            bad++;
            if (off < (int)sizeof(buf) - 24)
                off += snprintf(buf + off, sizeof(buf) - off, " %d×%d堆", ta, cnt);
        }
    }
    if (bad > 0) LOGI("易物: %s 同 ID 重复堆 %d 种:%s", tag, bad, buf);
    else LOGI("易物: %s 无同 ID 重复堆（共 %d 堆）", tag, n);
}

/* ---------------------------------------------------------------- 台账
 * 语义：**池子里的东西只能白拿一次**。
 *   ① 每次开易物面板，把「这个 NPC 还没被拿走的池子物品」复制一份塞进 NPC 背包；
 *   ② 被玩家拿走（成交/搬空时进了队伍背包）→ 按 uid+iid+类别+物品id 记账；
 *   ③ 下次开易物，台账里有的直接跳过 —— 同一件不会无限生成。
 * key 里带 NPC 实例 id，所以不同 NPC 的同一件物品互不影响。
 * 想重置：删 files/trade-taken.txt。 */
static uint64_t g_ledger[LEDGER_SLOTS];   /* 0 = 空槽；哈希后强制非 0 */
static int g_ledger_loaded = 0;

static uint64_t mix64(uint64_t x) {
    x ^= x >> 33; x *= 0xff51afd7ed558ccdULL;
    x ^= x >> 33; x *= 0xc4ceb9fe1a85ec53ULL;
    x ^= x >> 33;
    return x ? x : 1;
}
static uint64_t copy_key(void* npc, int drop, int itemId) {
    uint64_t uid = 0, iid = 0;
    if (okptr(npc)) {
        uid = (uint32_t)CALL(int, RVA_CHAR_TPL_UID, void*)(npc, nullptr);
        iid = (uint32_t)CALL(int, RVA_CHAR_INSTANCE_ID, void*)(npc, nullptr);
    }
    return mix64(uid * 1000003ULL + iid * 31ULL + (drop ? 7 : 3) * 1000000007ULL +
                 (uint32_t)itemId);
}

static int ledger_has(uint64_t k) {
    for (int i = 0; i < LEDGER_SLOTS; ++i) {
        int slot = (int)((k + (uint64_t)i * 0x9E3779B97F4A7C15ULL) % LEDGER_SLOTS);
        if (g_ledger[slot] == 0) return 0;
        if (g_ledger[slot] == k) return 1;
    }
    return 0;
}
static int ledger_add(uint64_t k) {
    for (int i = 0; i < LEDGER_SLOTS; ++i) {
        int slot = (int)((k + (uint64_t)i * 0x9E3779B97F4A7C15ULL) % LEDGER_SLOTS);
        if (g_ledger[slot] == k) return 0;
        if (g_ledger[slot] == 0) { g_ledger[slot] = k; return 1; }
    }
    return 0;
}

static void ledger_load() {
    if (g_ledger_loaded) return;
    g_ledger_loaded = 1;
    int fd = open(LEDGER_PATH, O_RDONLY);
    if (fd < 0) { LOGI("台账: 无历史文件"); return; }
    char buf[4096];
    int n, total = 0;
    char line[128];
    int ln = 0;
    while ((n = (int)read(fd, buf, sizeof(buf))) > 0) {
        for (int i = 0; i < n; ++i) {
            char c = buf[i];
            if (c == '\n' || c == '\r') {
                if (ln > 0) {
                    line[ln] = 0;
                    /* 格式 uid:iid:s|d:itemId */
                    int uid = 0, iid = 0, item = 0;
                    char kind = 's';
                    if (sscanf(line, "%d:%d:%c:%d", &uid, &iid, &kind, &item) == 4) {
                        uint64_t k = mix64((uint64_t)(uint32_t)uid * 1000003ULL +
                                           (uint64_t)(uint32_t)iid * 31ULL +
                                           (uint64_t)(kind == 'd' ? 7u : 3u) * 1000000007ULL +
                                           (uint32_t)item);
                        if (ledger_add(k)) total++;
                    }
                    ln = 0;
                }
            } else if (ln < (int)sizeof(line) - 1) {
                line[ln++] = c;
            }
        }
    }
    close(fd);
    LOGI("台账: 载入 %d 条 → %s", total, LEDGER_PATH);
}

static void ledger_mark(void* npc, int drop, int itemId) {
    ledger_load();
    uint64_t k = copy_key(npc, drop, itemId);
    if (!ledger_add(k)) return;
    int fd = open(LEDGER_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd < 0) return;
    int uid = okptr(npc) ? CALL(int, RVA_CHAR_TPL_UID, void*)(npc, nullptr) : 0;
    int iid = okptr(npc) ? CALL(int, RVA_CHAR_INSTANCE_ID, void*)(npc, nullptr) : 0;
    char line[96];
    int n = snprintf(line, sizeof(line), "%d:%d:%c:%d\n", uid, iid, drop ? 'd' : 's', itemId);
    if (n > 0) write(fd, line, (size_t)n);
    close(fd);
}

/* ------------------------------------------------------- 往背包造「只给易物看」的副本 */
struct Seen { int id[MAX_SEEN]; int n; };
static bool seen_has(Seen* s, int id) {
    for (int i = 0; i < s->n; ++i) if (s->id[i] == id) return true;
    return false;
}
static void seen_add(Seen* s, int id) { if (s->n < MAX_SEEN) s->id[s->n++] = id; }

static void remember_copy(void* copy, int id, int drop, int stack0) {
    if (g_ncopies >= MAX_COPIES) return;
    g_copies[g_ncopies].copy = copy;
    g_copies[g_ncopies].id = id;
    g_copies[g_ncopies].drop = drop;
    g_copies[g_ncopies].stack0 = stack0;
    g_copies[g_ncopies].merged = 0;
    g_copies[g_ncopies].npc0 = 0;
    g_ncopies++;
}
static void remember_merged(void* pile, int id, int drop, int added, int npc0) {
    if (g_ncopies >= MAX_COPIES) return;
    g_copies[g_ncopies].copy = pile;
    g_copies[g_ncopies].id = id;
    g_copies[g_ncopies].drop = drop;
    g_copies[g_ncopies].stack0 = added;
    g_copies[g_ncopies].merged = 1;
    g_copies[g_ncopies].npc0 = npc0;
    g_ncopies++;
}

/* 从池子原件里扣掉 count 个 tid（易物拿走了副本 → 原件也要少掉，否则暗取/摸尸还能再拿一份）。
 * 返回实际扣掉的数量。 */
static int pool_deduct(void* npc, int drop, int tid, int count) {
    void* pool = drop ? char_droppack(npc) : char_stealpack(npc);
    if (!okptr(pool) || tid <= 0 || count <= 0) return 0;
    void* list = pack_contents(pool);
    if (!list) return 0;
    int n = list_size(list);
    for (int i = 0; i < n; ++i) {
        void* it = list_item(list, i);
        if (item_tid(it) != tid || item_stack(it) <= 0) continue;
        int have = item_stack(it);
        int take = count < have ? count : have;
        if (!pack_remove(pool, it, take)) return 0;
        return take;
    }
    return 0;
}

static int pool_remaining(void* npc, int drop, int tid) {
    void* pool = drop ? char_droppack(npc) : char_stealpack(npc);
    if (!okptr(pool) || tid <= 0) return 0;
    void* list = pack_contents(pool);
    int n = list_size(list), sum = 0;
    for (int i = 0; i < n; ++i) {
        void* it = list_item(list, i);
        if (item_tid(it) == tid && item_stack(it) > 0) sum += item_stack(it);
    }
    return sum;
}

static int copy_from_pool(void* src, void* dst, void* npc, int drop, Seen* seen) {
    const char* tag = drop ? "掉落" : "暗取";
    if (!okptr(src) || !okptr(dst) || src == dst) {
        if (drop) LOGI("池子: %s池不可用 src=%p dst=%p", tag, src, dst);
        return 0;
    }
    void* list = pack_contents(src);
    if (!list) return 0;
    void* snap[256];
    int n = list_size(list), m = 0;
    for (int i = 0; i < n && m < 256; ++i) {
        void* it = list_item(list, i);
        if (okptr(it)) snap[m++] = it;
    }
    int made = 0;
    for (int i = 0; i < m; ++i) {
        int tid = item_tid(snap[i]);
        int stack = item_stack(snap[i]);
        if (tid <= 0 || stack <= 0) {
            LOGI("池子: %s#%d tid=%d stack=%d 条目无效", tag, i, tid, stack);
            continue;
        }
        if (seen_has(seen, tid)) {
            LOGI("池子: %s#%d tid=%d 本次已复制过（跳过重复）", tag, i, tid);
            continue;
        }
        if (ledger_has(copy_key(npc, drop, tid))) {
            LOGI("池子: %s#%d tid=%d 已经拿走过（不再生成）", tag, i, tid);
            continue;
        }
        /* NPC 背包若已有同 ID 的堆 → 合并进去（面板不出现两个同 ID 堆），按数量记账。
         * 这样池子里的东西不会因为"背包已有同类"而从易物面板里消失。 */
        int npc0 = 0;
        void* pile = pack_find_id(dst, tid, &npc0);
        if (okptr(pile)) {
            void* c = item_new_by_id(tid, stack);
            if (!c || item_stack(c) <= 0) { LOGI("池子: %s#%d tid=%d 新建实例失败", tag, i, tid); continue; }
            if (!pack_add_merge(dst, c)) { LOGI("池子: %s#%d tid=%d 合并进背包失败", tag, i, tid); continue; }
            int now = item_stack(pile);
            int added = now - npc0;
            if (added <= 0) { LOGI("池子: %s#%d tid=%d 合并后数量没变（跳过）", tag, i, tid); continue; }
            LOGI("池子: %s#%d tid=%d x%d → 并入已有堆 %d→%d", tag, i, tid, stack, npc0, now);
            seen_add(seen, tid);
            remember_merged(pile, tid, drop, added, npc0);
            made++;
            continue;
        }
        void* c = item_new_by_id(tid, stack);
        if (!c) { LOGI("池子: %s#%d tid=%d 新建实例失败", tag, i, tid); continue; }
        if (item_stack(c) <= 0 || !item_valid(c)) {
            LOGI("池子: %s#%d tid=%d 实例无效 stack=%d", tag, i, tid, item_stack(c));
            continue;
        }
        if (!pack_add(dst, c)) { LOGI("池子: %s#%d tid=%d 加进背包失败", tag, i, tid); continue; }
        LOGI("池子: %s#%d tid=%d x%d → 背包（独立堆）", tag, i, tid, stack);
        seen_add(seen, tid);
        remember_copy(c, tid, drop, stack);
        made++;
    }
    return made;
}

/* 模板里有的条目（池子里还没实例化的）→ 自己按 key/values 造副本 */
static int copy_from_template(void* dst, void* npc, int drop, Seen* seen) {
    const char* tag = drop ? "模板掉" : "模板取";
    void* tpl = char_templete(npc);
    if (!okptr(tpl) || !okptr(dst)) return 0;
    void* arr = *(void**)((char*)tpl + (drop ? OFF_CPL_DROP : OFF_CPL_STEAL));
    if (!arr) return 0;
    int n = *(int*)((char*)arr + 0x18);
    if (n <= 0) return 0;
    int made = 0;
    for (int i = 0; i < n; ++i) {
        void* kwv = ((void**)((char*)arr + 0x20))[i];
        if (!okptr(kwv)) continue;
        void* key = *(void**)((char*)kwv + OFF_KWV_KEY);
        if (!okptr(key)) { LOGI("池子: %s#%d key 为空", tag, i); continue; }
        int cnt = 1;
        void* vals = *(void**)((char*)kwv + OFF_KWV_VALUES);
        if (okptr(vals) && list_size(vals) > 0) {
            void* varr = list_at(vals);
            if (okptr(varr)) {
                int v = *(int*)((char*)varr + 0x20);
                if (v > 0) cnt = v;
            }
        }
        void* gii = item_new_by_key(key, cnt);
        if (!gii || !item_valid(gii) || item_stack(gii) <= 0) {
            char b[96];
            LOGI("池子: %s#%d key=%s cnt=%d 解析失败", tag, i, str_to_buf(key, b, sizeof(b)), cnt);
            continue;
        }
        int tid = item_tid(gii);
        if (tid <= 0) { LOGI("池子: %s#%d tid<=0 跳过", tag, i); continue; }
        if (seen_has(seen, tid)) {
            LOGI("池子: %s#%d tid=%d 本次已复制过（跳过重复）", tag, i, tid);
            continue;
        }
        if (ledger_has(copy_key(npc, drop, tid))) {
            LOGI("池子: %s#%d tid=%d 已经拿走过（不再生成）", tag, i, tid);
            continue;
        }
        int npc0 = 0;
        void* pile = pack_find_id(dst, tid, &npc0);
        if (okptr(pile)) {
            int st0 = item_stack(gii);
            if (!pack_add_merge(dst, gii)) { LOGI("池子: %s#%d tid=%d 合并进背包失败", tag, i, tid); continue; }
            int now = item_stack(pile);
            int added = now - npc0;
            if (added <= 0) { LOGI("池子: %s#%d tid=%d 合并后数量没变（跳过）", tag, i, tid); continue; }
            LOGI("池子: %s#%d tid=%d x%d → 并入已有堆 %d→%d", tag, i, tid, st0, npc0, now);
            seen_add(seen, tid);
            remember_merged(pile, tid, drop, added, npc0);
            made++;
            continue;
        }
        if (!pack_add(dst, gii)) { LOGI("池子: %s#%d tid=%d 加进背包失败", tag, i, tid); continue; }
        LOGI("池子: %s#%d tid=%d x%d → 背包（独立堆）", tag, i, tid, cnt);
        seen_add(seen, tid);
        remember_copy(gii, tid, drop, item_stack(gii));
        made++;
    }
    return made;
}

static int tpl_len(void* npc, int drop) {
    void* tpl = char_templete(npc);
    if (!okptr(tpl)) return -1;
    void* arr = *(void**)((char*)tpl + (drop ? OFF_CPL_DROP : OFF_CPL_STEAL));
    return arr ? *(int*)((char*)arr + 0x18) : -1;
}

static void cleanup_copies() {
    if (g_ncopies == 0) { g_npc = nullptr; g_bag = nullptr; return; }
    int removed = 0;
    if (okptr(g_bag)) {
        for (int i = 0; i < g_ncopies; ++i) {
            void* c = g_copies[i].copy;
            int stack = item_stack(c);
            if (stack <= 0) continue;
            int take = stack;
            if (g_copies[i].merged) {
                /* 合并形态：只撤我们加进去的那部分（当前数量 - 合并前数量），
                 * 玩家若已从这堆里拿走一部分就撤得更少 —— 绝不碰 NPC 原货。 */
                int extra = stack - g_copies[i].npc0;
                if (extra <= 0) continue;
                take = extra < g_copies[i].stack0 ? extra : g_copies[i].stack0;
            }
            if (pack_remove(g_bag, c, take)) removed++;
        }
    }
    LOGI("易物: 清掉显示副本 %d/%d", removed, g_ncopies);
    g_ncopies = 0;
    g_npc = nullptr;
    g_bag = nullptr;
}

/* ================================================================ hook 主体 */

/* ① 作价恒占优：两道门槛 */
static long long (*orig_total_value)(void*, float) = nullptr;

static long long hook_total_value(void* self, float rate) {
    long long v = orig_total_value ? orig_total_value(self, rate) : 0;
    if (!F_TRADE_WIN) return v;
    return v < 1 ? 1 : v;
}

static long long (*orig_value_offset)() = nullptr;

static long long hook_value_offset() {
    long long v = orig_value_offset ? orig_value_offset() : 0;
    if (!F_TRADE_WIN) return v;
    return v < 0 ? 0 : v;
}

/* ②③ StartTrading / ConfirmTrading / CancelTrading */
static void (*orig_tr_start)(void* npc) = nullptr;
static void (*orig_tr_confirm)() = nullptr;
static void (*orig_tr_cancel)() = nullptr;

/* 上一次异常关闭（没走 Cancel/Confirm）时交易区还留着东西，先退回，再清副本 */
static void trade_before_start() {
    cleanup_copies();

    void* old = tr_npc();
    void* bag = okptr(old) ? char_backpack(old) : nullptr;
    void* nz = tr_npc_zone();
    if (okptr(nz)) {
        int ghost = purge_invalid(nz);
        if (ghost) LOGI("易物: 清掉上次残留空壳 %d（NPC 区）", ghost);
        if (okptr(bag) && count_valid(nz) > 0) {
            int n = transfer(nz, bag);
            if (n) LOGI("易物: 上次残留 NPC 区 → 背包 %d 件", n);
        }
    }
    void* pz = tr_player_zone();
    if (okptr(pz)) {
        int ghost = purge_invalid(pz);
        if (ghost) LOGI("易物: 清掉上次残留空壳 %d（玩家区）", ghost);
        void* inv = team_inventory();
        if (okptr(inv) && count_valid(pz) > 0) {
            int n = transfer(pz, inv);
            if (n) LOGI("易物: 上次残留玩家区 → 队伍背包 %d 件", n);
        }
    }
}

static void trade_after_start(void* npcArg) {
    g_npc = npcArg ? npcArg : tr_npc();
    if (!okptr(g_npc)) { LOGI("易物: StartTrading 后拿不到 NPC"); return; }
    g_bag = char_backpack(g_npc);
    if (!okptr(g_bag)) { LOGI("易物: NPC 背包为空"); return; }

    /* 顺手把老存档里被拆开的同类堆并回去（与池子开关无关） */
    void* inv0 = team_inventory();
    if (okptr(inv0)) {
        int fixed = merge_dup_piles(inv0);
        if (fixed) { LOGI("易物: 队伍背包合并同 ID 重复堆 %d 处", fixed); refresh_ui(); }
    }
    int fixedBag = merge_dup_piles(g_bag);
    if (fixedBag) LOGI("易物: NPC 背包合并同 ID 重复堆 %d 处", fixedBag);

    if (!F_TRADE_POOLS) { LOGI("易物: 池子显示已关闭"); return; }

    int purged = purge_invalid(g_bag);
    if (purged) LOGI("易物: 清掉 NPC 背包空壳 %d", purged);
    int before = count_valid(g_bag);

    void* drop = char_droppack(g_npc);
    void* steal = char_stealpack(g_npc);
    char nb[64];
    str_to_buf(CALL(void*, RVA_CHAR_TPL_NAME, void*)(g_npc, nullptr), nb, sizeof(nb));

    LOGI("易物: NPC=%s 背包=%d 击杀池=%d 暗取池=%d 模板掉=%d 模板取=%d", nb, before,
         count_valid(drop), count_valid(steal), tpl_len(g_npc, 1), tpl_len(g_npc, 0));

    ledger_load();
    Seen seen;
    seen.n = 0;
    int a = copy_from_pool(drop, g_bag, g_npc, 1, &seen);
    int b = copy_from_pool(steal, g_bag, g_npc, 0, &seen);
    int c = copy_from_template(g_bag, g_npc, 1, &seen);
    int d = copy_from_template(g_bag, g_npc, 0, &seen);
    if (a + b + c + d > 0)
        LOGI("易物: 副本 掉落池%d 暗取池%d 模板掉%d 模板取%d（池子原件未动）", a, b, c, d);
    else
        LOGI("易物: 没有可复制的池子内容");

    refresh_ui();
    LOGI("易物: 背包 %d → %d", before, count_valid(g_bag));
}

static void trade_after_confirm() {
    if (!F_TRADE_TAKEALL) { LOGI("易物: 搬空已关闭"); cleanup_copies(); return; }

    void* inv = team_inventory();
    if (!okptr(inv)) { LOGE("易物: 找不到队伍背包，跳过搬空"); cleanup_copies(); return; }
    int before = count_valid(inv);

    void* pz = tr_player_zone();
    void* nz = tr_npc_zone();
    if (okptr(pz)) purge_invalid(pz);
    if (okptr(nz)) purge_invalid(nz);

    int a = okptr(pz) ? transfer(pz, inv) : 0;
    int b = okptr(nz) ? transfer(nz, inv) : 0;
    int c = 0;
    void* bag = okptr(g_npc) ? char_backpack(g_npc) : g_bag;
    if (okptr(bag)) {
        purge_invalid(bag);
        c = transfer(bag, inv);
    }

    /* 副本被拿走 → ① 记台账（以后不再给这个 NPC 造同一件）
     *              ② 从池子原件里扣掉同样数量 —— 否则暗取/摸尸还能再拿一份
     * 合并形态按数量算：搬空后那堆被整体搬走（实例被 RemoveItem 归零），
     * 拿走量 = (合并前数量 + 我们加的量) - 剩余，再按"最多只算副本量"截断；
     * 这一步只给（原有+副本）混合堆用的，独立堆仍按实例 Stack 差算。 */
    int marked = 0, deducted = 0;
    for (int i = 0; i < g_ncopies; ++i) {
        void* cp = g_copies[i].copy;
        if (!okptr(cp)) continue;
        int left = item_stack(cp);
        int took;
        if (g_copies[i].merged) {
            int total = g_copies[i].npc0 + g_copies[i].stack0;
            took = total - (left > 0 ? left : 0);
            if (took < 0) took = 0;
            if (took > g_copies[i].stack0) took = g_copies[i].stack0;
        } else {
            took = g_copies[i].stack0 - (left > 0 ? left : 0);
        }
        if (took <= 0) continue;
        if (!okptr(g_npc)) continue;
        int got = pool_deduct(g_npc, g_copies[i].drop, g_copies[i].id, took);
        /* 台账只在「池子里这件被扣干」时才记：只拿一部分的话下次开面板还能补剩下的；
         * 模板条目没有实物可扣（remain 恒 0）→ 仍然一次记掉，避免无实物却无限生成。 */
        int remain = pool_remaining(g_npc, g_copies[i].drop, g_copies[i].id);
        int markOk = remain <= 0;
        if (markOk) ledger_mark(g_npc, g_copies[i].drop, g_copies[i].id);
        marked += markOk;
        deducted += got;
        LOGI("池子: %s tid=%d 拿走%d → 池子扣%d 剩%d%s", g_copies[i].drop ? "掉落" : "暗取",
             g_copies[i].id, took, got, remain, markOk ? " 台账+1" : "");
    }

    int after = count_valid(inv);
    LOGI("易物: 搬空 玩家区%d NPC区%d NPC背包%d 记账%d 扣池%d | 队伍背包 %d → %d%s", a, b, c,
         marked, deducted, before, after, (after > before ? "  OK" : "  <<< 没进来"));
    int fixed = merge_dup_piles(inv);
    if (fixed) { LOGI("易物: 队伍背包合并同 ID 重复堆 %d 处", fixed); after = count_valid(inv); }
    log_dup_piles("队伍背包", inv);
    refresh_ui();
    g_ncopies = 0;
    g_npc = nullptr;
    g_bag = nullptr;
}

static void trade_after_cancel() {
    cleanup_copies();
}

static void hook_tr_start(void* npc) {
    trade_before_start();
    if (orig_tr_start) orig_tr_start(npc);
    trade_after_start(npc);
}

static void hook_tr_confirm() {
    if (orig_tr_confirm) orig_tr_confirm();
    trade_after_confirm();
}

static void hook_tr_cancel() {
    if (orig_tr_cancel) orig_tr_cancel();
    trade_after_cancel();
}

void trade_install() {
    hook_install("易物.ValueOffset", (void*)(g_base + RVA_TR_VALUE_OFFSET),
                 (void*)hook_value_offset, (void**)&orig_value_offset);
    hook_install("易物.GetTotalValue", (void*)(g_base + RVA_PACK_GET_TOTAL),
                 (void*)hook_total_value, (void**)&orig_total_value);
    hook_install("易物.StartTrading", (void*)(g_base + RVA_TR_START),
                 (void*)hook_tr_start, (void**)&orig_tr_start);
    hook_install("易物.ConfirmTrading", (void*)(g_base + RVA_TR_CONFIRM),
                 (void*)hook_tr_confirm, (void**)&orig_tr_confirm);
    hook_install("易物.CancelTrading", (void*)(g_base + RVA_TR_CANCEL),
                 (void*)hook_tr_cancel, (void**)&orig_tr_cancel);
}

void trade_hook_fire() {}
