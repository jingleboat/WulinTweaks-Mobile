/*
 * WulinTweaks · 剧情分支展开（Branch Flattener）  v80-generic
 * =========================================================
 * 通用规则，不再针对某一张图。与 PC 端 BranchFlatten.cs v2 完全同一套逻辑。
 *
 * 规则（一句话）：
 *   把「在队侧（Negate=0）」的**出口**改接到「不在队侧那道门（Negate=1）」，
 *   再让这两道门都判真 —— 于是先播在队侧的内容，接着播不在队侧的内容。
 *
 * 为什么成立：
 *   一对门的父节点出边是 [G0(Negate=0), G1(Negate=1)]，引擎按顺序问、第一个满足的就走；
 *   两道都真时永远走 G0。所以只把「G0 分支走完之后指向外面那条边」改成指向 G1，
 *   G1 侧原样保留（它连着的是一对门还是汇合点都无所谓）。
 *     · A 类：一侧「播放剧情」/ 一侧「直通」 —— 改完变成梯子顺序展开
 *     · B 类：两侧都是「播放剧情」 —— 改完变成"先播在队那段、再播不在队那段"
 *
 * 内容区域怎么界定（不靠"取第 1 条出边"，那样遇到多节点剧情/内部带分支就废）：
 *   region = reach(G0) − reach(G1) − {G0, G1}
 *   也就是只有在队侧能到、不在队侧到不了的节点 = 该侧独占内容；
 *   汇合点和之后的流程因为两侧都能到，天然被排除。
 *   exits = region 内所有指向 region 外的边（已指向 G1 的除外）
 *   动作  = exits 全部改指 G1；G0、G1 的条件登记进「恒真」表
 *
 * 跳过条件（宁可不动，也不要改坏）：
 *   · region 为空（两侧内容重叠）、region 过大、region 没有对外出口
 *   · 与另一对门的 region 相交（嵌套门对）→ 两对都跳过，留给人工
 *   · partnerID <= 0 的伪条件；两侧都不是「单节点剧情」的 X 类
 *
 * 接管方式（v87 起与 PC 完全对齐）：
 *   ① 条件层：把两道门的**条件节点**登记进 g_flat_cond，由 hook_check_partner /
 *      hook_check_joined 在判定入口直接走基类实现（= 丢掉这一个条件）。
 *   ② 门层（v87 补的，PC 一直是这么做的）：把两道**门**（HoldNode）本身登记进 g_flat_gate，
 *      由 hook_hold_check 在 HoldNode.CheckCondition 之后整门判真。手机原来只有 ①，
 *      于是「门挂的不是伙伴/在队条件」时就不生效（改线与判定不一致）。
 * ⚠️ 仍然**不写 partnerID / Negate 字段** —— 那两个字段被「显示熟悉后姓名」用来
 *    记录"放宽放过了谁"，抹掉姓名功能就失效（v69 的教训）。
 *
 * 结构偏移（V1.2.0820B131）：
 *   XNode.Node        graph 0x18
 *   XNode.NodePort    connections 0x20
 *   NodePort.PortConnection  node 0x18
 *   NodeGraph         nodes 0x18（List<Node>）
 *   StoryScenarioNode ScenarioKey 0x60
 *   HavePartnerCondition     partnerID 0x58(int)  Negate 0x5C(bool)
 *   il2cpp 数组元素起点 0x20（List._items 指向数组对象）
 */
#include "common.h"

#include <string.h>

extern volatile int F_FLAT;      /* 面板第 8 行开关 */
extern volatile int F_STORY;     /* 剧情条件放宽（姓名窗口也受它约束） */

/* ---------------------------------------------------------------- 偏移 */
#define OFF_NODEPORT_CONNS   0x20
#define OFF_PORTCONN_NODE    0x18
#define OFF_GRAPH_NODES      0x18
#define OFF_SSN_SCENARIOKEY  0x60
#define OFF_HP_PARTNER_ID    0x58
#define OFF_HP_NEGATE        0x5C

/* ---------------------------------------------------------------- 容量 */
#define FLAT_MAX_NODES   2050        /* 单图节点上限（最大的图约 1300） */
#define FLAT_MAX_EDGES   8200
#define FLAT_MAX_PAIRS   48
#define FLAT_MAX_CONDS   2048        /* v234：128 → 2048。原来处理 400+ 张图就满，导致后面的图条件旁路失效 */
#define FLAT_MAX_GATES   32768       /* v249：8192 → 32768（每指针 8B，共 256KB）。门表满 → 改过线的门不放行 → 卡死 */
#define FLAT_MAX_REGION  512         /* region 节点上限 */
#define FLAT_DONE_MAX    512         /* v234：96 → 512（处理过的图超过 96 张后环形挤出，会重复扫图） */
#define FLAT_LOG_GRAPH   400    /* v222：40 → 400（原来第 41 张图之后的汇总全被吞，R10049-2-1 就是受害者） */

/* ---------------------------------------------------------------- 列表访问 */
/* ⚠️ il2cpp 数组（T[]）的元素不是从数组对象头开始，而是 +0x20：
 *      klass 0x00 | monitor 0x08 | bounds 0x10 | max_length 0x18 | 元素从 0x20
 *   而 List<T> 的 _items 字段（0x10）指向的就是这个数组对象。 */
static int lcnt(void* lst) {
    if ((uintptr_t)lst < 0x10000) return 0;
    int n = *(int*)((char*)lst + OFF_LIST_SIZE);
    return (n < 0 || n > 65536) ? 0 : n;
}

static void* litem(void* lst, int i) {
    if ((uintptr_t)lst < 0x10000 || i < 0) return nullptr;
    void* a = *(void**)((char*)lst + OFF_LIST_ITEMS);
    if ((uintptr_t)a < 0x10000) return nullptr;
    void* v = ((void**)((char*)a + OFF_ARRAY_DATA))[i];
    return ((uintptr_t)v > 0x10000) ? v : nullptr;
}

/* ---------------------------------------------------------------- 小工具 */
static const char* unity_name(void* o, char* buf, int cap) {
    buf[0] = 0;
    if ((uintptr_t)o < 0x10000) return buf;
    typedef void* (*fn_getname)(void*);
    void* s = ((fn_getname)(g_base + RVA_GET_NAME))(o);
    if ((uintptr_t)s < 0x10000) return buf;
    str_to_buf(s, buf, cap);
    return buf;
}

static const char* cls_name(void* o, char* buf, int cap) {
    buf[0] = 0;
    if ((uintptr_t)o < 0x10000 || !A_obj_get_class || !A_class_get_name) return buf;
    void* k = A_obj_get_class(o);
    if ((uintptr_t)k < 0x10000) return buf;
    const char* n = A_class_get_name(k);
    if (!n) return buf;
    int i = 0;
    for (; n[i] && i < cap - 1; ++i) buf[i] = n[i];
    buf[i] = 0;
    return buf;
}

/* 按端口名取 NodePort —— 走托管方法 Node.GetOutputPort / Node.GetPort。
 * 端口名是 ASCII，用 il2cpp_string_new 造一个托管字符串传进去。
 * ⚠️ 不要改成读端口字段：手机端当初就是用这两个方法才绕开「ports 字典的
 *    keys/values 在运行期被清空」那个坑。 */
static void* node_port(void* node, const char* want, int output_only) {
    if ((uintptr_t)node < 0x10000 || !A_string_new) return nullptr;
    void* s = A_string_new(want);
    if ((uintptr_t)s < 0x10000) return nullptr;
    typedef void* (*fn_getport)(void*, void*);
    uintptr_t rva = output_only ? RVA_NODE_GET_OUTPUTPORT : RVA_NODE_GET_PORT;
    void* p = ((fn_getport)(g_base + rva))(node, s);
    return ((uintptr_t)p > 0x10000) ? p : nullptr;
}

static int port_conn_count(void* port) {
    if ((uintptr_t)port < 0x10000) return 0;
    return lcnt(*(void**)((char*)port + OFF_NODEPORT_CONNS));
}

static void* port_conn_node(void* port, int idx) {
    if ((uintptr_t)port < 0x10000 || idx < 0) return nullptr;
    void* cs = *(void**)((char*)port + OFF_NODEPORT_CONNS);
    if (idx >= lcnt(cs)) return nullptr;
    void* c = litem(cs, idx);
    if ((uintptr_t)c < 0x10000) return nullptr;
    void* t = *(void**)((char*)c + OFF_PORTCONN_NODE);
    return ((uintptr_t)t > 0x10000) ? t : nullptr;
}

static int port_set_conn_node(void* port, int idx, void* target) {
    if ((uintptr_t)port < 0x10000 || (uintptr_t)target < 0x10000) return 0;
    void* cs = *(void**)((char*)port + OFF_NODEPORT_CONNS);
    if (idx < 0 || idx >= lcnt(cs)) return 0;
    void* c = litem(cs, idx);
    if ((uintptr_t)c < 0x10000) return 0;
    *(void**)((char*)c + OFF_PORTCONN_NODE) = target;
    return 1;
}

/* ================================================================ 恒真表 */
/* 被接管的条件节点（门的 conditionPort 连着的那个 ConditionNode） */
static void* g_flat_cond[FLAT_MAX_CONDS];
static int g_flat_cond_n = 0;

static void flat_add_cond(void* cond) {
    if ((uintptr_t)cond < 0x10000) return;
    for (int i = 0; i < g_flat_cond_n; ++i)
        if (g_flat_cond[i] == cond) return;
    if (g_flat_cond_n < FLAT_MAX_CONDS) g_flat_cond[g_flat_cond_n++] = cond;
}


