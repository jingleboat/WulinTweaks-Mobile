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
#define FLAT_MAX_CONDS   4096    /* v158：128 → 4096（实测打满后所有门 forced=N，展平失效） */
#define FLAT_MAX_GATES   8192        /* v158：512 → 8192（同上） */
#define FLAT_MAX_REGION  512         /* region 节点上限 */
#define FLAT_DONE_MAX    96          /* 处理过的图指针（环形） */
#define FLAT_LOG_GRAPH   200   /* v149：40 → 200（本次诊断发现 40 条上限把 R10049-2 的结果吞掉了） */

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

static int g_cond_full_warned = 0, g_gate_full_warned = 0;
static void flat_add_cond(void* cond) {
    if ((uintptr_t)cond < 0x10000) return;
    for (int i = 0; i < g_flat_cond_n; ++i)
        if (g_flat_cond[i] == cond) return;
    if (g_flat_cond_n < FLAT_MAX_CONDS) g_flat_cond[g_flat_cond_n++] = cond;
    else if (!g_cond_full_warned) { g_cond_full_warned = 1; LOGE("[flat] 条件表已满（%d），后续条件不再登记 —— 展平会失效", FLAT_MAX_CONDS); }
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
        if (g_flat_gate[i] == gate) return;
    if (g_flat_gate_n < FLAT_MAX_GATES) g_flat_gate[g_flat_gate_n++] = gate;
    else if (!g_gate_full_warned) { g_gate_full_warned = 1; LOGE("[flat] 门表已满（%d），后续门不再登记 —— 展平会失效", FLAT_MAX_GATES); }
}

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
/* v153：处理过的图改为**不可淘汰**的开放寻址表。
 * 原来 96 条环形表会被挤掉 → 同一张图被二次展开（实测「门对 15，展开 13」紧接着
 * 「门对 15，展开 0（跳过 15）」）。表满时 fail-open：只报警不再登记 —— 重复处理
 * 会因为算不出可展开的 region/exits 而全部跳过，无害。 */
#define FLAT_DONE_TBL 2048
static void* g_done_tbl[FLAT_DONE_TBL];        /* 16 KB */
static int   g_done_used = 0, g_done_warned = 0;
static int   g_log_graph = 0;
static int   g_log_pair = 0;

static unsigned done_hash(void* p) {
    uintptr_t x = (uintptr_t)p; x ^= x >> 13; x *= 0x9E3779B1u; x ^= x >> 15;
    return (unsigned)x & (FLAT_DONE_TBL - 1);
}
static int flat_done(void* g) {
    if ((uintptr_t)g < 0x10000) return 0;
    unsigned h = done_hash(g);
    for (int i = 0; i < 8; ++i) {
        unsigned k = (h + i) & (FLAT_DONE_TBL - 1);
        if (!g_done_tbl[k]) return 0;
        if (g_done_tbl[k] == g) return 1;
    }
    return 0;
}
static void flat_mark_done(void* g) {
    if ((uintptr_t)g < 0x10000) return;
    if (g_done_used >= FLAT_DONE_TBL * 3 / 4) {
        if (!g_done_warned) {
            g_done_warned = 1;
            LOGI("[flat] 已处理图表已用 %d 条，后续不再登记（重复处理会全部跳过）", g_done_used);
        }
        return;
    }
    unsigned h = done_hash(g);
    for (int i = 0; i < 8; ++i) {
        unsigned k = (h + i) & (FLAT_DONE_TBL - 1);
        if (!g_done_tbl[k]) { g_done_tbl[k] = g; g_done_used++; return; }
        if (g_done_tbl[k] == g) return;
    }
}

/* ================================================================ 单图工作区 */
typedef struct { int src; int tgt; int slot; void* port; } FLAT_EDGE;
typedef struct { int g0, g1; int pid; int both_story; int reg_n; int ex_start; int ex_n;
                 int first_is_g0;   /* v156：父端口第一条连接是不是「在队」侧（决定引擎先问谁） */
                 int f, s;          /* v156：按连接顺序的 first / second */
} FLAT_PAIR;

/* v153：改线事务日志。undo 上限 1024（实测单图最多 51 条改线，余量充足）；
 * 超过上限 → 整图放弃展开，绝不留半改状态。 */
