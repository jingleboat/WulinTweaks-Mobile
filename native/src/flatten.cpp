/*
 * WulinTweaks · 剧情分支展开（Branch Flattener）  v67
 * ==================================================
 * 目的：原版剧情图里「某角色是否在队」的互补兄弟条件（Negate=0 / Negate=1）是 if/else，
 * 引擎按顺序问、第一个满足的就被走，另一条永远不播。这里把「讲解类」分支的出口
 * 从汇合点改接到下一段讲解，让所有在队讲解按顺序播完，再走「无伙伴」那条兜底。
 *
 * 为什么改内存对象而不是 bundle 字节：
 *   实测 AssetBundle.LoadFromFileAsync 在这个游戏里从不被调用（剧情 bundle 走别处）。
 *
 * 为什么不用「类名 + 条件」去认门（v65 的错法）：
 *   v65 用 ports 字典的 keys/values 平行数组按名找端口 → 播放剧情全找到、门一个没找到。
 *   XNode 的 NodePortDictionary 有 ClearSerialize()，运行期两个 List 很可能已被清空。
 *   v67 改成：
 *     ① 端口用 Node.GetOutputPort / Node.GetPort（走 Dictionary 的托管 API）取；
 *     ② 门**纯靠结构走**：从 _2_1 起沿 OutPort.connections[1] 连走 5 次，
 *        依次得到 门9 → 门11 → 门13 → 门15 → 门17（每级 Neg=1 的那个）；
 *        完全不依赖类名、不依赖条件字段扫描。
 *
 * 结构偏移（V1.2.0820B131，dump.cs 标定）：
 *   XNode.Node        graph 0x18  position 0x20
 *   XNode.NodePort    _fieldName 0x10  _node 0x18  connections 0x20  _direction 0x28
 *   NodePort.PortConnection  fieldName 0x10  node 0x18  port 0x20
 *   NodeGraph         nodes 0x18（List<Node>）
 *   StoryScenarioNode ScenarioKey 0x60
 *   HavePartnerCondition  partnerID 0x58（int）  Negate 0x5C（bool）
 */
#include "common.h"

#include <string.h>

extern volatile int F_FLAT;

/* ---------------------------------------------------------------- 偏移 */
#define OFF_NODEPORT_CONNS   0x20
#define OFF_PORTCONN_NODE    0x18
#define OFF_GRAPH_NODES      0x18
#define OFF_SSN_SCENARIOKEY  0x60
#define OFF_HP_PARTNER_ID    0x58
#define OFF_HP_NEGATE        0x5C

/* ---------------------------------------------------------------- 列表访问 */
/* ⚠️ il2cpp 数组（T[]）的元素不是从数组对象头开始，而是 +0x20：
 *      klass 0x00 | monitor 0x08 | bounds 0x10 | max_length 0x18 | 元素从 0x20
 *   而 List<T> 的 _items 字段（0x10）指向的就是这个数组对象。
 *   踩过的坑（v65~v68）：
 *     · 按 (T*)arr[i] 从 0x00 读 → [0] 拿到数组的 klass 指针（当节点用会闪退）、
 *       [1] 拿到 monitor=0（当 PortConnection 用 → 取到的 node 恒为 null）。
 *     · 表现为：ScenarioKey 还能命中（偏 4 位后仍覆盖到大部分元素），
 *       但「取 connections[1]」永远空、对「伪节点」调 GetPort 直接 SIGSEGV。 */
#define OFF_ARRAY_DATA 0x20

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
 * 端口名是 ASCII，用 il2cpp_string_new 造一个托管字符串传进去。 */
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
    if ((uintptr_t)port < 0x10000) return -1;
    return lcnt(*(void**)((char*)port + OFF_NODEPORT_CONNS));
}

static void* port_conn_node(void* port, int idx) {
    if ((uintptr_t)port < 0x10000) return nullptr;
    void* cs = *(void**)((char*)port + OFF_NODEPORT_CONNS);
    if (idx < 0 || idx >= lcnt(cs)) return nullptr;
    void* c = litem(cs, idx);
    if ((uintptr_t)c < 0x10000) return nullptr;
    void* t = *(void**)((char*)c + OFF_PORTCONN_NODE);
    return ((uintptr_t)t > 0x10000) ? t : nullptr;
}

static int port_set_conn_node(void* port, int idx, void* target) {
    if ((uintptr_t)port < 0x10000) return 0;
    void* cs = *(void**)((char*)port + OFF_NODEPORT_CONNS);
    if (idx < 0 || idx >= lcnt(cs)) return 0;
    void* c = litem(cs, idx);
    if ((uintptr_t)c < 0x10000) return 0;
    *(void**)((char*)c + OFF_PORTCONN_NODE) = target;
    return 1;
}