/* v87：被接管的**门**（HoldNode）本身 —— 与 PC 的 BranchFlatten._gateForced 对齐。
 * 为什么需要它：手机上条件层只能在 IsJoinedTeamCondition / HavePartnerCondition 两个
 * CheckAllConnectNode 里绕过条件，覆盖面比 PC 窄。PC 是在 HoldNode.CheckCondition 上
 * 整门判真，与条件类型无关。这里把门指针也记一份，由 hook_hold_check 兜底整门判真。
 * ⚠️ 故意**不看 F_FLAT**（与 PC 的 IsGateForced 一致）：线已经改指 G1，
 *    若中途关开关导致门又判假，就会「出口指向别处 + 门不放行」→ 剧情卡死。 */
static void* g_flat_gate[FLAT_MAX_GATES];
static int g_flat_gate_n = 0;

static void flat_add_gate(void* gate) {

    if ((uintptr_t)gate < 0x10000) return;
    for (int i = 0; i < g_flat_gate_n; ++i)
        if (g_flat_gate[i] == gate) return;                 /* 已登记 → 直接返回（去重） */
    if (g_flat_gate_n < FLAT_MAX_GATES) {
        g_flat_gate[g_flat_gate_n++] = gate;
    } else {
        /* v248：原来这里还多留了一条 `if (...) g_flat_gate[g_flat_gate_n++] = gate;`，
         * 导致**同一个门被登记两次**（每对门多占一个槽位）。虽然查表只看存在性、功能不受影响，
         * 但表容量被白白消耗一半。已删。 */
        extern int g_full_log;
        if (g_full_log < 5) { g_full_log++; LOGI("[flat-full] 门表已满（%d）→ 之后的门不会被强制放行", FLAT_MAX_GATES); }
    }
}
int g_full_log = 0;
/* 这道门是否已被「分支展开」接管（= 无条件判真）。由 hook_hold_check 调用。 */
int flatten_gate_forced(void* gate) {
    if ((uintptr_t)gate < 0x10000) return 0;
    for (int i = 0; i < g_flat_gate_n; ++i)
        if (g_flat_gate[i] == gate) return 1;
    return 0;
}

/* 这个条件节点是否已被接管。v87：去掉 F_FLAT 判据（同 PC），
 * 只用来给条件层做快速旁路，开关只决定「新图还要不要打补丁」。 */
int flatten_cond_always_true(void* cond) {
    if ((uintptr_t)cond < 0x10000) return 0;
    for (int i = 0; i < g_flat_cond_n; ++i)
        if (g_flat_cond[i] == cond) return 1;
    return 0;
}

/* ================================================================ 图处理状态 */
/* 处理过的图指针（环形）。通用规则下「任何图都要扫一次」，所以比原来大得多，
 * 否则指针一被挤出环形就会被反复重扫（每次都是几千次 il2cpp 调用）。 */
/* v218：去重表加**指纹校验**。
 *
 * 原来只比裸指针（`g_done[i] == g`），而 Unity 的剧情图对象会被释放、地址被下一张图复用：
 * 于是新图命中旧条目 → 被判「已经处理过」→ 整张图一次都不展开。
 * 实测后果：R10049-2-1（李乐雅「别去招惹妙音坊的人」那段所在图）在某一轮里
 * 日志中连一条 `[flat] 图 R10049-2-1：…` 都没有 —— 它不是「展开 0」，是根本没被处理。
 *
 * 指纹 = (节点数, 首节点指针)。图被重建时这两项都会变，据此判定「同地址但不同图」→ 重新处理。 */
typedef struct { void* g; int n; void* first; } FLAT_DONE_ENT;
static FLAT_DONE_ENT g_done[FLAT_DONE_MAX];
static int g_done_n = 0;
static int g_done_rr = 0;
static int g_log_graph = 0;
static int g_log_pair = 0;

static void graph_fp(void* g, int* nOut, void** firstOut) {
    *nOut = 0; *firstOut = nullptr;
    if ((uintptr_t)g < 0x10000) return;
    void* nodes = *(void**)((char*)g + OFF_GRAPH_NODES);
    if ((uintptr_t)nodes < 0x10000) return;
    *nOut = lcnt(nodes);
    *firstOut = litem(nodes, 0);
}




static int flat_done(void* g) {
    int n = 0; void* first = nullptr;
    graph_fp(g, &n, &first);

    /* v253：**删除 v229 的「强制重新处理」（flat-force）**。
     *
     * 它让 R10049-2-1 绕过去重最多重扫 8 次，而重扫是**破坏性**的：
     *   第 1 次：门对 4、改线 14 条（正确）
     *   第 2 次：图已被改过 → 只识别出 1 对 → 它成了「最后一对」→ next_g0=-1
     *           → 回落自己的 G1 门 → 而 G1 在 v244 已被改指 G0
     *           → 剧情 → 标记 → G1 → G0 → 剧情 …  死循环
     * 实测（v252 日志）：_4_2→_4_3→_4_4→_4_5 重复了 12 次。
     *
     * 当初加它是为了排查「图被误判 done」，那个问题已经由其它修正解决，不需要了。 */

    for (int i = 0; i < g_done_n; ++i) {
        if (g_done[i].g != g) continue;
        if (g_done[i].n == n && g_done[i].first == first) {
            extern int g_done_hit_log;
            char gb3[96];
            const char* gn3 = unity_name(g, gb3, sizeof(gb3));
            if (g_done_hit_log < 40) {
                g_done_hit_log++;

                LOGI("[flat-done] 命中跳过 图=%s g=%p 指纹=(%d,%p) 表内=(%d,%p)",
                     (gn3 && gn3[0]) ? gn3 : "?", g, n, first, g_done[i].n, g_done[i].first);
            }
            return 1;
        }
        /* 同地址但指纹不同 = 图被重建 → 刷新指纹，允许重新处理 */
        g_done[i].n = n;
        g_done[i].first = first;
        return 0;
    }
    return 0;
}
int g_done_hit_log = 0;     /* v219：flat_done 命中日志计数 */
int g_nograph_log = 0;      /* v221：读不到图指针的日志计数 */
int g_zero_log = 0;         /* v220：0 对门日志计数 */

/* v227：0 对门的重试表（按图记账，限次 32） */
#define NOPAIR_MAX    32
#define NOPAIR_TRIES  32
static struct { void* g; int n; } g_nopair[NOPAIR_MAX];
static int g_nopair_n = 0;
static int nopair_inc(void* g) {

    for (int i = 0; i < g_nopair_n; ++i)
        if (g_nopair[i].g == g) return ++g_nopair[i].n;
    if (g_nopair_n < NOPAIR_MAX) {
        g_nopair[g_nopair_n].g = g; g_nopair[g_nopair_n].n = 1; g_nopair_n++; return 1;
    }
    return NOPAIR_TRIES;   /* 表满：按超限处理，避免无限重扫 */
}
static void nopair_clear(void* g) {
    for (int i = 0; i < g_nopair_n; ++i)
        if (g_nopair[i].g == g) { g_nopair[i] = g_nopair[g_nopair_n - 1]; g_nopair_n--; return; }
}


/* v241：记录每条被改过的线（原目标），读档时按它还原。
 *
 * 为什么需要：flat_reset 会清空 g_done / g_flat_gate，但如果**不把改线还原**，
 * 重新处理时图还是改过的状态（门的出边数变了）→ 配不出门对 → 重试 32 次放弃
 * → 门表空 → 门不被接管 → 剧情卡住不播。
 * 实测（v240 日志）：读档后 8 次全是「门对 0 → 放弃」，随后 gate-miss，剧情完全不动。 */
typedef struct { void* port; int slot; void* orig; } FLAT_REWIRE;
static FLAT_REWIRE g_rewire[FLAT_MAX_EDGES];
static int g_rewire_n = 0;

/* v239：读档/换档时清空「分支展开」的全部状态。
 *
 * 这些表都是进程级全局，而展平改的是**内存里的图对象**：读档/换档后图会重新加载、
 * 改线全部丢失，但表还以为「处理过」→ 新存档走到同一段剧情时被 flat_done 判「已展开」
 * 直接跳过（用户实测：新建存档后李乐雅那段不再触发，只有重启游戏才恢复）。
 *
 * 清空后新存档里每张图会被重新扫描、重新改线；门表一并清掉，避免把旧存档的门指针
 * 套到新图上（图重建后指针会变，旧指针既无用又占表）。 */
void flat_reset(void) {

    /* v241：**先把改过的线还原**，再清表。
     * 顺序不能反 —— 不还原的话图仍是改过的状态，重新处理会「门对 0」直接放弃，
     * 结果是门表空、门不被接管、剧情卡住（v240 实测）。 */
    for (int i = 0; i < g_rewire_n; ++i) {
        if ((uintptr_t)g_rewire[i].port > 0x10000 && (uintptr_t)g_rewire[i].orig > 0x10000)
            port_set_conn_node(g_rewire[i].port, g_rewire[i].slot, g_rewire[i].orig);
    }
    g_rewire_n = 0;

    g_done_n = 0;
    g_done_rr = 0;
    g_flat_cond_n = 0;
    g_flat_gate_n = 0;
    g_nopair_n = 0;

    g_log_graph = 0;
    g_log_pair = 0;
    g_zero_log = 0;
    g_done_hit_log = 0;
    g_nograph_log = 0;

}
static int g_nopair_try = 0;  /* v225：0 对门但 API 未就绪时的重试计数 */