#define FLAT_MAX_UNDO 1024
typedef struct { int ei; int old_tgt; int g1; } FLAT_UNDO;
static FLAT_UNDO      g_undo[FLAT_MAX_UNDO];     /* 12 KB */
static int            g_undo_n = 0;
static unsigned char  g_reach_before[FLAT_MAX_NODES];   /* 兼容保留（不再用于判定） */
static unsigned char  g_tmp_reach[FLAT_MAX_NODES];      /* 临时位图 */
/* v154：可达性守恒必须**按对**比较 —— v153 错在把“所有对 g1 可达集的并集”当作每对的门槛，
 * 于是一对 g1 只要到不了别的对的内容就误判失败（实测 20+ 张图被误回滚，R10049-2 全灭）。
 * 这里给每对存一份改线前的 Reach(g1) 位集：48 对 × 257 字节 ≈ 12 KB。 */
#define FLAT_BITS ((FLAT_MAX_NODES + 7) / 8)
static unsigned char  g_before_bits[FLAT_MAX_PAIRS][FLAT_BITS];
static int            g_log_rollback = 0;

static void*          g_nd[FLAT_MAX_NODES];      /* 节点指针 */
static int            g_e0[FLAT_MAX_NODES];      /* 该节点第一条出边下标 */
static int            g_ec[FLAT_MAX_NODES];      /* 该节点出边条数 */
static void*          g_cond[FLAT_MAX_NODES];    /* 门的条件节点（非门为 0） */
static FLAT_EDGE      g_edge[FLAT_MAX_EDGES];
static FLAT_PAIR      g_pair[FLAT_MAX_PAIRS];
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

/* ---- v153：两个迭代式图算法（绝不用递归，避免 2000+ 节点爆栈） ---- */

/* 从下标 start 出发能到达的所有节点 → 写进 out（长度 g_nnodes） */
static void flat_mark_reach(int start, unsigned char* out) {
    static int stk[FLAT_MAX_NODES];
    memset(out, 0, (size_t)g_nnodes);
    if (start < 0 || start >= g_nnodes) return;
    int sp = 0;
    out[start] = 1;
    stk[sp++] = start;
    while (sp > 0) {
        int c = stk[--sp];
        for (int e = 0; e < g_ec[c]; ++e) {
            int t = g_edge[g_e0[c] + e].tgt;
            if (t < 0 || t >= g_nnodes || out[t]) continue;
            out[t] = 1;
            if (sp < FLAT_MAX_NODES) stk[sp++] = t;
        }
    }
}

/* src 能否到达 want（按当前 g_edge 镜像，含已改的边） */
static int flat_reach(int src, int want, unsigned char* seen) {
    static int stk[FLAT_MAX_NODES];
    if (src < 0 || src >= g_nnodes || want < 0 || want >= g_nnodes) return 0;
    memset(seen, 0, (size_t)g_nnodes);
    int sp = 0;
    seen[src] = 1;
    stk[sp++] = src;
    while (sp > 0) {
        int c = stk[--sp];
        if (c == want) return 1;
        for (int e = 0; e < g_ec[c]; ++e) {
            int t = g_edge[g_e0[c] + e].tgt;
            if (t < 0 || t >= g_nnodes || seen[t]) continue;
            seen[t] = 1;
            if (sp < FLAT_MAX_NODES) stk[sp++] = t;
        }
    }
    return 0;
}