/* ================================================================ 补丁 */
#define FLAT_GRAPH_NAME   "R10049-2"
#define FLAT_START_KEY    "R10049-2_2_1"

/* 讲解链上的 5 个角色 id。每个各有一对 Negate=0/1 的门（共 10 个）。
 *
 * ⚠️ v69 的做法是「partnerID 改成 0 + Negate=1」把它做成恒真 —— v70 撤掉。
 * 原因：它把「这是谁」一起抹掉了，mark_relaxed() 里 `pid <= 0 → return`，
 * 于是姓名窗口永远不开，「陌生名 → 熟悉后姓名」直接失效（用户实测报的问题）。
 *
 * v70 改成**一个字段都不写**：只记下这些条件节点的指针，
 * 在 hook_check_partner 里把这个条件强制判真（flatten_cond_always_true）。
 *   · partnerID / Negate 原样保留 → mark_relaxed 照常工作 → 姓名功能恢复
 *   · 仍然与「剧情条件放宽」解耦（放宽关着也恒真，不会卡死在半路）
 * 只碰这 5 个 id —— 图里还有 10054/10075/10071/10015/10039/10056/10098 的
 * 「两侧都有剧情」分支，那是 B 类，本期不动。 */
static const int k_neutral_pids[] = { 10084, 10083, 10078, 10053, 10008 };
#define FLAT_NEUTRAL_PIDS_N ((int)(sizeof(k_neutral_pids) / sizeof(k_neutral_pids[0])))

/* 被接管的条件节点表（恒真）*/
#define FLAT_COND_MAX 24
static void* g_flat_cond[FLAT_COND_MAX];
static int g_flat_cond_n = 0;

static void flat_add_cond(void* cond) {
    if ((uintptr_t)cond < 0x10000) return;
    for (int i = 0; i < g_flat_cond_n; ++i)
        if (g_flat_cond[i] == cond) return;
    if (g_flat_cond_n < FLAT_COND_MAX) g_flat_cond[g_flat_cond_n++] = cond;
}

/* 这个条件节点是否已被「分支展开」接管（= 恒真）。wulin_main.cpp 的
 * hook_check_partner / hook_check_joined 会在判定前先问一句。 */
int flatten_cond_always_true(void* cond) {
    if (!F_FLAT || (uintptr_t)cond < 0x10000) return 0;
    for (int i = 0; i < g_flat_cond_n; ++i)
        if (g_flat_cond[i] == cond) return 1;
    return 0;
}

/* 依次串起来的 5 段讲解（第 5 段之后接「无伙伴」兜底门） */
static const char* k_chain_keys[] = {
    "R10049-2_2_2",   /* 洛千雪 10084 */
    "R10049-2_2_3",   /* 洛天雄 10083 */
    "R10049-2_2_4",   /* 裘之墨 10078 */
    "R10049-2_2_5",   /* 说书人 10053 */
    "R10049-2_2_6",   /* 凌梦蝶 10008 */
};
#define FLAT_CHAIN_N ((int)(sizeof(k_chain_keys) / sizeof(k_chain_keys[0])))

/* 从 _2_1 起沿 OutPort.connections[1] 要走几次才到最后的 Neg=1 门（门17）：
 *   _2_1 --[1]--> 门9 --[1]--> 门11 --[1]--> 门13 --[1]--> 门15 --[1]--> 门17
 * 前四级各对应 10084/10083/10078/10053 的 Neg=1 门，第五级是 10008 的 Neg=1 门。 */
#define FLAT_WALK_STEPS 5

/* ================================================================ 主逻辑 */
#define FLAT_DONE_MAX 16
static void* g_flat_done[FLAT_DONE_MAX];
static int g_flat_done_n = 0;
static int g_flat_log = 0;
/* 最近被判定为「不是目标图」的 graph 指针（v78：从 1 个扩成 16 个）。
 * flatten_try 挂在很热的 hooks 上（进场条件 / 队友条件 / Hold 判定），
 * 而游戏会按几十上百次/秒的节奏重判 Hold 条件 —— 只缓存一个的话，
 * 多个图交替出现时几乎每次都要重新取名字（unity_name + strcmp）。 */
#define FLAT_NOPE_MAX 16
static void* g_flat_nope[FLAT_NOPE_MAX];
static int g_flat_nope_n = 0;
static int g_flat_nope_rr = 0;

static int flat_already(void* g) {
    for (int i = 0; i < g_flat_nope_n; ++i)
        if (g_flat_nope[i] == g) return 1;
    for (int i = 0; i < g_flat_done_n; ++i)
        if (g_flat_done[i] == g) return 1;
    return 0;
}