static void flat_mark_done(void* g, const char* reason) {
    if ((uintptr_t)g < 0x10000) return;
    int n = 0; void* first = nullptr;
    graph_fp(g, &n, &first);
    g_done[g_done_rr].g = g;
    g_done[g_done_rr].n = n;
    g_done[g_done_rr].first = first;
    g_done_rr = (g_done_rr + 1) % FLAT_DONE_MAX;
    if (g_done_n < FLAT_DONE_MAX) g_done_n++;

    /* v228：登记 done 的**原因**，无条件打前 100 条。
     * 之前四次排查全部失效，是因为：日志配额（40/60/300/400）全被打满、
     * 而且 force 判据依赖图名，图名在剧情加载早期读不到 → 强制打印也失效。
     * 这里只靠指针，不依赖图名、不占任何配额。 */
    extern int g_markdone_log;
    if (g_markdone_log < 100) {
        g_markdone_log++;
        char gb4[96];
        const char* gn4 = unity_name(g, gb4, sizeof(gb4));
        LOGI("[mark-done] 图=%s g=%p 指纹=(%d,%p) 原因=%s",
             (gn4 && gn4[0]) ? gn4 : "?", g, n, first, reason ? reason : "?");
    }
}
int g_markdone_log = 0;
/* ================================================================ 单图工作区 */
typedef struct { int src; int tgt; int slot; void* port; } FLAT_EDGE;
typedef struct { int g0, g1; int pid; int both_story; int reg_n; int ex_start; int ex_n; int parent; } FLAT_PAIR;

static void*          g_nd[FLAT_MAX_NODES];      /* 节点指针 */
static int            g_e0[FLAT_MAX_NODES];      /* 该节点第一条出边下标 */
static int            g_ec[FLAT_MAX_NODES];      /* 该节点出边条数 */
static void*          g_cond[FLAT_MAX_NODES];    /* 门的条件节点（非门为 0） */
static FLAT_EDGE      g_edge[FLAT_MAX_EDGES];
static FLAT_PAIR      g_pair[FLAT_MAX_PAIRS];

/* v271：**整链决策** 用的两张表。
 *
 * 背景（文档第二十八章）：同一父节点 / 同一条梯子链上的门对必须一起决定展开或不展开。
 *   · 全部展开 → 并联改串联，确定
 *   · 全部不展开 → 保持原版互斥，确定
 *   · 部分展开 → 一边串联一边并联，引擎「无法确定走哪个分支」（官方文档第 251 段）
 *              → 实测 R10049-0：5 段剧情几毫秒内连续触发，玩家看不见
 *
 * 链的定义（用户明确要求「宽松」判据）：
 *   pair i 的 G1 门出边 → pair j 的**父节点**、或 j 的 G0/G1 门 → i 与 j 同链。
 *   离线对比：宽松比严格只少 5 对（489 vs 494），组数 438 vs 548（链更完整）。
 */
static int g_will_expand[FLAT_MAX_PAIRS];   /* 该对最终会不会展开 */

/* v271：**未展开的链 → 禁止放宽**。
 *
 * 用户规则：「整组不展开时不放宽」（只针对门对，孤立门不改规则）。
 *
 * 为什么：展开失败时如果仍然放宽，G0（在队）和 G1（不在队）会同时为真 →
 * 违反官方设计范式（文档第 251 段「并联的多个流程判断必须互斥」）→ 引擎行为未定义。
 * 不放宽则回到原版互斥 → 引擎能唯一确定 → 按「是否在队」各走一边。
 */
static void* g_pair_ban[FLAT_MAX_CONDS];
static int   g_pair_ban_n = 0;

static void flat_add_ban(void* cond) {
    if ((uintptr_t)cond < 0x10000) return;
    for (int i = 0; i < g_pair_ban_n; ++i)
        if (g_pair_ban[i] == cond) return;
    if (g_pair_ban_n < FLAT_MAX_CONDS) g_pair_ban[g_pair_ban_n++] = cond;
}

/* 供 keep_original_for 调用：这个条件是否属于「未展开的链」→ 禁止放宽 */
/* v273（P0-6）：**这张图是否已经完成决策**（扫描 + 逐对判定 + 整链决策 + 改线）。
 *
 * 用途：keep_original_for 在放宽之前必须先确认图已决策，否则会出现
 * 「图还在 BUILDING，g_pair_ban 还是空的 → 落进普通放宽」的时序窗口。
 *
 * 判据：图已在 g_done 表里（flatten_try 处理完才登记），或者它压根没有门对（无需决策）。
 * 更精确的做法是维护 UNSEEN/BUILDING/DECIDED_* 状态机，但 g_done 已经能表达「决策完成」，
 * 这里先用它 —— 引入完整状态机需要改动 flatten_try 的生命周期，风险更大。
 */
int flatten_is_banned_cond(void* cond) {
    if ((uintptr_t)cond < 0x10000) return 0;
    for (int i = 0; i < g_pair_ban_n; ++i)
        if (g_pair_ban[i] == cond) return 1;
    return 0;
}
static int            g_exit[FLAT_MAX_EDGES];    /* 各 pair 的出口边下标（按 pair 顺序排） */
static unsigned char  g_reg[FLAT_MAX_PAIRS][FLAT_MAX_NODES];  /* 每对的 region 位图 */
static int            g_nnodes, g_nedges, g_npairs, g_nexit;

/* 节点指针 → 下标 的小哈希（BFS 要反复查） */
#define FLAT_HASH 4096
static void* g_hk[FLAT_HASH];
static int   g_hv[FLAT_HASH];

static unsigned hash_ptr(void* p) {
    uintptr_t x = (uintptr_t)p;
    x ^= x >> 13; x *= 0x9E3779B1u; x ^= x >> 15;
    return (unsigned)x & (FLAT_HASH - 1);
}

static void hash_put(void* p, int v) {
    unsigned h = hash_ptr(p);
    for (int i = 0; i < 64; ++i) {
        unsigned k = (h + i) & (FLAT_HASH - 1);
        if (!g_hk[k]) { g_hk[k] = p; g_hv[k] = v; return; }
    }
}

static int hash_get(void* p) {
    unsigned h = hash_ptr(p);
    for (int i = 0; i < 64; ++i) {
        unsigned k = (h + i) & (FLAT_HASH - 1);
        if (!g_hk[k]) return -1;
        if (g_hk[k] == p) return g_hv[k];
    }
    return -1;
}

/* 扫图：收节点 + 出边 + 门→条件节点 */

static int g_build_fail_log = 0;   /* v230：flat_build 失败的独立计数器（不占 g_log_graph 配额） */

static int flat_build(void* g) {
    void* nodes = *(void**)((char*)g + OFF_GRAPH_NODES);
    int n = lcnt(nodes);
    if (n <= 2 || n > FLAT_MAX_NODES) {
        if (g_build_fail_log < 50) {
            g_build_fail_log++;
            char bb[96];
            LOGI("[build-fail] 图=%s 节点列表长度=%d（越界或过小，上限 %d）",
                 unity_name(g, bb, sizeof(bb)), n, FLAT_MAX_NODES);
        }
        return 0;
    }

    memset(g_hk, 0, sizeof(g_hk));
    g_nnodes = 0; g_nedges = 0; g_nexit = 0;

    for (int i = 0; i < n; ++i) {
        void* nd = litem(nodes, i);
        if ((uintptr_t)nd < 0x10000) continue;
        if (*(void**)((char*)nd + OFF_NODE_GRAPH) != g) continue;   /* 数组读偏的保险 */
        g_nd[g_nnodes] = nd;
        hash_put(nd, g_nnodes);
        g_nnodes++;
    }

    if (g_nnodes < 2) {
        /* v230：过滤后节点太少 —— 说明节点的 graph 字段与本图对不上（OFF_NODE_GRAPH 偏移或子图复用）。
         * 这条日志不占 g_log_graph 配额，否则永远看不到。 */
        if (g_build_fail_log < 50) {
            g_build_fail_log++;
            char bb2[96];
            LOGI("[build-fail] 图=%s 列表长度=%d 过滤后节点=%d（graph 字段对不上本图 g=%p）",
                 unity_name(g, bb2, sizeof(bb2)), n, g_nnodes, g);
        }
        return 0;
    }

    /* 出边：只看 OutPort（剧情走线就在它上面）；顺便记下门的条件节点 */
    for (int i = 0; i < g_nnodes; ++i) {
        void* nd = g_nd[i];
        g_cond[i] = nullptr;
        g_e0[i] = g_nedges;
        g_ec[i] = 0;

        void* op = node_port(nd, "OutPort", 1);
        int cnt = port_conn_count(op);
        if (cnt > 0 && cnt <= 32) {
            for (int k = 0; k < cnt; ++k) {
                void* t = port_conn_node(op, k);
                int ti = hash_get(t);
                if (ti < 0) continue;
                if (g_nedges >= FLAT_MAX_EDGES) break;
                g_edge[g_nedges].src = i;
                g_edge[g_nedges].tgt = ti;
                g_edge[g_nedges].slot = k;
                g_edge[g_nedges].port = op;
                g_nedges++;
                g_ec[i]++;
            }
        }

        void* cp = node_port(nd, "conditionPort", 0);
        if ((uintptr_t)cp > 0x10000) g_cond[i] = port_conn_node(cp, 0);
    }
    return 1;
}

/* v256：从 start 出发（沿 OutPort）是否能到达 target。用 g_reg 的临时位图做 visited。
 * 用于「最后一对改指 G1 之前」的成环校验 —— 只在改线阶段跑一次，不在热路径。 */