/* 扫图：收节点 + 出边 + 门→条件节点 */
static int flat_build(void* g) {
    void* nodes = *(void**)((char*)g + OFF_GRAPH_NODES);
    int n = lcnt(nodes);
    if (n <= 2 || n > FLAT_MAX_NODES) return 0;

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
    if (g_nnodes < 2) return 0;

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

static int is_story_idx(int i) {
    if (i < 0 || i >= g_nnodes) return 0;
    char cb[64];
    if (strcmp(cls_name(g_nd[i], cb, sizeof(cb)), "StoryScenarioNode") != 0) return 0;
    void* sk = *(void**)((char*)g_nd[i] + OFF_SSN_SCENARIOKEY);
    return (uintptr_t)sk > 0x10000;
}

/* v157：允许参与展平的条件类（"和角色在队/在场相关"的那几类）。
 * 需要NPC情缘**未加入**：R10049-2 里它的下游是 R10049-2_6_6（定海棠出演），
 * 绛紫烟本人出演的 _6_7 由「需要伙伴」控制，按用户规则不加。 */
static int is_story_cond(const char* cn) {
    if (!cn || !cn[0]) return 0;
    /* v158（用户指定）：放宽与展平**都只对「需要伙伴」生效**。 */
    return strcmp(cn, "HavePartnerCondition") == 0;
}

/* v157：门 → (pid, 侧别)，供 [gate] 轨迹日志打印 */
#define FLAT_GATE_INFO 256
typedef struct { void* gate; int pid; int in_team; } FLAT_GATE_INFO_T;
static FLAT_GATE_INFO_T g_ginfo[FLAT_GATE_INFO];
static int g_ginfo_n = 0;
static void gate_info_add(void* gate, int pid, int in_team) {
    if ((uintptr_t)gate < 0x10000 || g_ginfo_n >= FLAT_GATE_INFO) return;
    for (int i = 0; i < g_ginfo_n; ++i) if (g_ginfo[i].gate == gate) return;
    g_ginfo[g_ginfo_n].gate = gate;
    g_ginfo[g_ginfo_n].pid = pid;
    g_ginfo[g_ginfo_n].in_team = in_team;
    g_ginfo_n++;
}
int flatten_gate_pid(void* gate, int* in_team) {
    for (int i = 0; i < g_ginfo_n; ++i)
        if (g_ginfo[i].gate == gate) { if (in_team) *in_team = g_ginfo[i].in_team; return g_ginfo[i].pid; }
    return 0;
}

/* v155：这一侧是否**含有**播放剧情节点（不再要求「单节点直连」）。
 * 用于区分 A/B 类，并决定 X 类是否放开：以前「两侧出口都不是单节点剧情」一律放弃，
 * 于是「门→设置标记→播放剧情」这种形状（X 类）永远拿不到在队内容。 */
static unsigned char g_story_reach[FLAT_MAX_NODES];
static int side_has_story(int gateIdx) {
    if (gateIdx < 0 || gateIdx >= g_nnodes) return 0;
    flat_mark_reach(gateIdx, g_story_reach);
    for (int i = 0; i < g_nnodes; ++i)
        if (g_story_reach[i] && is_story_idx(i)) return 1;
    return 0;
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

    int head = 0;
    if (p->s >= 0 && p->s < g_nnodes) { out[p->s] = 2; st[head++] = p->s; }
    while (head > 0) {
        int cur = st[--head];
        for (int e = 0; e < g_ec[cur]; ++e) {
            int t = g_edge[g_e0[cur] + e].tgt;
            if (out[t]) continue;
            out[t] = 2;
            if (head < FLAT_MAX_NODES) st[head++] = t;
        }
    }

    head = 0;
    if (p->f >= 0 && p->f < g_nnodes && !out[p->f]) { out[p->f] = 3; st[head++] = p->f; }
    while (head > 0) {
        int cur = st[--head];
        for (int e = 0; e < g_ec[cur]; ++e) {
            int t = g_edge[g_e0[cur] + e].tgt;
            if (out[t]) continue;
            out[t] = 1;
            if (head < FLAT_MAX_NODES) st[head++] = t;
        }
    }

    if (p->f >= 0 && p->f < g_nnodes) out[p->f] = 0;
    if (p->s >= 0 && p->s < g_nnodes) out[p->s] = 0;

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
    "R10049-10:10049",
    "Fame-10:10049",
};

/* 名单项是否命中「整图」：只比 ':' 前面的图名部分（带 * 前缀支持） */
static int flatten_skip(const char* gn) {
    if (!gn || !gn[0]) return 0;
    for (size_t i = 0; i < sizeof(k_flatten_skip) / sizeof(k_flatten_skip[0]); ++i) {
        const char* p = k_flatten_skip[i];
        const char* colon = strchr(p, ':');
        /* v155：带 ':' 的是**门对级**名单，只由 flatten_skip_pair 处理。
         * 以前这里把它截成 ':' 前的图名当整图匹配 → `Fame-10:10049` 会把整张
         * 1294 节点的 Fame-10 全跳过（PC 只跳一对），两端行为不一致。 */
        if (colon) continue;
        size_t n = strlen(p);
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

static void flatten_try_inner(void* any_node) {
    if (!F_FLAT) return;
    if ((uintptr_t)any_node < 0x10000) return;

    void* g = *(void**)((char*)any_node + OFF_NODE_GRAPH);
    if ((uintptr_t)g < 0x10000) return;
    if (flat_done(g)) return;

    char gb[96];
    const char* gn = unity_name(g, gb, sizeof(gb));
    if (flatten_skip(gn)) {
        flat_mark_done(g);
        if (g_log_graph < FLAT_LOG_GRAPH) {
            g_log_graph++;
            LOGI("[flat] 图 %s 在跳过名单 → 整图不展开（原版 if/else 走线）", gn);
        }
        return;
    }
    if (!flat_build(g)) {
        flat_mark_done(g);
        if (g_log_graph < FLAT_LOG_GRAPH) {
            g_log_graph++;
            LOGI("[flat] 图 %s 结构异常（节点 %d），跳过",
                 unity_name(g, gb, sizeof(gb)), g_nnodes);
        }
        return;
    }

    /* ---------------- ① 找互补门对 ---------------- */
    g_npairs = 0;
    int pair_skipped = 0;
    for (int i = 0; i < g_nnodes && g_npairs < FLAT_MAX_PAIRS; ++i) {
        if (g_ec[i] != 2) continue;                    /* 只认「一个端口两条连接」 */
        int a = g_edge[g_e0[i]].tgt;
        int b = g_edge[g_e0[i] + 1].tgt;
        if (a == b) continue;

        void* ca = g_cond[a];
        void* cb = g_cond[b];
        if ((uintptr_t)ca < 0x10000 || (uintptr_t)cb < 0x10000) continue;

        /* v155：**白名单**——只认「当前在队」这一类条件（HavePartnerCondition）。
         * 以前只看 partnerID+Negate，于是「石万三存活 / 石万三死亡」(NpcDeadCondition)、
         * 关系值、位置等同样带 partnerID 的条件也被当成在队门对，展平后两侧都强制放行
         * → 存活与死亡剧情同时播，语义矛盾（实测 R10049-2 的 10063）。
         * SlgHavePartnerCondition 属 DLC 小游戏，不参与。 */
        char cn1[64], cn2[64];
        if (!is_story_cond(cls_name(ca, cn1, sizeof(cn1)))) continue;
        if (!is_story_cond(cls_name(cb, cn2, sizeof(cn2)))) continue;

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

        int s0 = side_has_story(g0), s1 = side_has_story(g1);
        if (!s0 && !s1) continue;      /* v155：两侧 region 都没有剧情 → 仍不动 */

        int dup = 0;
        for (int k = 0; k < g_npairs; ++k)
            if (g_pair[k].g0 == g0 && g_pair[k].g1 == g1) { dup = 1; break; }
        if (dup) continue;

        FLAT_PAIR* p = &g_pair[g_npairs++];
        p->g0 = g0; p->g1 = g1; p->pid = pa;
        /* v159：v156 只加了字段和日志，**赋值丢了** → region 起点恒为 0，全部判空（展开 0）。
         * a、b 就是父端口的连接顺序：a 先被引擎问。 */
        p->f = a; p->s = b;
        p->first_is_g0 = (a == g0) ? 1 : 0;
        p->both_story = (s0 && s1);
        p->reg_n = 0; p->ex_start = 0; p->ex_n = 0;
    }

    if (pair_skipped && g_log_graph < FLAT_LOG_GRAPH) {
        g_log_graph++;
        LOGI("[flat] 图 %s 按门对名单跳过 %d 对（partnerID 命中，其余照常展开）",
             gn, pair_skipped);
    }
    if (g_npairs == 0) { flat_mark_done(g); return; }

    /* ---------------- ② 每对算独占 region 与对外出口 ---------------- */
    for (int k = 0; k < g_npairs; ++k) {
        FLAT_PAIR* p = &g_pair[k];
        p->reg_n = flat_region(k);
        p->ex_start = g_nexit;
        p->ex_n = 0;
        if (p->reg_n == 0 || p->reg_n > FLAT_MAX_REGION) continue;

        unsigned char* reg = g_reg[k];
        for (int i = 0; i < g_nnodes && g_nexit < FLAT_MAX_EDGES; ++i) {
            if (reg[i] != 1) continue;
            for (int e = 0; e < g_ec[i]; ++e) {
                int ei = g_e0[i] + e;
                if (reg[g_edge[ei].tgt] == 1) continue;      /* 内部边 */
                if (g_edge[ei].tgt == p->s) continue;        /* 已经接过去了 */
                if (g_nexit >= FLAT_MAX_EDGES) break;
                g_exit[g_nexit++] = ei;
                p->ex_n++;
            }
        }
    }

    /* ---------------- ③ 逐对落地（事务化：改线 → 校验 → 提交或全部回滚） ----------------
     * v153：把“边算边改”改成“先全改、再校验、不合格整体回滚”。
     * 校验两条（缺一不可，都会造成「剧情卡死」）：
     *   ① 无新增环：新环必然经过某条被改的边 (u→g1)，所以只要检查 g1 能否回到 u；
     *   ② 可达性守恒：改线后 Reach(g1) 必须仍然包含改线前的 Reach(g1)（汇合点不能丢）。
     * 任一不过 → 按 journal 逆序还原所有边 + 回退条件/门登记水位。 */
    int expanded = 0, redirected = 0, skipped = 0, overlaps = 0, rolled = 0;
    g_undo_n = 0;
    int wm_cond = g_flat_cond_n, wm_gate = g_flat_gate_n;
    int commit_fail = 0;

    /* 记录改线前 Reach(g1) 的并集（用于第 ② 条），按对分别记太占内存，这里只记并集 */
    memset(g_reach_before, 0, (size_t)g_nnodes);

    /* 预扫：按对记录“改线前 Reach(g1)”，必须在任何改线之前算 */
    memset(g_before_bits, 0, sizeof(g_before_bits));
    for (int k = 0; k < g_npairs; ++k) {
        FLAT_PAIR* p = &g_pair[k];
        if (p->reg_n == 0 || p->reg_n > FLAT_MAX_REGION || p->ex_n == 0) continue;
        flat_mark_reach(p->s, g_tmp_reach);
        for (int i = 0; i < g_nnodes; ++i)
            if (g_tmp_reach[i]) g_before_bits[k][i >> 3] |= (unsigned char)(1u << (i & 7));
    }

    for (int k = 0; k < g_npairs; ++k) {
        FLAT_PAIR* p = &g_pair[k];
        if (p->reg_n == 0 || p->reg_n > FLAT_MAX_REGION || p->ex_n == 0) { skipped++; continue; }

        /* v151（用户指定）：不再因为“与另一对门的 region 相交”而跳过，只计数 */
        int clash = 0;
        for (int q = 0; q < g_npairs && !clash; ++q) {
            if (q == k) continue;
            FLAT_PAIR* o = &g_pair[q];
            if (o->reg_n == 0) continue;
            const unsigned char* ra = g_reg[k];
            const unsigned char* rb = g_reg[q];
            for (int i = 0; i < g_nnodes; ++i)
                if (ra[i] == 1 && rb[i] == 1) { clash = 1; break; }
        }
        if (clash) overlaps++;

        int ok = 0, bad = 0;
        for (int e = 0; e < p->ex_n; ++e) {
            int ei = g_exit[p->ex_start + e];
            FLAT_EDGE* ed = &g_edge[ei];
            if (g_undo_n >= FLAT_MAX_UNDO) { commit_fail = 1; break; }
            if (!port_set_conn_node(ed->port, ed->slot, g_nd[p->s])) { commit_fail = 1; break; }
            g_undo[g_undo_n].ei = ei;
            g_undo[g_undo_n].old_tgt = ed->tgt;
            g_undo[g_undo_n].g1 = p->s;
            g_undo_n++;
            ed->tgt = p->s;          /* 镜像到内存表，后面的 DFS 才看得到新边 */
            ok++;
        }
        redirected += ok;
        if (commit_fail) { rolled = 1; break; }

        flat_add_cond(g_cond[p->f]);
        flat_add_cond(g_cond[p->s]);
        flat_add_gate(g_nd[p->f]);
        flat_add_gate(g_nd[p->s]);
        gate_info_add(g_nd[p->g0], p->pid, 1);    /* 侧别仅用于日志：Negate=0 = 在队 */
        gate_info_add(g_nd[p->g1], p->pid, 0);

        expanded++;
        if (g_log_pair < 300) {
            g_log_pair++;
            LOGI("[flat]   pid=%d (%s) 先问=%s 内容 %d 节点，改线 %d 条%s",
                 p->pid, p->both_story ? "B 类" : "A 类",
                 p->first_is_g0 ? "在队" : "不在队", p->reg_n, ok, bad ? " ⚠部分失败" : "");
        }
    }

    /* ---- 校验 ---- */
    if (!rolled) {
        for (int i = 0; i < g_undo_n; ++i) {
            int s = g_edge[g_undo[i].ei].src;
            if (flat_reach(g_undo[i].g1, s, g_tmp_reach)) { rolled = 1; break; }
        }
    }
    if (!rolled) {
        /* 按对比较：第 k 对改线前的 Reach(g1) 必须仍是改线后的子集 */
        for (int k = 0; k < g_npairs && !rolled; ++k) {
            FLAT_PAIR* p = &g_pair[k];
            if (p->reg_n == 0 || p->reg_n > FLAT_MAX_REGION || p->ex_n == 0) continue;
            flat_mark_reach(p->s, g_tmp_reach);
            for (int i = 0; i < g_nnodes; ++i) {
                if (!(g_before_bits[k][i >> 3] & (1u << (i & 7)))) continue;
                if (!g_tmp_reach[i]) { rolled = 1; break; }
            }
        }
    }

    if (rolled) {
        /* 全部回滚：边 → 旧目标；条件/门登记 → 水位 */
        for (int i = g_undo_n - 1; i >= 0; --i) {
            FLAT_EDGE* ed = &g_edge[g_undo[i].ei];
            port_set_conn_node(ed->port, ed->slot, g_nd[g_undo[i].old_tgt]);
            ed->tgt = g_undo[i].old_tgt;
        }
        g_flat_cond_n = wm_cond;
        g_flat_gate_n = wm_gate;
        if (g_log_rollback++ < 200)
            LOGI("[flat] 图 %s 回滚：改线 %d 条后校验不通过（环或可达性），已全部还原",
                 unity_name(g, gb, sizeof(gb)), g_undo_n);
        expanded = 0; redirected = 0;
    } else if (F_STORY) {
        for (int k = 0; k < g_npairs; ++k) {
            FLAT_PAIR* p = &g_pair[k];
            if (p->reg_n == 0 || p->reg_n > FLAT_MAX_REGION || p->ex_n == 0) continue;
            relax_name_open(p->pid, g);
        }
    }
    g_undo_n = 0;

    flat_mark_done(g);

    if (g_log_graph < FLAT_LOG_GRAPH) {
        g_log_graph++;
        LOGI("[flat] 图 %s：节点 %d，门对 %d，展开 %d（跳过 %d，含区域相交 %d），改线 %d 条，接管条件 %d 个（累计）",
             unity_name(g, gb, sizeof(gb)), g_nnodes, g_npairs, expanded, skipped, overlaps,
             redirected, g_flat_cond_n);
    }
}

/* v153：重入保护。条件判定会跑在加载线程（tombstone 里是 Loading.Preload），
 * 而整个展平用的是一组静态暂存 —— 并发进入会互相踩。抢不到就直接放弃本次。 */
static volatile int g_flat_busy = 0;

void flatten_try(void* any_node) {
    if (!F_FLAT) return;
    if (__atomic_exchange_n(&g_flat_busy, 1, __ATOMIC_ACQ_REL)) return;
    flatten_try_inner(any_node);
    __atomic_store_n(&g_flat_busy, 0, __ATOMIC_RELEASE);
}