static void flat_mark_nope(void* g) {
    if ((uintptr_t)g < 0x10000) return;
    for (int i = 0; i < g_flat_nope_n; ++i)
        if (g_flat_nope[i] == g) return;
    g_flat_nope[g_flat_nope_rr] = g;
    g_flat_nope_rr = (g_flat_nope_rr + 1) % FLAT_NOPE_MAX;
    if (g_flat_nope_n < FLAT_NOPE_MAX) g_flat_nope_n++;
}

static void flat_mark(void* g) {
    if (g_flat_done_n < FLAT_DONE_MAX) g_flat_done[g_flat_done_n++] = g;
}

/* 在已加载的图对象上套用补丁。node 可以是图上任意一个节点（用它的 graph 指针）。 */
void flatten_try(void* any_node) {
    if (!F_FLAT) return;
    if ((uintptr_t)any_node < 0x10000) return;

    void* g = *(void**)((char*)any_node + OFF_NODE_GRAPH);
    if ((uintptr_t)g < 0x10000) return;
    if (flat_already(g)) return;

    char gb[96];
    if (strcmp(unity_name(g, gb, sizeof(gb)), FLAT_GRAPH_NAME) != 0) {
        flat_mark_nope(g);
        return;
    }

    void* nodes = *(void**)((char*)g + OFF_GRAPH_NODES);
    int n = lcnt(nodes);
    if (n <= 0 || n > 4096) { flat_mark(g); return; }

    char cb[64];

    /* ---- 第一遍：按 ScenarioKey 找出起点和 5 段讲解 ---- */
    void* start = nullptr;
    void* chain[FLAT_CHAIN_N];
    for (int i = 0; i < FLAT_CHAIN_N; ++i) chain[i] = nullptr;

    for (int i = 0; i < n; ++i) {
        void* nd = litem(nodes, i);
        if (!nd) continue;
        if (*(void**)((char*)nd + OFF_NODE_GRAPH) != g) continue;   /* 见下：有效性校验 */
        cls_name(nd, cb, sizeof(cb));
        if (strcmp(cb, "StoryScenarioNode") != 0) continue;
        void* sk = *(void**)((char*)nd + OFF_SSN_SCENARIOKEY);
        if ((uintptr_t)sk < 0x10000) continue;
        char kb[96];
        str_to_buf(sk, kb, sizeof(kb));
        if (!start && strcmp(kb, FLAT_START_KEY) == 0) start = nd;
        for (int e = 0; e < FLAT_CHAIN_N; ++e)
            if (!chain[e] && strcmp(kb, k_chain_keys[e]) == 0) chain[e] = nd;
    }

    if (!start) {
        /* 不 mark：图可能还没加载完，下次 hook 再试。日志节流。 */
        if (g_flat_log < 4) {
            g_flat_log++;
            LOGI("[flat] 图 %s 里暂时没找到起点 %s（共 %d 个节点），稍后重试", gb, FLAT_START_KEY, n);
        }
        return;
    }

    /* 顺带把「入口节点出边取不到」这件事记清楚（v67 走链 0/5 的原因待定） */
    {
        void* o1 = node_port(start, "OutPort", 1);
        void* o0 = node_port(start, "OutPort", 0);
        LOGI("[flat] 入口 %s：OutPort(仅输出)=%s 连接数=%d；任意方向=%s 连接数=%d",
             FLAT_START_KEY, o1 ? "有" : "无", port_conn_count(o1),
             o0 ? "有" : "无", port_conn_count(o0));
    }

    /* ---- 第二遍：找「最后的 Negate=1 门」（门17） ----
     * v67 试过「从 _2_1 沿 OutPort.connections[1] 连走 5 步」，实测 0/5 步 ——
     * 第一步就取不到 _2_1 的出边（虽然 _2_2.._2_6 的 OutPort 都取得到）。
     * 所以改成不依赖入口节点、也不依赖类名的**条件扫描**：
     *   遍历所有节点 → GetPort(nd,"conditionPort") 非空 → 其 connections[0] 就是条件节点
     *   → 条件节点的 partnerID==10008 且 Negate==1 的那一个门就是门17。
     * 为了不误判，再加一条结构判据：条件节点必须还有 "stateIn" 端口（需要伙伴 的特征端口名）。 */
    void* gate_last = nullptr;
    {
        char cb2[64], nm2[96];
        int logged = 0;
        for (int i = 0; i < n && !gate_last; ++i) {
            void* nd = litem(nodes, i);
            if (!nd) continue;
            /* 有效性校验：图里的每个节点的 Node.graph(0x18) 都指回这张图。
             * 万一数组读偏了拿到别的东西，这一条能把它挡掉，不至于把垃圾当节点用。 */
            if (*(void**)((char*)nd + OFF_NODE_GRAPH) != g) continue;
            void* cp = node_port(nd, "conditionPort", 0);
            if (!cp) continue;
            void* cond = port_conn_node(cp, 0);
            if (!cond) continue;
            int pid = *(int*)((char*)cond + OFF_HP_PARTNER_ID);
            int neg = (int)*(unsigned char*)((char*)cond + OFF_HP_NEGATE);
            int has_statein = (node_port(cond, "stateIn", 0) != nullptr);
            if (logged < 14) {
                logged++;
                cls_name(nd, cb2, sizeof(cb2));
                unity_name(cond, nm2, sizeof(nm2));
                LOGI("[flat] 带 conditionPort 的节点 类=%s  条件类/名=%s/%s pid=%d neg=%d stateIn=%s",
                     cb2, cls_name(cond, cb2, sizeof(cb2)), nm2, pid, neg, has_statein ? "有" : "无");
            }
            if (pid == 10008 && neg == 1 && has_statein) gate_last = nd;
            /* 讲解链上的条件 → 登记为「恒真」，不改字段（见文件头注释） */
            for (int k2 = 0; k2 < FLAT_NEUTRAL_PIDS_N; ++k2)
                if (pid == k_neutral_pids[k2]) { flat_add_cond(cond); break; }
        }
    }

    /* 兜底：条件扫描没找到时，再试一次结构走链（v67 的方式） */
    int walked = 0;
    if (!gate_last) {
        void* p = start;
        for (int i = 0; i < FLAT_WALK_STEPS; ++i) {
            void* op = node_port(p, "OutPort", 1);
            int cc = port_conn_count(op);
            void* nxt = port_conn_node(op, (cc >= 2) ? 1 : 0);
            if (!nxt) break;
            p = nxt;
            walked++;
        }
        if (walked == FLAT_WALK_STEPS) gate_last = p;
        LOGI("[flat] 条件扫描没找到门17，改试结构走链：%d/%d 步", walked, FLAT_WALK_STEPS);
    }

    /* ---- 第三遍：改连接 ---- */
    int ok = 0, miss = 0;
    for (int e = 0; e < FLAT_CHAIN_N; ++e) {
        void* from = (e == 0) ? chain[0] : chain[e];
        void* to;
        if (e + 1 < FLAT_CHAIN_N) to = chain[e + 1];
        else                      to = gate_last;
        if (!from || !to) {
            miss++;
            if (g_flat_log < 12)
                LOGI("[flat] 缺项 %s（源=%s 目标=%s）", k_chain_keys[e],
                     from ? "有" : "无", to ? "有" : "无");
            continue;
        }
        void* op = node_port(from, "OutPort", 1);
        if (port_set_conn_node(op, 0, to)) ok++;
        else {
            miss++;
            if (g_flat_log < 12)
                LOGI("[flat] 缺项 %s 取不到 OutPort（连接数=%d）", k_chain_keys[e], port_conn_count(op));
        }
    }

    /* ---- 第四遍：兜底门也必须恒真 ----
     * 不改它的话：放宽关闭 + 凌梦蝶在队 → 门17 判假，而 _2_6 的出口已经指到它、
     * 没有兄弟可回退 → 剧情永久卡死。
     * v70 起不再改 partnerID/Negate 字段（那会破坏姓名显示），
     * 改成把它的条件节点登记进「恒真」表。 */
    int neu = 0;
    if (gate_last) {
        void* cp = node_port(gate_last, "conditionPort", 0);
        void* cond = port_conn_node(cp, 0);
        if (cond) { flat_add_cond(cond); neu = 1; }
    }

    flat_mark(g);
    g_flat_log++;
    LOGI("[flat] 图 %s 展开完成：改连 %d/%d，结构走链 %d/%d 步，兜底门=%s，接管条件 %d 个，缺 %d（共 %d 节点）",
         gb, ok, FLAT_CHAIN_N, walked, FLAT_WALK_STEPS,
         gate_last ? "有" : "无", g_flat_cond_n, miss, n);

    /* v77：趁这个时机（很早，日志额度还没被刷屏的门对吃掉）把讲解链 5 个角色的
     * 模板内容做一次只读体检 —— 直接回答「模板找得到吗 / 里面有没有真名」。 */
    for (int i = 0; i < FLAT_NEUTRAL_PIDS_N; ++i) relax_name_probe(k_neutral_pids[i]);
    /* 顺便把这 5 个名字窗口一起打开：后几位在剧情开始时可能没有实例，
     * 等轮到各自的门才开窗口就晚了。 */
    for (int i = 0; i < FLAT_NEUTRAL_PIDS_N; ++i) relax_name_open(k_neutral_pids[i], g);
}
