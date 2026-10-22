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
 * 接管方式：把两道门的**条件节点**登记进 g_flat_cond，由 wulin_main.cpp 的
 * hook_check_partner / hook_check_joined 在判定入口直接走基类实现（= 丢掉这一个条件）。
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
#define FLAT_MAX_CONDS   128
#define FLAT_MAX_REGION  512         /* region 节点上限 */
#define FLAT_DONE_MAX    96          /* 处理过的图指针（环形） */
#define FLAT_LOG_GRAPH   40

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

int flatten_cond_always_true(void* cond) {
    if (!F_FLAT || (uintptr_t)cond < 0x10000) return 0;
    for (int i = 0; i < g_flat_cond_n; ++i)
        if (g_flat_cond[i] == cond) return 1;
    return 0;
}

/* ================================================================ 图处理状态 */
/* 处理过的图指针（环形）。通用规则下「任何图都要扫一次」，所以比原来大得多，
 * 否则指针一被挤出环形就会被反复重扫（每次都是几千次 il2cpp 调用）。 */
static void* g_done[FLAT_DONE_MAX];
static int g_done_n = 0;
static int g_done_rr = 0;
static int g_log_graph = 0;
static int g_log_pair = 0;

static int flat_done(void* g) {
    for (int i = 0; i < g_done_n; ++i)
        if (g_done[i] == g) return 1;
    return 0;
}

static void flat_mark_done(void* g) {
    if ((uintptr_t)g < 0x10000) return;
    g_done[g_done_rr] = g;
    g_done_rr = (g_done_rr + 1) % FLAT_DONE_MAX;
    if (g_done_n < FLAT_DONE_MAX) g_done_n++;
}

/* ================================================================ 单图工作区 */
typedef struct { int src; int tgt; int slot; void* port; } FLAT_EDGE;
typedef struct { int g0, g1; int pid; int both_story; int reg_n; int ex_start; int ex_n; } FLAT_PAIR;

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
    if (p->g1 >= 0 && p->g1 < g_nnodes) { out[p->g1] = 2; st[head++] = p->g1; }
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
    if (p->g0 >= 0 && p->g0 < g_nnodes && !out[p->g0]) { out[p->g0] = 3; st[head++] = p->g0; }
    while (head > 0) {
        int cur = st[--head];
        for (int e = 0; e < g_ec[cur]; ++e) {
            int t = g_edge[g_e0[cur] + e].tgt;
            if (out[t]) continue;
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
void flatten_try(void* any_node) {
    if (!F_FLAT) return;
    if ((uintptr_t)any_node < 0x10000) return;

    void* g = *(void**)((char*)any_node + OFF_NODE_GRAPH);
    if ((uintptr_t)g < 0x10000) return;
    if (flat_done(g)) return;

    char gb[96];
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
    for (int i = 0; i < g_nnodes && g_npairs < FLAT_MAX_PAIRS; ++i) {
        if (g_ec[i] != 2) continue;                    /* 只认「一个端口两条连接」 */
        int a = g_edge[g_e0[i]].tgt;
        int b = g_edge[g_e0[i] + 1].tgt;
        if (a == b) continue;

        void* ca = g_cond[a];
        void* cb = g_cond[b];
        if ((uintptr_t)ca < 0x10000 || (uintptr_t)cb < 0x10000) continue;

        int pa = *(int*)((char*)ca + OFF_HP_PARTNER_ID);
        int pb = *(int*)((char*)cb + OFF_HP_PARTNER_ID);
        if (pa <= 0 || pa != pb) continue;             /* partnerID<=0 不是「需要伙伴」 */

        int na = (*(unsigned char*)((char*)ca + OFF_HP_NEGATE) != 0);
        int nb = (*(unsigned char*)((char*)cb + OFF_HP_NEGATE) != 0);
        if (na == nb) continue;                        /* 必须互补 */

        int g0 = na ? b : a;                           /* Negate=0 = 在队侧 */
        int g1 = na ? a : b;

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
                if (g_edge[ei].tgt == p->g1) continue;       /* 已经接过去了 */
                if (g_nexit >= FLAT_MAX_EDGES) break;
                g_exit[g_nexit++] = ei;
                p->ex_n++;
            }
        }
    }

    /* ---------------- ③ 逐对落地（先判冲突，再动手） ---------------- */
    int expanded = 0, redirected = 0, skipped = 0;
    for (int k = 0; k < g_npairs; ++k) {
        FLAT_PAIR* p = &g_pair[k];
        if (p->reg_n == 0 || p->reg_n > FLAT_MAX_REGION || p->ex_n == 0) { skipped++; continue; }

        /* 冲突：与另一对的 region 相交 → 这一对不动（另一对会在它自己那轮判） */
        int clash = 0;
        for (int q = 0; q < g_npairs && !clash; ++q) {
            if (q == k) continue;
            FLAT_PAIR* o = &g_pair[q];
            if (o->reg_n == 0 || o->reg_n > FLAT_MAX_REGION) continue;
            const unsigned char* ra = g_reg[k];
            const unsigned char* rb = g_reg[q];
            for (int i = 0; i < g_nnodes; ++i)
                if (ra[i] == 1 && rb[i] == 1) { clash = 1; break; }
        }
        if (clash) { skipped++; continue; }

        int ok = 0, bad = 0;
        for (int e = 0; e < p->ex_n; ++e) {
            FLAT_EDGE* ed = &g_edge[g_exit[p->ex_start + e]];
            if (port_set_conn_node(ed->port, ed->slot, g_nd[p->g1])) ok++;
            else bad++;
        }
        redirected += ok;

        flat_add_cond(g_cond[p->g0]);
        flat_add_cond(g_cond[p->g1]);

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

    flat_mark_done(g);

    if (g_log_graph < FLAT_LOG_GRAPH) {
        g_log_graph++;
        LOGI("[flat] 图 %s：节点 %d，门对 %d，展开 %d（跳过 %d），改线 %d 条，接管条件 %d 个（累计）",
             unity_name(g, gb, sizeof(gb)), g_nnodes, g_npairs, expanded, skipped,
             redirected, g_flat_cond_n);
    }
}