/* v270：目标节点除 exclude 之外还有没有别的入边（按**节点指针**判断）。
 *
 * 用途：G1→G0 改线时只改「目标还有别的入边」的那些边（策略 C）。
 *
 * 为什么必须这样：v244~v269 把 G1 的**所有**出边都改指 G0，于是 G1 独占的下级子树
 * 全部不可达。离线实测：42 对受影响，**丢 3046 个节点**，例如
 *   DBZX-6  pid=10083  49+52 节点（20 段剧情、删除标记×10、强制NPC死亡×6）
 *   M2190-3 pid=10064  43+43 节点（17 段剧情、获得/扣除背包物品×3）
 *   M10140-3 pid=10086 12+18 节点（12 段剧情、预设战斗×2）
 *   DNJH-6  pid=10008   6+12 节点（6 段剧情、改变NPC势力）
 *
 * 判据来源：G1 出边若指向**并联兄弟**（如 R10049-2-1 的 10061 G1 → 10075 的门，
 * 那个门还有父节点指着）→ 改指安全；若指向**自己独占的下级**（只有 G1 指着）→ 改指就切断。
 * 三种策略离线对比：全部改指 42 对丢 3046；只改门对成员 42 对丢 3046；只改仍有别的入边 **0 对丢 0**。
 */
static int flat_has_other_in_ptr(void* tgt_nd, void* exclude_nd) {
    if ((uintptr_t)tgt_nd < 0x10000) return 0;
    for (int i = 0; i < g_nnodes; ++i) {
        if (g_nd[i] == exclude_nd) continue;
        for (int e = 0; e < g_ec[i]; ++e)
            if (g_nd[g_edge[g_e0[i] + e].tgt] == tgt_nd) return 1;
    }
    return 0;
}

/* v271：**整链决策** —— 同一链上的门对要么全展开，要么全不展开。
 *
 * 链的定义（宽松）：pair i 的 G1 门出边指向 pair j 的父节点 / G0 / G1 → i 与 j 同链。
 *
 * 输入：g_will_expand[] 已经填了「逐对判定通过」的结果。
 * 输出：g_will_expand[] 被改成「整链通过」的结果 —— 链内任一不通过则整链清零。
 */
static void flat_chain_decide(void) {
    if (g_npairs <= 0) return;
    int par[FLAT_MAX_PAIRS];
    for (int i = 0; i < g_npairs; ++i) par[i] = i;

    /* 迭代式 union-find（无路径压缩也够用：最多 48 对） */
    for (int i = 0; i < g_npairs; ++i) {
        int g1 = g_pair[i].g1;
        if (g1 < 0 || g1 >= g_nnodes) continue;
        for (int e = 0; e < g_ec[g1]; ++e) {
            int t = g_edge[g_e0[g1] + e].tgt;
            for (int j = 0; j < g_npairs; ++j) {
                if (j == i) continue;
                if (t == g_pair[j].parent || t == g_pair[j].g0 || t == g_pair[j].g1) {
                    int a = i, b = j;
                    while (par[a] != a) a = par[a];
                    while (par[b] != b) b = par[b];
                    if (a != b) par[a] = b;
                }
            }
        }
    }

    /* 每个链组：组内全 pass 才保留，否则整组清零 */
    for (int i = 0; i < g_npairs; ++i) {
        int ri = i;
        while (par[ri] != ri) ri = par[ri];
        int all_ok = 1;
        for (int j = 0; j < g_npairs; ++j) {
            int rj = j;
            while (par[rj] != rj) rj = par[rj];
            if (rj != ri) continue;
            if (!g_will_expand[j]) { all_ok = 0; break; }
        }
        if (!all_ok) {
            int cnt = 0;
            for (int j = 0; j < g_npairs; ++j) {
                int rj = j;
                while (par[rj] != rj) rj = par[rj];
                if (rj == ri && g_will_expand[j]) cnt++;
            }
            if (g_log_graph < FLAT_LOG_GRAPH) {
                g_log_graph++;
                LOGI("[flat-chain] 链组内有对未通过 → 整组清零（本组原有 %d 对通过，现全清）", cnt);
            }
            for (int j = 0; j < g_npairs; ++j) {
                int rj = j;
                while (par[rj] != rj) rj = par[rj];
                if (rj == ri) g_will_expand[j] = 0;
            }
        }
    }
}

static int is_story_idx(int i) {
    if (i < 0 || i >= g_nnodes) return 0;
    char cb[64];
    if (strcmp(cls_name(g_nd[i], cb, sizeof(cb)), "StoryScenarioNode") != 0) return 0;
    void* sk = *(void**)((char*)g_nd[i] + OFF_SSN_SCENARIOKEY);
    return (uintptr_t)sk > 0x10000;
}

/* 这一侧是不是「单个播放剧情节点」——用来区分 A 类 / B 类 */
static int story_exit(int gateIdx) {
    if (gateIdx < 0 || gateIdx >= g_nnodes) return 0;
    if (g_ec[gateIdx] != 1) return 0;
    return is_story_idx(g_edge[g_e0[gateIdx]].tgt);
}

/* 算第 k 对的独占区域 → 写进 g_reg[k]，返回节点数。
 *   region = reach(G0) − reach(G1) − {G0, G1}
 * 标记：1 = 只在 G0 侧可达（region）；2 = G1 侧可达（要排除） */
static int flat_region(int k) {
    static int st[FLAT_MAX_NODES];
    FLAT_PAIR* p = &g_pair[k];
    unsigned char* out = g_reg[k];
    memset(out, 0, (size_t)g_nnodes);

    /* ① 先标记「不在队侧（G1）能到达的所有节点」= 2。
     *
     * v213：**扩展时不许穿过 G0**。
     *
     * 为什么必须这样：剧情图常常是环形的（例如 R10049-2-1：各段剧情写完标记后汇入同一个
     * 「流程判断」枢纽，枢纽又分给另一组门，其中一道门直接回到主链头 _4_1）。
     * 环形下 reach(G1) 会顺着 枢纽 → 42162门 → _4_1 → G0 绕回来，把 G0 自己也划进 reach(G1)；
     * 于是下面第 ② 步「起点 G0 是否已被标记」判断失败，BFS 根本不启动 → region 恒为 0 →
     * 这一对永远被判成「没有独占内容」而跳过。
     * 实测：R10049-2-1 的 4 对门全部 region=0，10061 那段（_4_2「别去招惹妙音坊的人」）
     * 因此从未被展开。
     *
     * 加上这一句之后：
     *   region(R10049-2-1 / 10061) = {播放剧情 _4_2, 添加标记}，出口 = 添加标记 → 流程判断
     * 也就是「独占区包含了剧情后面的标记写入，改线发生在标记之后」——标记不会丢。
     *
     * 全库离线模拟（813 对门对）：不变 762、救活 51（其中 36 对真有出口可改线）、
     * **变大 0、变小 0** —— 不会扰动任何已经在正常工作的门对。 */
    int head = 0;
    if (p->g1 >= 0 && p->g1 < g_nnodes) { out[p->g1] = 2; st[head++] = p->g1; }
    while (head > 0) {
        int cur = st[--head];
        for (int e = 0; e < g_ec[cur]; ++e) {
            int t = g_edge[g_e0[cur] + e].tgt;
            if (t == p->g0) continue;      /* v213：不穿过对方门（环形图的关键） */
            if (out[t]) continue;
            out[t] = 2;
            if (head < FLAT_MAX_NODES) st[head++] = t;
        }
    }

    head = 0;

    if (p->g0 >= 0 && p->g0 < g_nnodes && !out[p->g0]) { out[p->g0] = 3; st[head++] = p->g0; }
    while (head > 0) {
        int cur = st[--head];
        for (int e = 0; e < g_ec[cur]; ++e) {
            int t = g_edge[g_e0[cur] + e].tgt;
            if (out[t]) continue;
            /* v233：G0 侧的扩展**不许穿过同图其它门对的 G0**。
             *
             * 只禁自己的 g0（v213）不够：R10049-2-1 里 pid=10098 的 G0=48 能绕到
             * 汇合点 7282 → 42162 门 → _4_1 → 10061 的 G0=30 → 于是把 10061/10075/10052
             * 的独占内容（_4_2/_4_3/_4_4 各 2 个节点）全划进自己的 region，膨胀到 29 个；
             * 冲突判定「相交即双否」于是把四对全部否掉 → 展开 0（跳过 4）。
             *
             * 拦住其它 G0 后四对各是 2 个节点（剧情 + 添加标记）、出口 1 条、互不重叠。
             * 全库离线模拟（813 对）：不变 792、变小 21、变大 0、
             * **没有任何一对从「有 region」变成「0」**。 */
            int other_g0 = 0;
            for (int q = 0; q < g_npairs; ++q) {
                if (q == k) continue;
                if (g_pair[q].g0 == t) { other_g0 = 1; break; }
            }
            if (other_g0) continue;

            out[t] = 1;

            if (head < FLAT_MAX_NODES) st[head++] = t;
        }
    }

    if (p->g0 >= 0 && p->g0 < g_nnodes) out[p->g0] = 0;
    if (p->g1 >= 0 && p->g1 < g_nnodes) out[p->g1] = 0;

    int n = 0;
    for (int i = 0; i < g_nnodes; ++i) if (out[i] == 1) n++;
    return n;
}

/* ================================================================ 主流程 */

/* 「剧情分支展开」跳过名单。两种写法（末尾都可带 * 做前缀匹配）：
 *
 *   "Fame-10"          整图跳过 —— 这张图完全不展开，保持原版 if/else 走线
 *   "Fame-10:10049"    只跳过该图里 **partnerID=10049 的那一对门**，其余门对照常展开
 *
 * 为什么需要门对级：展平改的是「门」的出边接线，一张图里通常有几十对门
 * （例如 Fame-10：门对 40，展开 27，跳过 13）。整图跳过会连带放弃本来安全的那些；
 * 门对级可以只压住出问题的那一对。
 *
 * 2026-11-02（用户点名，已改为门对级）：
 *   R10049-10 / Fame-10 —— 出问题的都是 partnerID=10049（月咏樱羽）那对「在队 / 不在队」门，
 *   摊平后剧情停在叉口不动。 */
static const char* k_flatten_skip[] = {
    /* v260：**清空**。历史条目已全部由代码里的判据自动覆盖：
     *
     *   R10049-10:10049 / Fame-10:10049 —— 旧逻辑（出口改指自己的 G1）的产物；
     *     现在由「G0 侧无剧情不改指」+「G1 能绕回本对剧情不改指」两道校验处理。
     *   R10039-5:10064 —— 同上（pair0 → 校验①，pair1 → 校验②）。
     *   DBZX-6:10078 / R10025-6-1:10009 / School-2160-4:10055 / R10021-4-8:10071
     *   / M10070-4-1:10045 —— v259 为「出口目标变孤岛」加的 5 条，v260 起由**孤岛判据**
     *     在代码里自动拦截（出口目标除这条边外无其它入边 → 整对不展开）。
     *
     * 名单是打补丁，判据是机制 —— 以后新增的同类图自动拦住，不需要再往这里加条目。 */
};
/* 名单项是否命中「整图」：只比 ':' 前面的图名部分（带 * 前缀支持） */
static int flatten_skip(const char* gn) {
    if (!gn || !gn[0]) return 0;
    for (size_t i = 0; i < sizeof(k_flatten_skip) / sizeof(k_flatten_skip[0]); ++i) {
        const char* p = k_flatten_skip[i];
        const char* colon = strchr(p, ':');
        size_t n = colon ? (size_t)(colon - p) : strlen(p);
        if (n == 0) continue;
        if (p[n - 1] == '*') {
            if (n > 1 && strncmp(gn, p, n - 1) == 0) return 1;
        } else if (strlen(gn) == n && strncmp(gn, p, n) == 0) {
            return 1;
        }
    }
    return 0;
}

/* 名单项是否命中「这对门」：图名匹配 且 ':partnerID' 等于 pid */
static int flatten_skip_pair(const char* gn, int pid) {
    if (!gn || !gn[0]) return 0;
    for (size_t i = 0; i < sizeof(k_flatten_skip) / sizeof(k_flatten_skip[0]); ++i) {
        const char* p = k_flatten_skip[i];
        const char* colon = strchr(p, ':');
        if (!colon) continue;                        /* 没写 :pid = 整图项，不参与门对判定 */
        size_t n = (size_t)(colon - p);
        if (n == 0) continue;
        int gok;
        if (p[n - 1] == '*') gok = (n > 1 && strncmp(gn, p, n - 1) == 0);
        else gok = (strlen(gn) == n && strncmp(gn, p, n) == 0);
        if (!gok) continue;
        int want = 0;
        for (const char* q = colon + 1; *q >= '0' && *q <= '9'; ++q) want = want * 10 + (*q - '0');
        if (want > 0 && want == pid) return 1;
    }
    return 0;
}

void flatten_try(void* any_node) {

    if (!F_FLAT) return;
    if ((uintptr_t)any_node < 0x10000) return;

    void* g = *(void**)((char*)any_node + OFF_NODE_GRAPH);
    if ((uintptr_t)g < 0x10000) {
        /* v221：读不到图指针 = 静默放弃（原来完全没日志）。
         * R10049-2-1（李乐雅那段）有一轮连 flat 汇总、flat-zero 都没有，就是落在这里。 */
        extern int g_nograph_log;
        if (g_nograph_log < 40) {
            g_nograph_log++;
            LOGI("[flat-nograph] node=%p → 读不到 graph，放弃本次", any_node);
        }
        return;
    }
    if (flat_done(g)) return;
    char gb[96];
    const char* gn = unity_name(g, gb, sizeof(gb));
    if (flatten_skip(gn)) {
        flat_mark_done(g, "skip-list");
        if (g_log_graph < FLAT_LOG_GRAPH) {
            g_log_graph++;
            LOGI("[flat] 图 %s 在跳过名单 → 整图不展开（原版 if/else 走线）", gn);
        }
        return;
    }
    if (!flat_build(g)) {
        flat_mark_done(g, "build-fail");
        if (g_log_graph < FLAT_LOG_GRAPH) {
            g_log_graph++;
            LOGI("[flat] 图 %s 结构异常（节点 %d），跳过",
                 unity_name(g, gb, sizeof(gb)), g_nnodes);
        }
        return;
    }

    /* ---------------- ① 找互补门对 ---------------- */
    /* v220：统计「有两条出边的节点数」和「能读到条件节点的节点数」，用于 0 对门的归因 */
    int g_ec2 = 0, g_ncond = 0;
    for (int i = 0; i < g_nnodes; ++i) {
        if (g_ec[i] == 2) g_ec2++;
        if ((uintptr_t)g_cond[i] > 0x10000) g_ncond++;
    }
    g_npairs = 0;
    g_pair_ban_n = 0;
    int pair_skipped = 0;
    for (int i = 0; i < g_nnodes && g_npairs < FLAT_MAX_PAIRS; ++i) {
        if (g_ec[i] != 2) continue;                    /* 只认「一个端口两条连接」 */
        int a = g_edge[g_e0[i]].tgt;
        int b = g_edge[g_e0[i] + 1].tgt;
        if (a == b) continue;

        void* ca = g_cond[a];
        void* cb = g_cond[b];
        if ((uintptr_t)ca < 0x10000 || (uintptr_t)cb < 0x10000) continue;

        /* v251：**必须确认两个条件节点都是「队友条件」**，不能只看 partnerID + Negate。
         *
         * `partnerID` 字段不是队友条件专属，带它的节点类型很多：
         *   指定NPC死亡 / 需要伙伴 / 当与Npc交互 / 改变角色属性 / 改变NPC关系值
         *   增减buff / 需要角色属于帮派 / 需要人物属性 / **判断地图存在NPC** …
         *
         * 实测后果（用户报告）：R10049-2 里两个「判断地图存在NPC 10063」(Negate 0/1)
         * 因为 partnerID 相同、Negate 互补，被当成「需要伙伴」门对展开 →
         * 石万三「在地点」和「不在地点」两段剧情被依次播放，而原版只会播其中一段。
         *
         * 离线工具 scan_flattenable.py 早就加了 TEAM_COND_NAMES 白名单，运行时一直没加，
         * 所以离线统计是干净的、线上行为却是脏的（我据此误判过一轮）。 */
        {

            char nb1[64], nb2[64];
            const char* n1 = unity_name(ca, nb1, sizeof(nb1));
            const char* n2 = unity_name(cb, nb2, sizeof(nb2));
            /* v272：**只认「需要伙伴」**（HavePartnerCondition）。
             *
             * 实现目标是「角色是否在队」这一个语义 —— 只有它符合。
             * 之前这里是 4 个名字，后三个（需要角色加入 / 是否加入过队伍 / 需要角色加入过队伍）
             * 是 IsJoinedTeamCondition（**历史入队**），与「当前在队」不是同一维度；
             * 而且 v212 已决定它**不放宽** → 对它展平不会生效（展平依赖放宽让 G0 判真），
             * 只会白登记门表/条件表。
             *
             * 来源是离线工具里的一句「后者在 ModEditor 里没直接观察到，先留名备用」——
             * 运行时照抄了这个「备用」白名单 → 隐患：DLC 图里若出现「是否加入过队伍」，
             * 会被误当成「在队/不在队」门对按 Negate 互补去展平，语义完全错。
             *
             * 离线全库实测：伙伴/入队类条件 1546(需要伙伴) + 402(队伍有空闲位置)
             * + 196(判断队伍属性满足) + 7(判断NPC加入过队伍)，后三个备用名 **0 次**。
             * 收窄后门对总数不变（651 对 / 209 图），零影响。 */
            int is_team1 = n1 && (strcmp(n1, "需要伙伴") == 0);
            int is_team2 = n2 && (strcmp(n2, "需要伙伴") == 0);
            if (!is_team1 || !is_team2) continue;      /* 不是队友条件 → 不是门对 */
        }
        int pa = *(int*)((char*)ca + OFF_HP_PARTNER_ID);
        int pb = *(int*)((char*)cb + OFF_HP_PARTNER_ID);
        if (pa <= 0 || pa != pb) continue;             /* partnerID<=0 不是「需要伙伴」 */

        /* 门对级跳过名单：只压 partnerID 命中的那一对，同图其它门对照常展开 */
        if (flatten_skip_pair(gn, pa)) { pair_skipped++; continue; }

        int na = (*(unsigned char*)((char*)ca + OFF_HP_NEGATE) != 0);
        int nb = (*(unsigned char*)((char*)cb + OFF_HP_NEGATE) != 0);
        if (na == nb) continue;                        /* 必须互补 */

        int g0 = na ? b : a;                           /* Negate=0 = 在队侧 */
        int g1 = na ? a : b;

        /* v273（P0-2）：**门的 conditionPort 必须恰好 1 条连接**。
         *
         * 为什么：强制放行的粒度是**门**（`hook_hold_check` 里 `flatten_gate_forced(self)` →
         * 直接 `return true`，不再跑条件判定）。门上若串了多个条件，把这整个门放行
         * 就把**所有条件**一起跳过了。
         *
         * 实证：Fame-4-1 pid=10099 的 G0 门有 2 个条件 ——
         *   「需要伙伴」+「判断角色带有Buff」
         * 强制放行 → Buff 限制被绕过 → 剧情在不该播的时候播。
         * 全库 124 对门存在这种多条件情况。
         *
         * 审查意见 P0-2 原话：「宁可漏识别，也不要在条件结构不完整时接管门。」 */
        {
            int cp0 = 0, cp1 = 0;
            void* op0 = node_port(g_nd[g0], "conditionPort", 1);
            void* op1 = node_port(g_nd[g1], "conditionPort", 1);
            if ((uintptr_t)op0 > 0x10000) cp0 = port_conn_count(op0);
            if ((uintptr_t)op1 > 0x10000) cp1 = port_conn_count(op1);
            if (cp0 != 1 || cp1 != 1) {
                if (g_log_graph < FLAT_LOG_GRAPH) {
                    g_log_graph++;
                    LOGI("[flat-reject] 图 %s pid=%d 的门 conditionPort=%d/%d（要求恰好 1）→ 拒绝该对",
                         gn, pa, cp0, cp1);
                }
                continue;
            }
        }

        int s0 = story_exit(g0), s1 = story_exit(g1);
        if (!s0 && !s1) continue;                      /* X 类不动 */

        int dup = 0;
        for (int k = 0; k < g_npairs; ++k)
            if (g_pair[k].g0 == g0 && g_pair[k].g1 == g1) { dup = 1; break; }
        if (dup) continue;

        FLAT_PAIR* p = &g_pair[g_npairs++];
        p->g0 = g0; p->g1 = g1; p->pid = pa;
        p->both_story = (s0 && s1);
        p->reg_n = 0; p->ex_start = 0; p->ex_n = 0;
        p->parent = i;                                 /* v271：门对自己所在的父节点 */
    }

    if (pair_skipped && g_log_graph < FLAT_LOG_GRAPH) {
        g_log_graph++;
        LOGI("[flat] 图 %s 按门对名单跳过 %d 对（partnerID 命中，其余照常展开）",
             gn, pair_skipped);
    }
    /* v220：0 对门时**不要静默**。R10049-2-1（李乐雅那段所在图）有一轮被识别成 4 对、
     * 另一轮连汇总都没有 —— 说明它要么落进「g 读不到」的静默 return，
     * 要么这里被判定成「0 对门」直接 mark_done（之后整局都不再处理）。
     * 打出来才能区分。顺带：只打前 40 条，避免刷屏。 */
    if (g_npairs == 0) {

        /* v227：**0 对门一律重试，不再永久放弃**。
         *
         * 实测链条（v226 日志证据）：
         *   [hold-dbg] R10049-2-1 的门被判定 ×7  → hook 正常、flatten_try 被调用
         *   但第一次处理时 g_npairs==0（条件节点只读到一部分，配不出门对）
         *   而 g_ncond>0 让 v225 的 api_ready 判据误认为「就绪」→ flat_mark_done
         *   之后 6 次门判定全部 flat_done 命中 → 一次都不再处理
         *   → _4_2 播完直接汇合，接不上 10075/10052/10098 → 只播李乐雅
         *
         * 所以判据不能是「读到几个」，而应该是「配不出就再试一次」。
         * 用按图记账的重试表限次（32 次），避免热路径无限重扫几千次 il2cpp 调用。 */
        extern int g_zero_log;
        int tries = nopair_inc(g);
        if (g_zero_log < 60) {
            g_zero_log++;

            LOGI("[flat-zero] 图 %s 节点 %d：门对 0（两条出边=%d 能读条件=%d）第 %d 次 → %s",
                 gn, g_nnodes, g_ec2, g_ncond, tries,
                 (tries < NOPAIR_TRIES) ? "稍后重试" : "重试到上限，放弃");
        }
        if (tries < NOPAIR_TRIES) return;      /* 不 mark_done，等下一次门判定再试 */
        flat_mark_done(g, "no-pair-retry-limit");
        return;
    }
    nopair_clear(g);        /* 成功配出门对 → 清掉重试计数 */
    /* ---------------- ② 每对算独占 region 与对外出口 ---------------- */

    /* v250：**门表余量不足 → 整图不展开（安全降级）**。
     *
     * 为什么必须提前拦住：改线和「门放行」必须同生共死 ——
     *   表满 → flat_add_gate 登记失败 → flatten_gate_forced 查不到
     *        → times 压制让门判假，但线已经改指到别处 → 剧情卡死在那儿。
     * v239（清表不清线）和 v241（用失效指针还原线）都栽在这个一致性问题上。
     *
     * 这里在动手之前检查余量，不够就整张图走原版：最坏是「这张图没展开」，
     * 绝不出现「线改了但门不放行」的卡死。 */
    if (g_flat_gate_n + 2 * g_npairs > FLAT_MAX_GATES) {
        extern int g_full_log;
        if (g_full_log < 5) {
            g_full_log++;
            LOGI("[flat-full] 门表余量不足（已用 %d / %d，本图需 %d）→ 图 %s 走原版不展开",
                 g_flat_gate_n, FLAT_MAX_GATES, 2 * g_npairs, gn);
        }
        flat_mark_done(g, "table-full");
        return;
    }
    /* v254：清理调试代码 —— 原 v232 的「R10049-2-1 免配额」已删，恢复统一配额。 */


    for (int k = 0; k < g_npairs; ++k) {
        FLAT_PAIR* p = &g_pair[k];


        p->reg_n = flat_region(k);
        p->ex_start = g_nexit;
        p->ex_n = 0;

        if ((g_log_pair < 300) && (p->reg_n == 0 || p->ex_n == 0)) {
            g_log_pair++;
            LOGI("[flat-region] pid=%d g0=%d g1=%d reg=%d g0有效=%d g1有效=%d ec0=%d ec1=%d nd=%p/%p",
                 p->pid, p->g0, p->g1, p->reg_n,
                 (p->g0 >= 0 && p->g0 < g_nnodes), (p->g1 >= 0 && p->g1 < g_nnodes),
                 (p->g0 >= 0 && p->g0 < g_nnodes) ? g_ec[p->g0] : -1,
                 (p->g1 >= 0 && p->g1 < g_nnodes) ? g_ec[p->g1] : -1,
                 (p->g0 >= 0 && p->g0 < g_nnodes) ? g_nd[p->g0] : nullptr,
                 (p->g1 >= 0 && p->g1 < g_nnodes) ? g_nd[p->g1] : nullptr);
        }
        if (p->reg_n == 0 || p->reg_n > FLAT_MAX_REGION) continue;

        unsigned char* reg = g_reg[k];
        for (int i = 0; i < g_nnodes && g_nexit < FLAT_MAX_EDGES; ++i) {
            if (reg[i] != 1) continue;
            for (int e = 0; e < g_ec[i]; ++e) {
                int ei = g_e0[i] + e;
                if (reg[g_edge[ei].tgt] == 1) continue;      /* 内部边 */
                if (g_edge[ei].tgt == p->g1) continue;       /* 已经接过去了 */
                if (g_nexit >= FLAT_MAX_EDGES) break;
                g_exit[g_nexit++] = ei;
                p->ex_n++;
            }
        }

        /* v216：reg>0 但一条出口都没收到 —— 把 region 里每个节点和它的每条边都打出来，
         * 直接看是「边没收到（ec=0）」「指向了 region 内（reg=1）」还是「指向 G1 被跳过」。
         * R10049-2-1 的四对就是卡在这里（reg=2/29 都算出来了，ex_n 却是 0）。 */
        if (p->ex_n == 0 && g_log_pair < 300) {
            g_log_pair++;
            LOGI("[flat-noexit] pid=%d pid_idx g0=%d g1=%d reg=%d", p->pid, p->g0, p->g1, p->reg_n);
            for (int i = 0; i < g_nnodes; ++i) {
                if (reg[i] != 1) continue;
                LOGI("   region[%d] nd=%p ec=%d", i, g_nd[i], g_ec[i]);
                for (int e = 0; e < g_ec[i]; ++e) {
                    int ei = g_e0[i] + e;
                    int tg = g_edge[ei].tgt;
                    LOGI("     边 ei=%d slot=%d → idx=%d (reg=%d isG1=%d)",
                         ei, g_edge[ei].slot, tg,
                         (tg >= 0 && tg < g_nnodes) ? reg[tg] : -1,
                         (tg == p->g1) ? 1 : 0);
                }
            }
        }
    }

    /* ---------------- ③a 整链决策（只判定，不改线） ----------------
     *
     * v271：先把「每对能不能展开」算出来（clash / 孤岛都在这里判），
     * 再按链分组做整链决策，最后才进入 ③b 真正改线。
     *
     * 为什么要拆两步：链内任一不通过就必须整链不展开（否则一边串联一边并联 →
     * 引擎「无法确定走哪个分支」，官方文档第 251 段；实测 R10049-0 五段同时触发）。
     * 而逐对处理时不知道「后面的对会不会被拦」，所以必须先全算一遍。
     *
     * 离线验证：原始图一次性判定 vs 逐对改线逐对判定，209 张图**完全一致**（0 差异），
     * 所以这里用原始图算是安全的。 */
    for (int k = 0; k < g_npairs; ++k) {
        FLAT_PAIR* p = &g_pair[k];
        g_will_expand[k] = 0;
        if (p->reg_n == 0 || p->reg_n > FLAT_MAX_REGION || p->ex_n == 0) continue;
        /* clash */
        int clash2 = 0;
        for (int q = 0; q < g_npairs && !clash2; ++q) {
            if (q == k || g_pair[q].reg_n == 0) continue;
            const unsigned char* ra = g_reg[k];
            const unsigned char* rb = g_reg[q];
            for (int i = 0; i < g_nnodes; ++i)
                if (ra[i] == 1 && rb[i] == 1) { clash2 = 1; break; }
        }
        if (clash2) continue;
        /* 孤岛 */
        int orphan2 = 0;
        for (int e = 0; e < p->ex_n && !orphan2; ++e) {
            FLAT_EDGE* ed = &g_edge[g_exit[p->ex_start + e]];
            int other = 0;
            for (int i = 0; i < g_nnodes && !other; ++i) {
                if (i == ed->src) continue;
                for (int e2 = 0; e2 < g_ec[i]; ++e2)
                    if (g_edge[g_e0[i] + e2].tgt == ed->tgt) { other = 1; break; }
            }
            if (!other) orphan2 = 1;
        }
        if (orphan2) continue;
        /* v273（P0-5）：**多出口时，各目标的下游必须有共同汇合点**。
         *
         * 出口改线是把「region 的所有出口」统一改指下一对的 G0。若这些出口原本通向
         * 语义不同的路径（一个设标记、一个给奖励），强行合流会改变执行语义。
         *
         * 判据：各出口目标的下游可达集（含自身）求交，排除本对 G1 后必须非空 ——
         * 说明它们最终都会汇到同一个节点，合流不改变「之后走哪」。
         *
         * 离线：146 对多出口里 145 对有共同汇合（放行），只有 M2190-1 pid=20001 没有（拒绝）。 */
        {
            int tg0 = -1;
            int multi = 0;
            for (int e = 0; e < p->ex_n; ++e) {
                int t = g_edge[g_exit[p->ex_start + e]].tgt;
                if (tg0 < 0) tg0 = t;
                else if (t != tg0) { multi = 1; break; }
            }
            if (multi) {
                /* 对每个目标做 BFS（避开本对 G1），再求交 */
                static unsigned char hit1[FLAT_MAX_NODES];
                static unsigned char hit2[FLAT_MAX_NODES];
                static int q[FLAT_MAX_NODES];
                int first = 1;
                for (int e = 0; e < p->ex_n; ++e) {
                    int t = g_edge[g_exit[p->ex_start + e]].tgt;
                    if (t < 0 || t >= g_nnodes) continue;
                    memset(hit2, 0, (size_t)g_nnodes);
                    int head = 0;
                    hit2[t] = 1; q[head++] = t;
                    while (head > 0) {
                        int cur = q[--head];
                        for (int ee = 0; ee < g_ec[cur]; ++ee) {
                            int nx = g_edge[g_e0[cur] + ee].tgt;
                            if (nx == p->g1) continue;      /* 不穿过本对 G1 */
                            if (nx < 0 || nx >= g_nnodes || hit2[nx]) continue;
                            hit2[nx] = 1;
                            if (head < FLAT_MAX_NODES) q[head++] = nx;
                        }
                    }
                    if (first) {
                        memcpy(hit1, hit2, (size_t)g_nnodes);
                        first = 0;
                    } else {
                        for (int i = 0; i < g_nnodes; ++i) hit1[i] &= hit2[i];
                    }
                }
                int common = 0;
                for (int i = 0; i < g_nnodes; ++i)
                    if (hit1[i] && i != p->g1) { common = 1; break; }
                if (!common) {
                    if (g_log_pair < 300) {
                        g_log_pair++;
                        LOGI("[flat-reject] pid=%d 有多个出口且各目标下游无共同汇合 → 不展开", p->pid);
                    }
                    continue;
                }
            }
        }
        g_will_expand[k] = 1;
    }
    /* v274 诊断：③a 后每对的结果（定位「整图展开 0」） */
    if (g_log_graph < FLAT_LOG_GRAPH) {
        int cs = 0;
        char buf[256];
        int off = 0;
        for (int k = 0; k < g_npairs && off < 240; ++k) {
            if (g_will_expand[k]) cs++;
            if (k && off < 254) buf[off++] = ',';
            if (off < 254) buf[off++] = g_will_expand[k] ? '1' : '0';
        }
        buf[off < 255 ? off : 255] = 0;
        if (cs < g_npairs) {
            g_log_graph++;
            char nb[96];
            const char* gn2 = unity_name(g, nb, sizeof(nb));
            LOGI("[flat-3a] 图=%s 逐对结果=[%s] 通过 %d/%d",
                 (gn2 && gn2[0]) ? gn2 : "?", buf, cs, g_npairs);
        }
    }

    flat_chain_decide();        /* 整链决策：链内任一不通过 → 整链清零 */
    /* v274：**撤掉 P0-3'（改线后线性检查）** —— 用户要求：「P0-3 那项不要改动，
     * 用审查之前的版本代码就行，只要进行逻辑上判断无风险即可」。
     * 离线已验证改线后 R10049-2-1 / R10049-0 都是纯线性，逻辑上无风险；
     * 不再在运行时加这道判据（它会因为实现细节误拒整图）。 */

    /* v273（P0-2 第 5 条 / P0-7）：**改线记录容量不足 → 整图不展开**。
     *
     * 审查意见原话：「任一扫描、数组容量或改线记录容量不足时，整图保持原版，
     * 不得部分登记强制放行门。」
     *
     * g_rewire[] 用来记录改线前的原目标（供失败回滚）。容量不够时回滚会不完整 →
     * 留下半改线状态。所以先算一遍总改线数，超了就整图拒绝。 */
    {
        int need = 0;
        for (int k = 0; k < g_npairs; ++k) {
            if (!g_will_expand[k]) continue;
            need += g_pair[k].ex_n;                 /* 出口改线 */
            int g1 = g_pair[k].g1;
            if (g1 >= 0 && g1 < g_nnodes) need += g_ec[g1];   /* G1→G0 改线 */
        }
        if (g_rewire_n + need > FLAT_MAX_EDGES) {
            if (g_log_graph < FLAT_LOG_GRAPH) {
                g_log_graph++;
                LOGI("[flat-reject] 改线记录容量不足（需 %d，剩 %d）→ 整图不展开",
                     need, FLAT_MAX_EDGES - g_rewire_n);
            }
            for (int k = 0; k < g_npairs; ++k) g_will_expand[k] = 0;
        }
    }

    /* 未展开的对 → 它们的条件进「禁止放宽」表 */
    for (int k = 0; k < g_npairs; ++k) {
        if (g_will_expand[k]) continue;
        flat_add_ban(g_cond[g_pair[k].g0]);
        flat_add_ban(g_cond[g_pair[k].g1]);
    }
    /* v273（P0-6）：ban 表刚建好 → 清放宽判定的 TTL 缓存。
     * 否则之前（图还在决策中时）缓存下的「放宽」决定会继续生效，形成时序窗口。 */
    relax_rule_cache_clear();

    /* ---------------- ③b 逐对落地（只处理整链通过的） ---------------- */
    int expanded = 0, redirected = 0, skipped = 0;
    for (int k = 0; k < g_npairs; ++k) {
        FLAT_PAIR* p = &g_pair[k];
        if (!g_will_expand[k]) { skipped++; continue; }
        if (p->reg_n == 0 || p->reg_n > FLAT_MAX_REGION || p->ex_n == 0) { skipped++; continue; }

        /* 冲突：与另一对的 region 相交 → 这一对不动（另一对会在它自己那轮判）。
         * v87 与 PC 对齐：region 超上限的对**也参与**冲突判定（PC 只跳过 reg_n==0 的）。 */
        int clash = 0;
        for (int q = 0; q < g_npairs && !clash; ++q) {
            if (q == k) continue;
            FLAT_PAIR* o = &g_pair[q];
            if (o->reg_n == 0) continue;
            const unsigned char* ra = g_reg[k];
            const unsigned char* rb = g_reg[q];
            for (int i = 0; i < g_nnodes; ++i)
                if (ra[i] == 1 && rb[i] == 1) {
                    clash = 1;
                    /* v217：谁跟谁撞上了、撞在哪个节点上。
                     * R10049-2-1 的四对 reg 都是 2/2/2/29，居然全被判 clash → 全跳过，
                     * 需要看出到底是哪两个 region 重叠。 */
                    if (g_log_pair < 300) {
                        g_log_pair++;
                        LOGI("[flat-clash] k=%d pid=%d(reg=%d)  vs  q=%d pid=%d(reg=%d)  重叠节点 idx=%d nd=%p",
                             k, p->pid, p->reg_n, q, o->pid, o->reg_n, i, g_nd[i]);
                    }
                    break;
                }
        }
        if (clash) { skipped++; continue; }

        /* v270：**孤岛判据** —— 出口边的目标如果「除这条边外没有别的入边」，
         * 改线会让它（以及它下游整棵子树）永久不可达。
         *
         * 离线实测：19 对 / 26 个目标节点受影响，典型
         *   R10025-6-1（6 个）、Role5-15-2+3qianzhitiaojian（7 个）、
         *   DBZX-6 / M10070-4-1 / R10021-4-8 / R10081-2。
         *
         * 判据按**节点索引**（g_edge[].tgt 是索引）。 */
        int orphan = 0;
        for (int e = 0; e < p->ex_n && !orphan; ++e) {
            FLAT_EDGE* ed = &g_edge[g_exit[p->ex_start + e]];
            int tgt = ed->tgt;
            int src = ed->src;
            int other = 0;
            for (int i = 0; i < g_nnodes && !other; ++i) {
                if (i == src) continue;
                for (int e2 = 0; e2 < g_ec[i]; ++e2)
                    if (g_edge[g_e0[i] + e2].tgt == tgt) { other = 1; break; }
            }
            if (!other) orphan = 1;
        }
        if (orphan) {
            skipped++;
            if (g_log_pair < 300) {
                g_log_pair++;
                LOGI("[flat-orphan] pid=%d 的出口目标除这条边外无其它入边 → 不展开（避免丢下游）", p->pid);
            }
            continue;
        }


        /* v238：**链式接续** —— 剧情出口改指「下一对的在队门（G0）」，而不是自己的 G1 门。
         *
         * 背景（v237 日志实证）：门的 OutPort 有多条连接时，引擎只走**最后一条**。
         * 10061 的「不在队」门有两条出边（[0]→10075 在队门，[1]→10075 不在队门），
         * 引擎走 [1]，于是流程沿「不在队」链一路到底：
         *   _4_2 → 10061 G1 → 10075 G1 → 10052 G1 → 10098 G1 → _4_6（都不在队）
         * 四个在队门从头到尾没被判定过，_4_3/_4_4/_4_5 永远不播。
         *
         * 改指下一对的 G0 门后：放宽让它判真 → _4_3；_4_3 的标记再指向下一对 → _4_4 …
         * 最后一对没有下一级，保持原逻辑（指向自己的 G1 门）。
         *
         * 「下一对」的判定：本对的 G1 门出边若指向另一对的某个门，那一对就是下一级。 */
        int next_g0 = -1;
        if (p->g1 >= 0 && p->g1 < g_nnodes) {
            for (int e = 0; e < g_ec[p->g1] && next_g0 < 0; ++e) {
                int t = g_edge[g_e0[p->g1] + e].tgt;
                for (int q = 0; q < g_npairs; ++q) {
                    if (q == k) continue;
                    if (t == g_pair[q].g0 || t == g_pair[q].g1) { next_g0 = g_pair[q].g0; break; }
                }
            }
        }

        void* rewire_to = (next_g0 >= 0 && next_g0 < g_nnodes) ? g_nd[next_g0] : g_nd[p->g1];



        int rw_start = g_rewire_n;      /* v273（P0-7）：本次改线的起点，用于失败回滚 */
        int ok = 0, bad = 0;
        for (int e = 0; e < p->ex_n; ++e) {
            FLAT_EDGE* ed = &g_edge[g_exit[p->ex_start + e]];
            void* orig = port_conn_node(ed->port, ed->slot);   /* v241：改之前先记原目标 */
            if (port_set_conn_node(ed->port, ed->slot, rewire_to)) {
                if (g_rewire_n < FLAT_MAX_EDGES) {
                    g_rewire[g_rewire_n].port = ed->port;
                    g_rewire[g_rewire_n].slot = ed->slot;
                    g_rewire[g_rewire_n].orig = orig;
                    g_rewire_n++;
                }
                ok++;
            } else bad++;
        }
        redirected += ok;      /* v238 替换改线目标时把这行弄丢了 → 汇总永远显示「改线 0 条」 */

        /* v273（P0-7）：**事务化** — 任一出口改线失败就回滚本次全部改线，
         * 并且**不登记门表**（避免「连接只改了一半 + 门被强制放行」这种不一致）。
         *
         * 审查意见原话：「任意步骤失败都恢复原连接，不留下部分改线或部分门表。」 */
        if (bad > 0) {
            for (int i = g_rewire_n - 1; i >= rw_start; --i)
                port_set_conn_node(g_rewire[i].port, g_rewire[i].slot, g_rewire[i].orig);
            g_rewire_n = rw_start;
            redirected -= ok;
            skipped++;
            if (g_log_pair < 300) {
                g_log_pair++;
                LOGI("[flat-rollback] pid=%d 有 %d 条出口改线失败 → 回滚本次改线，不登记门表", p->pid, bad);
            }
            continue;
        }

        /* v244：**把本对的 G1 门（不在队）出边改指本对的 G0 门（在队）**。
         *
         * v243 日志实证：_4_1 播完之后被判定的四个门**全是 G1**（不在队门），
         * 四个 G0（在队门）一次都没被判定 → _4_2/_4_3/_4_4/_4_5 全都不播，只剩 _4_6。
         * 原因是引擎在分叉点只走 OutPort 的**最后一条**连接（v237 已实证），
         * 而 _4_1 不在任何 region 里、展平没改过它的出口，顺序保持 asset 原样 [G0, G1]
         * → 走最后一条 = G1。读旧档时能正常，说明那次图重建后顺序恰好相反。
         *
         * 让 G1 → G0 之后，无论引擎走哪条分叉都落到在队剧情，再由链式接续接力到下一对。
         * 原版「不在队」那条老路本来就被链式接续取代了（v238），不需要它。 */

        /* v245：**只对有下一对的门对生效**。
         * v244 对最后一对也做了 G1→G0，结果 10098 的 G1 指回自己的 G0：
         *   _4_5 → 标记 → 10098 G1 →（改指 G0）→ _4_5 → … 死循环，且 _4_6 永远播不到。
         * 实测：新档最后一幕无限循环、旧档看不到 _4_6。
         * next_g0 < 0 就是「最后一对」→ 保持原样，让它自然走到不在队剧情收尾。 */
        if (next_g0 >= 0 && p->g0 >= 0 && p->g0 < g_nnodes && p->g1 >= 0 && p->g1 < g_nnodes) {
            void* op1 = node_port(g_nd[p->g1], "OutPort", 1);
            if ((uintptr_t)op1 > 0x10000) {
                int cnt1 = port_conn_count(op1);
                if (cnt1 > 0 && cnt1 <= 32) {
                    for (int k2 = 0; k2 < cnt1; ++k2) {
                        /* v270（策略 C）：只改「目标还有别的入边」的边。
                         * 目标只有 G1 指着 = 它独占的下级 → 改指就切断整棵子树。 */
                        void* tgt = port_conn_node(op1, k2);
                        if (!flat_has_other_in_ptr(tgt, g_nd[p->g1])) {
                            if (g_log_pair < 300) {
                                g_log_pair++;
                                LOGI("[flat-g1] pid=%d 的 G1 有一条出边目标只有它指向 → 保持原样（避免切断下级）",
                                     p->pid);
                            }
                            continue;
                        }
                        void* orig2 = port_conn_node(op1, k2);
                        if (port_set_conn_node(op1, k2, g_nd[p->g0])) {
                            if (g_rewire_n < FLAT_MAX_EDGES) {
                                g_rewire[g_rewire_n].port = op1;
                                g_rewire[g_rewire_n].slot = k2;
                                g_rewire[g_rewire_n].orig = orig2;
                                g_rewire_n++;
                            }
                            redirected++;
                        }
                    }
                }
            }
        }


        /* v247：**撤销 v246 的「收尾到不在队剧情」**。
         *
         * v246 把「剧情 → 添加标记 → 汇合点」这条链上的标记出口直接改指本对 G1 门，
         * 想让 _4_5 播完收尾到 _4_6。R10049-2-1 上确实通（_4_5 → G1 → _4_6，不同剧情），
         * 但**开场图 Role1-1 因此死循环**：它同样是「G0 → 剧情 → 添加标记」三层结构，
         * 标记节点连名字都一样（「添加标记」），改指 G1 后 G1 又指回同一段剧情 → 无限循环，
         * 实测卡在游戏开场（Role1-1_3-0 重复播放 20+ 次）。
         *
         * 「改指 G1 是否安全」取决于 G1 后面接的是不是另一段剧情，**静态无法判断**，
         * 所以这个通用做法不能用。要收尾到不在队剧情，得找更可靠的判据（例如只对该图生效的
         * 白名单，或由玩家开关控制）。当前先保持 v245 的稳定行为。 */
        flat_add_cond(g_cond[p->g1]);
        /* v87：门指针也记一份 —— hook_hold_check 用它整门判真（对齐 PC） */
        flat_add_gate(g_nd[p->g0]);
        flat_add_gate(g_nd[p->g1]);

        /* 只登记姓名窗口（改名的时机由播放状态驱动，见 story_play_begin/end） */
        if (F_STORY) relax_name_open(p->pid, g);

        expanded++;
        if (g_log_pair < 60) {
            g_log_pair++;
            LOGI("[flat]   pid=%d (%s) 内容 %d 节点，改线 %d 条%s",
                 p->pid, p->both_story ? "B 类" : "A 类", p->reg_n, ok,
                 bad ? " ⚠部分失败" : "");
        }
    }

    flat_mark_done(g, "done");
    /* v228：done 这条也把结果带上 —— 之前只知道「被 mark_done」，
     * 不知道那次 expand/skip 了多少，只能靠配额早已打满的汇总行去猜。 */
    {
        extern int g_markdone_log;
        if (g_markdone_log < 100) {
            g_markdone_log++;
            char gb5[96];
            const char* gn5 = unity_name(g, gb5, sizeof(gb5));
            LOGI("[mark-done] 图=%s g=%p 指纹=(%d,%p) 原因=done 门对=%d 展开=%d 跳过=%d 改线=%d",
                 (gn5 && gn5[0]) ? gn5 : "?", g, g_nnodes, (void*)0, g_npairs, expanded, skipped, redirected);
        }
    }
    flat_mark_done(g, "done");
    /* v231：这里原来多了一层 `if (g_log_graph < FLAT_LOG_GRAPH) {` —— v224 插入 force_log
     * 时把它留在了外面，配额一满整块连判断都不执行，导致 R10049-2-1 的处理结果
     * 从 v224 起一次都没打印过（它也正好排在 394 条汇总之后）。已删除。 */
    if (g_log_graph < FLAT_LOG_GRAPH) {
        g_log_graph++;
        LOGI("[flat] 图 %s：节点 %d，门对 %d，展开 %d（跳过 %d），改线 %d 条，接管条件 %d 个（累计）",
             unity_name(g, gb, sizeof(gb)), g_nnodes, g_npairs, expanded, skipped,
             redirected, g_flat_cond_n);
    }
}

