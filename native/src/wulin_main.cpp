/*
 * WulinTweaks · Android 原生插件（v4：易物强化 + 游戏内面板）
 * ==============================================================
 * 通道：il2cpp_init 首指令被静态改成 B 到插页里的引导桩，桩 dlopen 本库。
 * 本文件负责：
 *   1) 等 libil2cpp.so / il2cpp 运行时就绪，解析出全部 RVA 与 il2cpp C API
 *   2) 装 hook：
 *        易物强化（trade.cpp：作价恒占优含搬空 + 池子）
 *        大地图 / 马车地图（IsMapEnable / IsPassIcon / HashSet.Contains / initIcon / cabIcon）
 *        剧情条件放宽（两个 CheckAllConnectNode）
 *        游戏内面板（ui.cpp，每帧 tick 挂在 MainThreadSynchronizationContext.Update）
 *   3) 全部功能都由运行时开关控制，面板点一下即时生效（默认全开）
 *
 * 静态补丁已全部取消（除了「鸣谢 → 模组页」那条 4 字节补丁，在 libil2cpp_intl_ui.so 里），
 * 因为静态补丁写死不可关，而面板要求每个开关都能实时切换。
 */
#include "common.h"
#include "hook.h"
#include "trade.h"
#include "ui.h"
#include "probe.h"
#include "water.h"
#include "join.h"

#include <dlfcn.h>
#include <link.h>
#include <pthread.h>
#include <string.h>
#include <stdio.h>
#include <unistd.h>
#include <stdarg.h>
#include <fcntl.h>
#include <time.h>

/* 游戏内面板开关：1 = 带面板（默认展开、可折叠），0 = 只装功能不建 UI */
#define WULIN_PANEL 1

/* 排查探针：1 = 往 wulin.log 打水面/阻挡诊断，正式包改回 0 */
#define WULIN_PROBE 0
/* 名字排查探针（v92/v93 那套）：1 = 把 TMP 里的 ASCII 文本、getter 返回的 ASCII 名、
 * 以及实例/模板的名字字段全打出来。定位「名字变 OutPort」时用过，正式包保持 0。
 * v94 的读侧消毒与 v91 的类型校验不受这个开关影响。 */
#define WULIN_NAMEPROBE 0

/* DynamicModifier 运行时懒加载兜底：实测不可用（见 install_tweaks 注释） */
#define WULIN_DM_LAZY 0

/* ------------------------------------------------------------------ 全局 */
uintptr_t g_base = 0;
void* g_i2cpp = nullptr;

volatile int F_TRADE_WIN = 1;      /* 易物作价恒占优（含确认搬空） */
volatile int F_TRADE_POOLS = 1;
volatile int F_MAP = 1;
volatile int F_CAB = 1;
volatile int F_STORY = 1;   /* v44 起默认开（此前是 0，每次启动要手动点） */
volatile int F_FLAT = 1;    /* v65 剧情分支展开，默认开 */
volatile int F_WATER = 1;

fn_i2c_get A_domain_get = nullptr;
fn_i2c_asm A_get_assemblies = nullptr;
fn_i2c_img A_get_image = nullptr;
fn_i2c_cls A_class_from_name = nullptr;
fn_i2c_cls_ty A_class_get_type = nullptr;
fn_i2c_ty_obj A_type_get_object = nullptr;
fn_i2c_new A_object_new = nullptr;
fn_i2c_str A_string_new = nullptr;
fn_i2c_arr A_array_new = nullptr;
fn_i2c_clsinit A_class_init = nullptr;
fn_i2c_objcls A_obj_get_class = nullptr;
fn_i2c_clsname A_class_get_name = nullptr;

/* ------------------------------------------------------------------ 日志 */
void glog(const char* fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf) - 2, fmt, ap);
    va_end(ap);
    __android_log_print(ANDROID_LOG_INFO, "wulin", "%s", buf);
    int fd = open(LOG_PATH, O_WRONLY | O_CREAT | O_APPEND, 0644);
    if (fd >= 0) {
        size_t n = strlen(buf);
        buf[n] = '\n';
        write(fd, buf, n + 1);
        close(fd);
    }
}

/* ============================================================ 反射小工具 */
static void** g_imgs = nullptr;
static size_t g_nimg = 0;

static void cache_images() {
    if (g_imgs || !A_domain_get) return;
    void* dom = A_domain_get();
    if (!dom) return;
    size_t n = 0;
    void** asms = A_get_assemblies(dom, &n);
    if (!asms || !n) return;
    static void* imgs[512];
    if (n > 512) n = 512;
    for (size_t i = 0; i < n; ++i) imgs[i] = A_get_image(asms[i]);
    g_imgs = imgs;
    g_nimg = n;
}

void* find_class(const char* ns, const char* name) {
    cache_images();
    if (!g_imgs) return nullptr;
    for (size_t i = 0; i < g_nimg; ++i) {
        if (!g_imgs[i]) continue;
        void* k = A_class_from_name(g_imgs[i], ns, name);
        if (k) return k;
    }
    return nullptr;
}

void* class_type(void* klass) {
    if (!klass) return nullptr;
    if (A_class_init) A_class_init(klass);
    /* 注意：il2cpp_class_get_type 返回的是 const Il2CppType*（&klass->byval_arg），
     * 不是 System.Type。必须再用 il2cpp_type_get_object 包一层，
     * 否则 AddComponent(Type) / FindObjectOfType(Type) 会在 libil2cpp 里 null 解引用（fault 0x135）。 */
    const void* ty = A_class_get_type(klass);
    if (!ty || !A_type_get_object) return nullptr;
    return A_type_get_object((void*)ty);
}

void* find_object_of_type(void* klass) {
    void* t = class_type(klass);
    if (!t) return nullptr;
    typedef void* (*fn_find)(void*, void*);
    return ((fn_find)(g_base + RVA_FIND_OBJECT_TYPE))(t, nullptr);
}

const char* str_to_buf(void* il2str, char* buf, int cap) {
    if (!buf || cap <= 0) return "";
    buf[0] = 0;
    if (!il2str) return buf;
    int len = *(int*)((char*)il2str + 0x10);
    if (len <= 0) return buf;
    const uint16_t* c = (const uint16_t*)((char*)il2str + 0x14);
    int o = 0;
    for (int i = 0; i < len; ++i) {
        uint32_t u = c[i];
        if (u < 0x80) {
            if (o + 1 >= cap) break;
            buf[o++] = (char)u;
        } else if (u < 0x800) {
            if (o + 2 >= cap) break;
            buf[o++] = (char)(0xC0 | (u >> 6));
            buf[o++] = (char)(0x80 | (u & 0x3F));
        } else if (u >= 0xD800 && u <= 0xDBFF && i + 1 < len &&
                   c[i + 1] >= 0xDC00 && c[i + 1] <= 0xDFFF) {
            uint32_t cp = 0x10000 + ((u - 0xD800) << 10) + (c[++i] - 0xDC00);
            if (o + 4 >= cap) break;
            buf[o++] = (char)(0xF0 | (cp >> 18));
            buf[o++] = (char)(0x80 | ((cp >> 12) & 0x3F));
            buf[o++] = (char)(0x80 | ((cp >> 6) & 0x3F));
            buf[o++] = (char)(0x80 | (cp & 0x3F));
        } else {
            if (o + 3 >= cap) break;
            buf[o++] = (char)(0xE0 | (u >> 12));
            buf[o++] = (char)(0x80 | ((u >> 6) & 0x3F));
            buf[o++] = (char)(0x80 | (u & 0x3F));
        }
    }
    buf[o] = 0;
    return buf;
}

/* ====================================================== 大地图 / 马车地图 */
static int (*orig_contains)(void* set, int id, void* type) = nullptr;
static void* g_uimap = nullptr;
static void* g_data = nullptr;
static void* g_set_enable = nullptr;
static void* g_set_vis = nullptr;
static void* g_set_allow = nullptr;
static void* g_set_deny = nullptr;
static int g_map_log = 0;

static void resolve_save() {
    /* 这个元数据槽里放的是 MonoSingleton<UIWorldMapManager>.get_Instance 的泛型用法槽，
     * 按 MethodInfo 传给共享泛型实现就能拿到实例。v3 就是这么拿到存档数据的，别改。 */
    void** cell = *(void***)(g_base + VA_CLASS_CELL_UIMAP);
    if (!cell || !*cell) return;
    typedef void* (*fn_single)(void*);
    void* sm = ((fn_single)(g_base + RVA_SINGLETON_GET))(*cell);
    if (!sm) return;
    void* save = *(void**)((char*)sm + 0xD8);
    if (!save) return;
    void* data = *(void**)((char*)save + 0x20);
    if (!data) return;
    g_data = data;
    g_set_enable = *(void**)((char*)data + 0x90);
    g_set_vis = *(void**)((char*)data + 0xA0);
    g_set_allow = *(void**)((char*)data + 0xD0);
    g_set_deny = *(void**)((char*)data + 0xD8);
}

/* ---- 存档集合的「尽早解析」 ------------------------------------------------
 * 大地图 / 马车地图的解锁，全靠 hook_contains 认出这四个 HashSet 指针
 * （enable / visible / allow / deny）后强行返回 1/0。
 * 但 resolve_save() 原来只有两个调用点，都在马车地图的路径上
 * （hook_cabIcon ← UICabMapManager.UpdateIconShow、inject_mapdata）。
 *
 * 后果：新开档 / 没打开过马车地图时，四个指针一直是 NULL，
 * hook_contains 认不出任何集合 → 大地图照样锁定；
 * 只有等玩家打开一遍马车地图，resolve_save 跑过之后才「突然生效」。
 * 这就是「开放大地图需要打开一遍马车地图才生效」的成因。
 *
 * 修法：把 resolve_save() 提到所有用到它的入口，谁先来谁解析
 * （大地图打开 → initIcon；IsMapEnable / IsPassIcon；马车 UpdateIconShow）。
 * 解析本身很轻（读一个元数据槽 + 几次解引用），但为了不给热路径添开销，
 * 未就绪时最多每 200ms 试一次；就绪后只做一次指针比较。
 *
 * 注意：**不在 hook_contains 里解析**。那是共享泛型 HashSet<T>.Contains，
 * 存档/资源加载期间可能被后台线程调用，从工作线程碰 il2cpp（MonoSingleton.get_Instance、
 * 解引用 Unity 对象）会直接崩。上面几个入口都是主线程的 UI 方法，安全。 */
static uint64_t rule_now_ms(void);          /* 定义在剧情缓存那一节 */
static void char_mgr_reset(void);           /* 定义在「从未入队」那一节：换档后清角色管理器缓存 */

static int save_sets_ready(void) {
    return ((uintptr_t)g_set_enable > 0x10000) || ((uintptr_t)g_set_vis > 0x10000);
}

static uint64_t g_save_try_t = 0;
static int g_save_busy = 0;
static void* g_last_enable = nullptr;

/* force=1：忽略节流、无条件重新解析（大地图 initIcon / 马车 UpdateIconShow 这种低频入口用；
 *          换档后集合指针会变，靠它们刷新）。
 * force=0：只在未就绪时每 200ms 试一次，就绪后立刻返回（IsMapEnable/IsPassIcon 用）。 */
static int ensure_save(int force) {
    int ready = save_sets_ready();
    if (ready && !force) return 1;
    if (g_save_busy) return ready;

    uint64_t now = rule_now_ms();
    if (!force && g_save_try_t && now - g_save_try_t < 200) return ready;
    g_save_try_t = now;

    g_save_busy = 1;
    resolve_save();
    g_save_busy = 0;

    ready = save_sets_ready();
    if (ready && g_set_enable != g_last_enable) {
        LOGI("地图: 存档集合%s enable=%p vis=%p allow=%p deny=%p",
             g_last_enable ? "换档" : "已就绪", g_set_enable, g_set_vis, g_set_allow, g_set_deny);
        g_last_enable = g_set_enable;
        char_mgr_reset();       /* 换档后 GameCharacterManager 可能换实例，别用旧指针 */
    }
    return ready;
}

/* ---- 剧情条件期间临时关掉地图解锁 -----------------------------------------
 * 「判断大地图图标显示」这个条件节点内部走的是
 *     UIWorldMapManager.IsCityVisible(mapID)
 *         → [uimap+0xD8]+0x20 的管理器 +0xA0（= 我们的 g_set_vis）→ HashSet<int>.Contains
 * （反汇编 IsCityVisible 确认：尾部就是 b 到 Contains）。
 * 所以手机端在 Contains 上做的全局解锁会把「图标没显示」判成「显示了」
 * → 这类剧情门槛恒真 → 剧情提前可获知
 * （实例：R10112-2「烈酒与故事」的入口就是 判断大地图图标显示(5064)）。
 *
 * 解法：在这两个条件节点被判定期间把解锁拦截临时关掉（g_map_suppress>0），
 * 让它读原版数据；UI 那侧不受影响，地图照样全开。
 * 效果等价于 PC 端「只在 UI 层 patch、不碰数据」的做法。 */
static volatile int g_map_suppress = 0;
static int g_suppress_log = 0;

static int hook_contains(void* set, int id, void* type) {
    /* 这里只做集合比对，不解析存档（见上面注释：可能是后台线程） */
    if (g_map_suppress > 0) {
        /* 剧情里的「图标显示」类条件：一律读原版数据（enable/visible/allow/deny 都不拦） */
        if (set == g_set_vis && g_suppress_log < 24) {
            g_suppress_log++;
            LOGI("[cond] 剧情条件期间走原版：visible.Contains(%d) 不强制为 1", id);
        }
        return orig_contains ? orig_contains(set, id, type) : 0;
    }
    if (set && F_MAP) {
        if (set == g_set_enable || set == g_set_vis) {
            if (g_map_log < 6) {
                LOGI("地图: %s-set id=%d -> 1", set == g_set_enable ? "enable" : "visible", id);
                g_map_log++;
            }
            return 1;
        }
    }
    if (set && F_CAB) {
        if (set == g_set_allow) return 1;
        if (set == g_set_deny) return 0;
    }
    return orig_contains ? orig_contains(set, id, type) : 0;
}

/* 「判断大地图图标显示」/「判断马车图标是否通过」：判定期间抑制地图解锁 */
static bool (*orig_city_visible_cc)(void* self, int io, void* line) = nullptr;
static bool hook_city_visible_cc(void* self, int io, void* line) {
    if (!orig_city_visible_cc) return true;
    g_map_suppress++;
    bool r = orig_city_visible_cc(self, io, line);
    g_map_suppress--;
    return r;
}

static bool (*orig_cab_pass_cc)(void* self, int io, void* line) = nullptr;
static bool hook_cab_pass_cc(void* self, int io, void* line) {
    if (!orig_cab_pass_cc) return true;
    g_map_suppress++;
    bool r = orig_cab_pass_cc(self, io, line);
    g_map_suppress--;
    return r;
}

/* UIWorldMapManager.IsMapEnable(int) */
static bool (*orig_is_map_enable)(void* self, int id) = nullptr;
static bool hook_is_map_enable(void* self, int id) {
    if (!save_sets_ready()) ensure_save(0);
    bool r = orig_is_map_enable ? orig_is_map_enable(self, id) : false;
    return F_MAP ? true : r;
}

/* UICabMapManager.IsPassIcon(int) */
static bool (*orig_is_pass_icon)(void* self, int id) = nullptr;
static bool hook_is_pass_icon(void* self, int id) {
    if (!save_sets_ready()) ensure_save(0);
    bool r = orig_is_pass_icon ? orig_is_pass_icon(self, id) : false;
    return F_CAB ? true : r;
}

static void (*orig_initIcon)(void* self) = nullptr;
static void hook_initIcon(void* self) {
    if (self && !g_uimap) {
        g_uimap = self;
        LOGI("大地图: uimap=%p listWorldMap=%p", g_uimap, *(void**)((char*)self + 0xE0));
    }
    /* 先解析存档集合，再让游戏建图标 —— 这样第一遍建出来的图标就是解锁态，
     * 不用等玩家去开一次马车地图。 */
    int ready = ensure_save(1);
    if (orig_initIcon) orig_initIcon(self);
    /* 建图标时集合还没解析出来（存档刚载入等情形）：图标已按原版锁定态建好，
     * 而集合刚刚就绪，重建一遍（等价于 inject_mapdata 末尾那次 orig_initIcon）。 */
    if (!ready && orig_initIcon && ensure_save(1)) {
        static int rebuilt = 0;
        if (!rebuilt) {
            rebuilt = 1;
            orig_initIcon(self);
            LOGI("大地图: 集合晚于图标初始化，已重建图标列表");
        }
    }
}

/* 名字 → 数字（马车图标名就是 mapID） */
static int parse_id_from_name(void* str) {
    if (!str) return -1;
    int len = *(int*)((char*)str + 0x10);
    if (len <= 0 || len > 32) return -1;
    const uint16_t* c = (const uint16_t*)((char*)str + 0x14);
    int v = 0;
    for (int i = 0; i < len; ++i) {
        if (c[i] < '0' || c[i] > '9') return -1;
        v = v * 10 + (c[i] - '0');
    }
    return v;
}

static void inject_mapdata(void* self) {
    void* tr = self ? *(void**)((char*)self + 0xB8) : nullptr;
    if (!tr) return;
    if (!g_data) resolve_save();
    if (!g_data) { LOGI("马车: 存档对象未就绪，跳过 MapData 注入"); return; }

    int n = ((int (*)(void*, void*))(g_base + RVA_T_CHILD_COUNT))(tr, nullptr);
    int made = 0, ids[512], nid = 0;
    for (int i = 0; i < n && nid < 512; ++i) {
        void* child = ((void* (*)(void*, int, void*))(g_base + RVA_T_GET_CHILD))(tr, i, nullptr);
        if (!child) continue;
        void* obj = ((void* (*)(void*, void*))(g_base + RVA_GO_CHILD_OBJ))(child, nullptr);
        if (!obj) continue;
        void* nm = ((void* (*)(void*, void*))(g_base + RVA_GET_NAME))(obj, nullptr);
        int id = parse_id_from_name(nm);
        if (id <= 0 || id >= 1000000) continue;
        bool dup = false;
        for (int k = 0; k < nid; ++k) if (ids[k] == id) { dup = true; break; }
        if (dup) continue;
        ids[nid++] = id;
        ((void* (*)(void*, int))(g_base + RVA_GET_OR_CREATE_MAPDATA))(g_data, id);
        made++;
    }
    LOGI("马车: children=%d mapdata+%d", n, made);
    if (made > 0 && g_uimap && orig_initIcon) {
        orig_initIcon(g_uimap);
        LOGI("马车: 世界地图已重建 (+%d)", made);
    }
}

static void (*orig_cabIcon)(void* self) = nullptr;
static void hook_cabIcon(void* self) {
    ensure_save(1);
    if (orig_cabIcon) orig_cabIcon(self);
    if (F_CAB) inject_mapdata(self);
}

/* ============================================================== 剧情条件 */
static bool (*orig_check_joined)(void* self, int io, void* line) = nullptr;
static bool (*orig_check_partner)(void* self, int io, void* line) = nullptr;
typedef bool (*fn_check)(void*, int, void*);

/* 只跳过子类的「队友判断」，其余条件照查 --------------------------------
 *
 * 0x3D936EC / 0x3D923FC 反汇编出来是这么个结构：
 *     if (CheckAllConnectNodeAction == null) return ...;
 *     if (!CheckAllConnectNodeAction(self, io, line)) return false;   ← 队友在队判断
 *     return base.CheckAllConnectNode(io, line);                      ← 尾调用基类
 * 基类 0x3D7E2A0 做的事是：把 self 压进 line → CheckAllExceptSelf(io, line) → 弹出 self。
 * 所以「跳过子类、直接跑基类」= 只丢掉 CheckAllConnectNodeAction 这一条，
 * 连线上其它条件节点照常检查 —— 与 PC 版 WulinTweaks 的 jump-to-base 完全同义。
 *
 * 历史：v5 曾改成「hook 恒 return true」+ 每秒 20 次自旋闸门。恒真会让剧情 Hold 节点
 * 变成"条件随时满足"，StoryManager.ContinueArrange 与 HoldNode.OnActive 互相递归，
 * 切场景栈溢出闪退，那才是闸门存在的理由。走基类不改变 line 的语义、不会恒真，
 * 这个副作用不存在，闸门一并去掉。 */
static fn_check base_check_all = nullptr;

/* 下面两个函数定义在后面，这里先声明 */
static const char* obj_name(void* o, char* buf, int cap);
static int story_blacklisted(const char* g);

/* 主线和师父的故事线：固定按原版判断（不放宽）。
 * 末尾两条是「用户点名要压住」的个别剧情（条件是别人角色，按彩蛋规则会放宽）。 */
static const char* k_fixed_original[] = {
    "Fame-2",
    "Guide10099-1",                  /* 2026-10-21 追加：与 6/11/13… 同属师父线 */
    "Guide10099-6",  "Guide10099-11", "Guide10099-13",
    "Guide10099-15", "Guide10099-16", "Guide10099-18",
    "R10112-2",                      /* 2026-10-21 追加：烈酒与故事。图里 8 条「需要伙伴」全是别人的
                                      * 角色（10008/10009/10005/10015），按彩蛋规则会被放宽 → 剧情提前播出。
                                      * 用户要求压回原版：那些角色不在队就不该演。 */
    /* ---- 2026-10-23 追加（用户点名）---- */
    "R20004-6",                      /* 伙伴的故事 / 包打听(20004)。图里对花四娘(20025)的「需要伙伴」
                                      * 属于「别的角色」→ 按彩蛋规则必放宽；而图里有 3 个
                                      * ChangePartnerNode(state=1, partnerID=20004) = 把包打听踢出队。
                                      * 放宽后这段在不该播的时候播了（结缘花四娘 + 同队时"包打听离队"）。*/
};

/* R10005-11 -> 10005；不是 "R数字-" 形式返回 -1 */
static int parse_r_id(const char* gn) {
    if (!gn || gn[0] != 'R') return -1;
    int v = 0, i = 1;
    while (gn[i] >= '0' && gn[i] <= '9' && i < 12) { v = v * 10 + (gn[i] - '0'); ++i; }
    if (i < 3) return -1;            /* 至少两位数字 */
    if (gn[i] != '-') return -1;     /* 必须是 R<id>- */
    return v;
}

/* 该角色是否从未入队：GameCharacterInstance.m_TeamMemberState(+0x130) == NULL。
 * 实测（老档）：苗彩蝶/曼陀罗/癞头癫 = NULL；猎户/铁匠/玩家 = 非 NULL（入过队）。
 *
 * 管理器实例缓存 60 秒。换档/重载后 GameCharacterManager 可能换实例，
 * 旧指针悬空会读到垃圾甚至崩，所以定期重找；换档时由 ensure_save() 主动清。 */
static void* g_char_mgr = nullptr;
static uint64_t g_char_mgr_t = 0;
#define CHAR_MGR_TTL_MS 60000

static void clear_relax_name_state(void);   /* 定义在「姓名」那一节 */
static void char_mgr_reset(void) {
    g_char_mgr = nullptr;
    g_char_mgr_t = 0;
    /* v63：换档/读档后旧的角色实例与它持有的字符串全部作废，名字缓存必须一起清，
     * 否则 g_relax_stranger / g_relax_real 变悬空指针，而 90 秒窗口可能还开着。 */
    clear_relax_name_state();
}

static void* char_mgr() {
    uint64_t now = rule_now_ms();
    if ((uintptr_t)g_char_mgr > 0x10000 && now - g_char_mgr_t < CHAR_MGR_TTL_MS) return g_char_mgr;
    /* GameCharacterManager 在 WuLin 命名空间下（dump 确认），namespace 传空会找不到 */
    void* k = find_class("WuLin", "GameCharacterManager");
    void* inst = k ? find_object_of_type(k) : nullptr;
    g_char_mgr = ((uintptr_t)inst > 0x10000) ? inst : nullptr;
    g_char_mgr_t = now;
    return g_char_mgr;
}

/* 判定缓存：条件节点 -> (是否按原版, 时间戳)，TTL 3 秒。
 * 入队状态会变，不能永久缓存；但条件检查是热路径，每次读图名+调 GetCharacter 会卡。 */
#define RULE_TTL_MS 3000
#define RULE_CACHE_N 256
static struct { void* node; int keep; uint64_t t; } g_rule_cache[RULE_CACHE_N];
static int g_rule_cache_i = 0;

static uint64_t rule_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000);
}

/* 2026-10-21 修正：拿不到实例 / 管理器未就绪时，旧实现返回 0（当作已入队 → 放宽）。
 * 新档里游戏还没创建大部分角色实例，判据于是集体失效 → 本命角色的个人剧情被放宽，
 * 不该触发的剧情提前触发（PC 端日志同源：`[nj] uid=10006 实例未创建`）。
 * 语义上「连实例都没有」必然没入过队，所以兜底只能是 1（不放宽，走原版）。
 * ⚠️ 这与 MapKit/CharKit 那类「查不到就放行」的兜底方向相反 —— 这里的放行等于放宽剧情。 */
static int g_nj_log = 0;

static int never_joined(int uid) {
    if (uid <= 0) return 0;

    void* m = char_mgr();
    if ((uintptr_t)m < 0x10000) {
        if (g_nj_log++ < 40) LOGI("[nj] uid=%d 管理器未就绪 → 按「从未入队」（不放宽）", uid);
        return 1;
    }
    typedef void* (*fn_getchar)(void*, int);
    void* c = ((fn_getchar)(g_base + RVA_GET_CHARACTER))(m, uid);
    if ((uintptr_t)c < 0x10000) {
        if (g_nj_log++ < 40) LOGI("[nj] uid=%d 实例未创建 → 从未入队（不放宽）", uid);
        return 1;
    }
    void* tms = *(void**)((char*)c + OFF_TEAM_STATE);
    int v = ((uintptr_t)tms < 0x10000) ? 1 : 0;
    if (g_nj_log++ < 40) LOGI("[nj] uid=%d ptr=%p tms=%p → neverJoined=%d", uid, c, tms, v);
    return v;
}

/* 这个「角色在队」条件节点是否要**按原版判断**（不放宽）。
 *
 * 规则（与 PC 端一致）：
 *   1) R{角色id}-xxx 这类角色剧情图：只有当条件里的 partnerID == 图名里的角色 id 时才走原版
 *      —— 该角色入队后才开启自己的故事线；条件是别的角色时照常放宽（用来触发其它彩蛋）。
 *   2) Fame-2 / Guide10099-6,11,13,15,16,18（主线与师父线）固定走原版。
 *   3) 手动名单 story_blacklisted() 仍然生效。
 *   4) 其余一律放宽。
 * 读不到图名时保守返回 0（放宽），避免误挡。 */
static int keep_original_for_uncached(void* condNode);

/* v109：同一张剧情图只放宽一次（不再用固定名单） */
#define RELAX_ONCE_N 128
static char g_ro_name[RELAX_ONCE_N][64];
static int  g_ro_i = 0;
static int  g_ro_log = 0;
static int relax_once(const char* gn) {
    if (!gn || !gn[0]) return 1;
    for (int i = 0; i < RELAX_ONCE_N; ++i)
        if (g_ro_name[i][0] && strcmp(g_ro_name[i], gn) == 0) {
            if (g_ro_log++ < 12) LOGI("[relax] 图 %s 已放宽过一次 -> 本次走原版", gn);
            return 0;
        }
    snprintf(g_ro_name[g_ro_i], sizeof(g_ro_name[0]), "%s", gn);
    g_ro_i = (g_ro_i + 1) % RELAX_ONCE_N;
    return 1;
}

/* 带 TTL 缓存的入口：条件检查是热路径，若每次读图名 + 调 GetCharacter 会卡。
 * 入队状态会变，所以 TTL 3 秒，不能永久缓存。 */
static int keep_original_for(void* condNode) {
    if ((uintptr_t)condNode < 0x10000) return 0;

    uint64_t now = rule_now_ms();
    for (int i = 0; i < RULE_CACHE_N; ++i) {
        if (g_rule_cache[i].node == condNode) {
            if (now - g_rule_cache[i].t < (uint64_t)RULE_TTL_MS) return g_rule_cache[i].keep;
            break;
        }
    }

    int result = keep_original_for_uncached(condNode);

    g_rule_cache[g_rule_cache_i].node = condNode;
    g_rule_cache[g_rule_cache_i].keep = result;
    g_rule_cache[g_rule_cache_i].t = now;
    g_rule_cache_i = (g_rule_cache_i + 1) % RULE_CACHE_N;
    return result;
}

/* ---- v95：含「结缘界面」的图一律不放宽（与 PC 1.11.19 对齐） -----------------
 * 这类图（12 张，全在月老祠 map 5016）的收尾会写人物数据：`激活NPC情缘`（SetBond 双向写）、
 * `获得武功或绝学`（武功）、buff 85xx/89xx/88xx、`改变NPC关系值`。
 * 她不在队 / 实例不唯一时，这些写入会落到临时实例上 → 人物数据分裂。
 * 所以直接跳过放宽，走原版「必须在队」。其余本命图照旧放宽（人不在队也照演）。 */
#define BOND_CACHE_N 64
static void* g_bond_g[BOND_CACHE_N];
static int   g_bond_v[BOND_CACHE_N];
static int   g_bond_i = 0;
static int   g_bond_log = 0;

static const char* node_class_name(void* node);   /* 定义在「ContinueArrange 递归」那一节 */

/* 已知的 12 张结缘图（扫不到类名时的兜底；DLC 新增的靠节点扫描发现）。 */
static const char* k_bond_graphs[] = {
    "R10008-5", "R10009-6", "R10039-9", "R10052-3", "R10056-4", "R10064-6",
    "R10081-3", "R10084-5", "R10092-4", "R10108-3", "R20025-11", "R20063-2",
};

static void* node_list_item(void* list, int i) {
    if ((uintptr_t)list < 0x10000 || i < 0) return nullptr;
    void* arr = *(void**)((char*)list + OFF_LIST_ITEMS);
    if ((uintptr_t)arr < 0x10000) return nullptr;
    void* v = *(void**)((char*)arr + OFF_ARRAY_DATA + (size_t)i * 8);
    return ((uintptr_t)v > 0x10000) ? v : nullptr;
}

static int is_bond_graph(void* g, const char* gn) {
    if ((uintptr_t)g < 0x10000) {
        for (size_t i = 0; i < sizeof(k_bond_graphs) / sizeof(k_bond_graphs[0]); ++i)
            if (gn && strcmp(gn, k_bond_graphs[i]) == 0) return 1;
        return 0;
    }
    for (int i = 0; i < BOND_CACHE_N; ++i)
        if (g_bond_g[i] == g) return g_bond_v[i];

    int hit = 0, scanned = 0;
    void* nodes = *(void**)((char*)g + OFF_GRAPH_NODES);
    if ((uintptr_t)nodes > 0x10000) {
        int n = *(int*)((char*)nodes + OFF_LIST_SIZE);
        if (n > 0 && n <= 4096) {
            scanned = 1;
            for (int i = 0; i < n; ++i) {
                void* nd = node_list_item(nodes, i);
                if (!nd) continue;
                if (*(void**)((char*)nd + OFF_NODE_GRAPH) != g) continue;   /* 数组读偏的保险 */
                const char* cn = node_class_name(nd);
                if (cn && strcmp(cn, "ShowUIBondsNode") == 0) { hit = 1; break; }
            }
        }
    }
    if (!scanned) {                                  /* 扫不了：按 12 张已知图名 */
        for (size_t i = 0; i < sizeof(k_bond_graphs) / sizeof(k_bond_graphs[0]); ++i)
            if (gn && strcmp(gn, k_bond_graphs[i]) == 0) { hit = 1; break; }
        if (g_bond_log++ < 20) LOGI("[team] 扫图失败（%s）→ 退回图名表", gn ? gn : "?");
    }

    g_bond_g[g_bond_i] = g;
    g_bond_v[g_bond_i] = hit;
    g_bond_i = (g_bond_i + 1) % BOND_CACHE_N;
    return hit;
}

static int keep_original_for_uncached(void* condNode) {
    void* g = *(void**)((char*)condNode + OFF_NODE_GRAPH);
    if ((uintptr_t)g < 0x10000) return 0;

    char gb[128];
    const char* gn = obj_name(g, gb, sizeof(gb));
    if (!gn || !gn[0] || gn[0] == '?') return 0;

    for (size_t i = 0; i < sizeof(k_fixed_original) / sizeof(k_fixed_original[0]); ++i)
        if (strcmp(gn, k_fixed_original[i]) == 0) return 1;

    if (story_blacklisted(gn)) return 1;

    int selfId = parse_r_id(gn);
    if (selfId <= 0) return 0;                       /* 非角色剧情图：放宽 */

    int pid = *(int*)((char*)condNode + OFF_PARTNER_ID);
    if (pid != selfId) return relax_once(gn) ? 0 : 1;  /* 彩蛋：放宽（每图一次） */

    if (is_bond_graph(g, gn)) {                      /* v95：结缘图跳过放宽 */
        if (g_bond_log++ < 20)
            LOGI("[team] 图 %s 含「结缘界面」→ 跳过放宽（按原版：必须在队）", gn);
        return 1;
    }
    if (never_joined(pid)) return 1;                 /* 本命角色：从未入队 → 不放宽 */
    return relax_once(gn) ? 0 : 1;                   /* 已入过队 → 只放宽一次 */
}

/* 每次「需要伙伴 / 有伙伴」条件判定 +1（按线程）。
 * HoldNode.CheckCondition 用它判断「这次判定到底有没有碰队友条件」：
 * 没碰就说明这个 Hold 与放宽无关，不该被压制 —— 否则会打断游戏设计的可重复剧情。 */
static __thread int g_team_cond_hits = 0;

/* ---- 剧情期间显示「熟悉后姓名」（v61 → v72 改多槽） ---------------------
 * 放宽之后剧情触发并不算"互动过一次"，所以那些角色仍是陌生名（@冷峻严肃的中年人），
 * 光看立绘分不清是谁的彩蛋。做法：放宽命中时记下「放过了谁」并开窗口，
 * 窗口内把这个角色的姓名换成「熟悉后姓名」。
 *
 * ⚠️ v72 重写：原来是「单个全局 uid + 90 秒窗口」，实测被一个反复判定的门对钉死：
 *      [name] 放宽命中登记 uid=10075 neg=0   ×30
 *      [name] 放宽命中登记 uid=10033 neg=1   ×30
 *      [name] getter uid=42155 窗口 uid=10075 → 跳过
 *    某个 Hold 每帧重判一次，`uid=10075` 就每帧续命 → 窗口永远不换人 →
 *    洛千雪之后所有讲解的姓名都换不出来。
 *    v63 之所以看着能用，是那时只测了「只有一个放宽角色」的场景，单槽碰巧没撞车。
 *
 * 现在每个 uid 一个独立槽位 + 独立到期时间，刷屏的门对只能污染它自己那一格。 */
#define RN_SLOTS 24
/* ⚠️ v90 修掉的真 bug：这里原来把「陌生名 / 真名 / 原始姓 / 原始名 / 模板原陌生名」
 * 全部以 void* 直接缓存托管字符串（il2cpp string）。而我们随后就把实例/模板上的那个
 * 字段改成别的字符串 —— 原来那串在托管堆上就**没人引用了**，Boehm GC 很快回收，
 * 之后再用缓存的裸指针就是 dangling：还原时把野指针写回 m_surName/m_givenName，
 * 或者把 real 交给 TMP 显示。碰巧那块内存被别的字符串对象复用（本 mod 自己每次
 * 展开剧情图都会 A_string_new("OutPort") / "conditionPort"），于是人物名字就显示成
 * "OutPort" —— 就是这版之前偶发的「名字变成 outport」。
 * 现在一律缓存**文本快照**（UTF-8 / UTF-16），要字符串就现 new 一个。 */
#define RN_SLOTS 24
#define RN_U16_MAX 48            /* 陌生名快照（UTF-16 码元）上限 */
typedef struct {
    int      uid;
    uint16_t stranger_u16[RN_U16_MAX];   /* 陌生名快照，用于和 TMP 文本逐码元比对 */
    int      stranger_u16_len;
    char     real_txt[128];               /* 「熟悉后姓名」快照 */
    uint64_t until;      /* 到期时刻（ms） */
    /* v75：数据层改名用 —— 直接把实例的 m_surName/m_givenName 换成真名，
     * 这样**任何 UI 路径**读到的都是真名，不用再猜谁是显示 sink。 */
    void*    renamed_inst;   /* 被改过名的角色实例（换档后指针失效，恢复时必须比对） */
    char     orig_sur_txt[64];       /* 原始姓（文本快照） */
    char     orig_given_txt[128];     /* 原始名（文本快照） */
    int      orig_sur_null;          /* 原本就是 null 字符串 → 还原成 null */
    int      orig_given_null;
    int      renamed;
    /* v76：模板改名 —— 没有实例的角色（不在队/不在场景）只能从 CharacterPoolData 读名字，
     * 直接改它的 NameAsStranger(0x20) 才是覆盖到位的做法。 */
    void*    tpl;
    char     orig_tpl_stranger_txt[128];
    int      orig_tpl_stranger_null;
    int      tpl_patched;
    /* v79：这个窗口是从哪张剧情图记下来的。播放期间只改「同一张图」的角色，
     * 避免把全城被重判放宽的 NPC 都改名。 */
    void*    graph;
} RN_SLOT;
static RN_SLOT g_rn[RN_SLOTS];
#define RELAX_NAME_WIN_MS 90000ull     /* 一场对话足够；每次放宽命中都会刷新那一格 */

static void refresh_rn_slot(int i);      /* 定义在后面（需要 orig_realfullname） */
static void rn_apply_rename(int i);
static void rn_patch_tpl(int i);
static void rn_restore_tpl(int i);
static void rn_restore_rename(int i);
/* 长度合理性检查：il2cpp 字符串的 +0x10 是 int32 长度。
 * 悬空/野指针读到的长度通常离谱，先挡掉，避免后面 memcmp 按巨大长度读越界。 */
#define IL2STR_MAX_LEN 4096
static int il2str_len_ok(void* s) {
    if ((uintptr_t)s < 0x10000) return 0;
    int l = *(int*)((char*)s + OFF_IL2STR_LEN);
    return l >= 0 && l <= IL2STR_MAX_LEN;
}
static __thread int g_rn_busy = 0;       /* 防重入：见 story_real_name 的注释 */

/* 兼容旧代码：最近一次命中放宽的 uid（只用于日志/调试） */
static volatile int g_relax_uid = 0;

static uint64_t rn_now_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000);
}

static int rn_find(int uid) {
    if (uid <= 0) return -1;
    for (int i = 0; i < RN_SLOTS; ++i)
        if (g_rn[i].uid == uid) return i;
    return -1;
}

static void rn_clear_slot(int i, int restore) {
    if (i < 0 || i >= RN_SLOTS) return;
    if (restore) { rn_restore_rename(i); rn_restore_tpl(i); }   /* 别把改过的名字留在游戏里 */
    memset(&g_rn[i], 0, sizeof(g_rn[i]));
}

static int rn_alloc(int uid) {
    for (int i = 0; i < RN_SLOTS; ++i)
        if (g_rn[i].uid == uid) return i;
    for (int i = 0; i < RN_SLOTS; ++i)
        if (g_rn[i].uid == 0) { g_rn[i].uid = uid; return i; }
    /* 满了：踢掉最早到期的 */
    int worst = 0;
    for (int i = 1; i < RN_SLOTS; ++i)
        if (g_rn[i].until < g_rn[worst].until) worst = i;
    rn_clear_slot(worst, 1);
    g_rn[worst].uid = uid;
    return worst;
}

/* 条件节点里记下"这次放宽放过了谁"。条件节点自身的 partnerID 偏移与角色实例不同：
 * ConditionNode(IsJoinedTeam/HavePartner).partnerID = +0x58（见 OFF_PARTNER_ID）。
 *
 * v79：**只登记，不改名**。改名改由「剧情真正播放」驱动（见 story_play_begin/end）——
 * 因为游戏会按几十上百次/秒重判所有开启事件的 Hold 条件，如果在登记时就直接改名，
 * 全城被放宽的 NPC 都会顶着真名，远超出「剧情播放期间」的语义。 */
static void mark_relaxed(void* condNode) {
    if ((uintptr_t)condNode < 0x10000) return;
    int pid = *(int*)((char*)condNode + OFF_PARTNER_ID);
    int neg = (int)*(unsigned char*)((char*)condNode + OFF_PARTNER_NEGATE);
    if (pid <= 0 || neg) return;            /* 只记「要求某人在队」这一侧被放宽的 */
    int i = rn_alloc(pid);
    if (i < 0) return;
    void* g = *(void**)((char*)condNode + OFF_NODE_GRAPH);
    if ((uintptr_t)g > 0x10000) g_rn[i].graph = g;
    g_rn[i].until = rn_now_ms() + RELAX_NAME_WIN_MS;
    g_relax_uid = pid;
}

static void* (*orig_fullname)(void* self) = nullptr;
static void* (*orig_fullname_title)(void* self) = nullptr;
static void* (*orig_realfullname)(void* self) = nullptr;
static int g_rn_log = 0;
/* v77：分类独立额度。v76 的教训：共用 60 条额度被「每帧重判的门对」吃光，
 * 后面真正关心的日志全被吞掉，看起来像"没生效"。 */
static int g_log_rename = 0;
static int g_log_tpl = 0;
static int g_log_ready = 0;
static int g_log_name_sink = 0;   /* 文本层 / 气泡改名 的日志上限 */
static int g_ascii_log = 0;       /* v91-probe：TMP 里出现纯 ASCII 短串的日志上限 */
static int g_port_probe = 0;      /* v92-probe：TMP 里出现 "Port" 字样的日志上限 */


static int str_empty(void* s) {
    return ((uintptr_t)s < 0x10000) || *(int*)((char*)s + OFF_IL2STR_LEN) == 0;
}

/* ---- v90：托管字符串的「文本快照」工具 ----------------------------------
 * 缓存裸指针 = 缓存一个随时会被 GC 回收并被别的字符串复用的地址（见 RN_SLOT 注释）。
 * 统一改成：要留就留文本，要字符串就现造。 */
static void snap_str(void* obj, int off, char* buf, int cap, int* was_null) {
    if (buf && cap > 0) buf[0] = 0;
    if (was_null) *was_null = 1;
    if ((uintptr_t)obj < 0x10000) return;
    void* s = *(void**)((char*)obj + off);
    if (!il2str_len_ok(s)) return;                       /* null 或悬空 → 当作 null */
    int l = *(int*)((char*)s + OFF_IL2STR_LEN);
    if (was_null) *was_null = 0;
    if (l <= 0) return;                                  /* 空串 */
    str_to_buf(s, buf, cap);
}

static void* str_rebuild(const char* txt, int was_null) {
    if (was_null || !A_string_new) return nullptr;
    void* s = A_string_new(txt ? txt : "");
    return ((uintptr_t)s > 0x10000) ? s : nullptr;
}

static int snap_take_u16(void* s, uint16_t* out, int cap) {
    if (!il2str_len_ok(s)) return 0;
    int l = *(int*)((char*)s + OFF_IL2STR_LEN);
    if (l <= 0 || l > cap) return 0;
    memcpy(out, (char*)s + OFF_IL2STR_CHARS, (size_t)l * 2);
    return l;
}

static int snap_match_u16(void* s, const uint16_t* want, int wantlen) {
    if (wantlen <= 0 || !il2str_len_ok(s)) return 0;
    int l = *(int*)((char*)s + OFF_IL2STR_LEN);
    if (l != wantlen) return 0;
    return memcmp((char*)s + OFF_IL2STR_CHARS, want, (size_t)wantlen * 2) == 0;
}

/* ---- v91：写名字前的「类型体检」-----------------------------------------
 * 症状：洛天雄离队后再去找他，名字变成 "OutPort"。
 * 离队会把角色实例销毁/重建，`GameCharacterManager.GetCharacter(uid)` 之后可能
 * 返回**已释放的地址**（被别的对象复用，很可能是剧情图里的节点/端口对象 ——
 * 那些对象身上到处是 "OutPort" / "conditionPort" 这类字符串）。
 * 这时我们去读 c+OFF_GCI_TEMPLATE 得到的是野指针，读 +0x20/+0x28 拿到某个
 * 字符串（正好是端口名），再把它写进 m_surName/m_givenName —— 名字就成了 "OutPort"。
 * 所以写之前先按 il2cpp 类名 + 实例 uid 双重确认，宁可这一次不改名。 */
static int obj_is_class(void* obj, const char* want) {
    if ((uintptr_t)obj < 0x10000) return 0;
    if (!A_obj_get_class || !A_class_get_name) return 1;   /* 拿不到 API 就不拦 */
    void* k = A_obj_get_class(obj);
    if ((uintptr_t)k < 0x10000) return 0;
    const char* n = A_class_get_name(k);
    return (n && strcmp(n, want) == 0);
}

/* ---- v92-probe：定位「端口名被当成名字显示」到底走哪条路 ------------------
 * 判断标准：3~32 个字符、全部是可打印 ASCII、且至少含一个字母。
 * `OutPort`、`conditionPort`、`HeadName` 这类运行时串都符合，中文名一律不符合。 */
static int str_is_ascii_name(void* s) {
    if (!il2str_len_ok(s)) return 0;
    int L = *(int*)((char*)s + OFF_IL2STR_LEN);
    if (L < 3 || L > 32) return 0;
    const unsigned short* p = (const unsigned short*)((char*)s + OFF_IL2STR_CHARS);
    int alpha = 0;
    for (int k = 0; k < L; ++k) {
        unsigned short c = p[k];
        if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z')) { alpha = 1; continue; }
        if (c < 0x20 || c > 0x7E) return 0;
    }
    return alpha;
}

#if WULIN_NAMEPROBE
/* Unity 组件 → GameObject 名（用来判断是哪块 UI 在显示这个串） */
static void comp_go_name(void* comp, char* buf, int cap) {
    if (buf && cap > 0) buf[0] = 0;
    if ((uintptr_t)comp < 0x10000) return;
    typedef void* (*fn_go)(void*);
    void* go = ((fn_go)(g_base + RVA_COMP_GET_GO))(comp);
    if ((uintptr_t)go < 0x10000) return;
    typedef void* (*fn_name)(void*);
    void* nm = ((fn_name)(g_base + RVA_GET_NAME))(go);
    if ((uintptr_t)nm > 0x10000) str_to_buf(nm, buf, cap);
}

static void obj_class_name(void* obj, char* buf, int cap) {
    if (buf && cap > 0) buf[0] = 0;
    if ((uintptr_t)obj < 0x10000 || !A_obj_get_class || !A_class_get_name) return;
    void* k = A_obj_get_class(obj);
    if ((uintptr_t)k < 0x10000) return;
    const char* n = A_class_get_name(k);
    if (n) snprintf(buf, cap, "%s", n);
}
#endif

/* 取「熟悉后姓名」的分层兜底，全部基于传入的角色实例 self（不查管理器、不依赖缓存）：
 *   ① get_RealFullName —— 陌生状态下从模板拼 SurName + GivenName（「姓+名」，不带称号）
 *   ② 模板正式名 CharacterPoolData.name(+0x18)
 *   ③ 陌生名本身（宁可显示 @xxx 也不要空）
 * v71：补了 ② —— 老版本 ① 空就直接退 ③，等于什么都没换。
 * v86：「称号·姓名」已回退，这里恢复成 RealFullName。 */
static void* real_name_of(void* self, const char** how) {
    if ((uintptr_t)self < 0x10000) { if (how) *how = "无实例"; return nullptr; }

    if (orig_realfullname && !g_rn_busy) {
        g_rn_busy = 1;
        void* real = orig_realfullname(self);
        g_rn_busy = 0;
        if (!str_empty(real)) { if (how) *how = "RealFullName"; return real; }
    }
    void* tpl = *(void**)((char*)self + OFF_GCI_TEMPLATE);
    if ((uintptr_t)tpl > 0x10000) {
        void* nm = *(void**)((char*)tpl + OFF_CPD_NAME);
        if (!str_empty(nm)) { if (how) *how = "模板正式名"; return nm; }
        void* s = *(void**)((char*)tpl + OFF_CPD_NAME_STRANGER);
        if (!str_empty(s)) { if (how) *how = "回退陌生名"; return s; }
    }
    if (how) *how = "模板为空";
    return nullptr;
}

/* 刷新第 i 个槽位缓存的陌生名/真名（供 swap_if_stranger 的字符串比对用）
 * v90：只存文本快照。原来存的是陌生名的裸指针，而 rn_apply_rename 随后就把
 * m_givenName 改掉 → 那串没人引用 → GC 回收后比对的就是垃圾。 */
static void refresh_rn_slot(int i) {
    if (i < 0 || i >= RN_SLOTS) return;
    g_rn[i].stranger_u16_len = 0;
    g_rn[i].real_txt[0] = 0;
    int uid = g_rn[i].uid;
    if (uid <= 0) return;

    void* m = char_mgr();
    if ((uintptr_t)m < 0x10000) return;
    typedef void* (*fn_getchar)(void*, int);
    void* c = ((fn_getchar)(g_base + RVA_GET_CHARACTER))(m, uid);
    if ((uintptr_t)c < 0x10000) return;

    void* stranger = *(void**)((char*)c + OFF_GCI_GIVENNAME);      /* 陌生名 = @xxx */
    /* 已经改成真名了就别把真名当陌生名记下来 */
    g_rn[i].stranger_u16_len = snap_take_u16(stranger, g_rn[i].stranger_u16, RN_U16_MAX);
    if (g_rn[i].stranger_u16_len > 0 && g_rn[i].stranger_u16[0] == 0x40) {
        /* 以 @ 开头 = 陌生名，正常 */
    } else if (g_rn[i].stranger_u16_len > 0) {
        g_rn[i].stranger_u16_len = 0;                              /* 不是陌生名，别拿来比对 */
    }

    const char* how = "?";
    void* real = real_name_of(c, &how);
    if (str_empty(real)) return;
    str_to_buf(real, g_rn[i].real_txt, sizeof(g_rn[i].real_txt));
    if (g_log_ready < 20) {
        g_log_ready++;
        LOGI("[name] uid=%d 已就绪：熟悉后姓名=\"%s\"（%s）", uid, g_rn[i].real_txt, how);
    }
}

/* ---- v75：数据层改名 ----------------------------------------------------
 * v74 的否证：TalkMessageNode.GetNpcNameRuntime 与 NpcTalkingBubble.Init 各 0 次调用，
 * 但 get_FullName 被调 30 次且已返回真名 —— 说明显示走的是第三条路。
 * 与其继续猜 sink，直接把角色实例身上的 m_surName(0xA8) / m_givenName(0xB0)
 * 换成真名的姓/名。改成之后 get_IsUsingStangerName 会判为「不是陌生名」，
 * 任何 UI（头顶名、对话框、名册、关系面板…）都只能读到真名。
 * 到期时按记录的原始指针还原；实例指针变了（换档）就不还原，避免写已释放对象。 */
static void rn_apply_rename(int i) {
    if (i < 0 || i >= RN_SLOTS) return;
    int uid = g_rn[i].uid;
    if (uid <= 0) return;

    void* m = char_mgr();
    if ((uintptr_t)m < 0x10000) return;
    typedef void* (*fn_getchar)(void*, int);
    void* c = ((fn_getchar)(g_base + RVA_GET_CHARACTER))(m, uid);
    if ((uintptr_t)c < 0x10000) return;

    /* v91：实例地址可能已被释放/复用（离队、换图都会），先验类名与 uid 再动字段 */
    if (!obj_is_class(c, "GameCharacterInstance") ||
        *(int*)((char*)c + OFF_GCI_TPL_UID) != uid) {
        if (g_log_name_sink < 12) {
            g_log_name_sink++;
            LOGI("[name] uid=%d 实例不可信（类名/uid 不符），跳过改名", uid);
        }
        return;
    }

    void* tpl = *(void**)((char*)c + OFF_GCI_TEMPLATE);
    if ((uintptr_t)tpl < 0x10000) return;
    /* v91：模板同样要验 —— 这里读错就是「名字被写成 OutPort」的直接来源 */
    if (!obj_is_class(tpl, "CharacterPoolData") ||
        *(int*)((char*)tpl + OFF_CPD_UID) != *(int*)((char*)c + OFF_GCI_TPL_UID)) {
        if (g_log_name_sink < 12) {
            g_log_name_sink++;
            LOGI("[name] uid=%d 模板不可信（类名/uid 不符），跳过改名", uid);
        }
        return;
    }

    /* v78 快路径：已经改过名、而且给定的名字确实不再是陌生名（不带 @ 前缀）→ 直接返回。
     * 游戏会按几十上百次/秒的节奏重判 Hold 条件，每次都走完整流程纯属浪费。 */
    if (g_rn[i].renamed && g_rn[i].renamed_inst == c) {
        void* g0 = *(void**)((char*)c + OFF_GCI_GIVENNAME);
        if (il2str_len_ok(g0)) {
            int L0 = *(int*)((char*)g0 + OFF_IL2STR_LEN);
            const unsigned short* p0 = (const unsigned short*)((char*)g0 + OFF_IL2STR_CHARS);
            if (L0 > 0 && p0[0] != 0x40) return;
        }
    }

    void* sur = *(void**)((char*)tpl + OFF_CPD_SURNAME);
    void* giv = *(void**)((char*)tpl + OFF_CPD_GIVENNAME);
    if (str_empty(sur) && str_empty(giv)) {
        /* 模板里姓/名都空 → 用正式全名充当名（SurName 置空串，FullName 拼接不会炸） */
        void* nm = *(void**)((char*)tpl + OFF_CPD_NAME);
        if (str_empty(nm)) return;
        giv = nm;
    }
    if (str_empty(giv)) return;
    /* 模板的 GivenName 若本身还是陌生名（以 @ 开头），不动 */
    if (il2str_len_ok(giv)) {
        int L = *(int*)((char*)giv + OFF_IL2STR_LEN);
        const unsigned short* p = (const unsigned short*)((char*)giv + OFF_IL2STR_CHARS);
        if (L > 0 && p[0] == 0x40) return;
    }

    /* 第一次动这个实例：记下原始值（v90：记文本，不记指针 —— 字段马上就被我们覆盖，
     * 原来那串随即失去引用，GC 一回收缓存的指针就是野的） */
    int was_renamed = (g_rn[i].renamed && g_rn[i].renamed_inst == c);
    if (!was_renamed) {
        g_rn[i].renamed_inst = c;
        snap_str(c, OFF_GCI_SURNAME, g_rn[i].orig_sur_txt, sizeof(g_rn[i].orig_sur_txt),
                 &g_rn[i].orig_sur_null);
        snap_str(c, OFF_GCI_GIVENNAME, g_rn[i].orig_given_txt, sizeof(g_rn[i].orig_given_txt),
                 &g_rn[i].orig_given_null);
        g_rn[i].renamed = 1;
    }

    /* v86 回退：不再拼「称号·姓名」。
     * 原因（实测）：字段里写 "总捕头·洛天雄" 后，画面仍然只显示「洛天雄」——
     * 该 UI 既不走 m_givenName/m_surName，也不走气泡 Init 与 TMP set_text，
     * 说明它读的是剧情节点自己的 NpcName 数据；而 PC 端又会给「知府」这类
     * 没有 GivenName 的角色拼出重复串。留着只有副作用，故回退成「姓+名」（= 真名）。 */
    void* empty = A_string_new ? A_string_new("") : nullptr;
    *(void**)((char*)c + OFF_GCI_SURNAME) = str_empty(sur) ? empty : sur;
    *(void**)((char*)c + OFF_GCI_GIVENNAME) = giv;

    /* v77：只在「首次改名」时记一条 —— 刷屏的门对每帧都会走到这里，
     * 共用计数器会被它吃光（v76 就是这么把后面所有日志吞掉的）。 */
    if (!was_renamed && g_log_rename < 12) {
        g_log_rename++;
        char sb[32], gb2[64];
        str_to_buf(*(void**)((char*)c + OFF_GCI_SURNAME), sb, sizeof(sb));
        str_to_buf(giv, gb2, sizeof(gb2));
        LOGI("[name] uid=%d 已把实例姓名改成 \"%s%s\"（数据层）", uid, sb, gb2);
    }
}

/* ---- v76：按 uid 找角色模板 CharacterPoolData --------------------------
 * 关键认识（用户实测给出）：多人链里洛天雄/裘之墨/凌梦蝶 **没有角色实例**（不在队也不在场景），
 * 所以 GetCharacter(uid) 返回 null，改实例字段全部落空；游戏只能从模板上取名字，
 * 而模板的 NameAsStranger(0x20) 正是「@冷峻严肃的中年人」。
 * 用 Resources.FindObjectsOfTypeAll(Type) 枚举全部模板，按 Uid(0x10) 匹配 —— 不改任何游戏状态。 */
static void* tpl_by_uid(int uid) {
    if (uid <= 0) return nullptr;
    /* v78：类指针缓存。mark_relaxed 会被游戏的重判节奏反复调用（每秒几十上百次），
     * 以前每次都 find_class → 136 个程序集逐个 il2cpp_class_from_name，纯浪费。
     * 而且实测这类模板不是 UnityEngine.Object，Resources.FindObjectsOfTypeAll 找不到它，
     * 所以查不到就记下「不可用」，之后一次指针比较就返回。 */
    static void* k_cpd = nullptr;
    static int k_cpd_state = 0;          /* 0=未查 1=可用 2=不可用 */
    if (k_cpd_state == 2) return nullptr;
    if (k_cpd_state == 0) {
        k_cpd = find_class("GameData", "CharacterPoolData");   /* ⚠️ 是 GameData，不是 WuLin */
        k_cpd_state = k_cpd ? 1 : 2;
    }
    if (!k_cpd) return nullptr;
    void* t = class_type(k_cpd);
    if (!t) return nullptr;
    typedef void* (*fn_all)(void*, void*);
    void* arr = ((fn_all)(g_base + RVA_FIND_OBJECTS_ALL))(t, nullptr);
    if ((uintptr_t)arr < 0x10000) return nullptr;
    int n = *(int*)((char*)arr + 0x18);             /* il2cpp 数组 max_length */
    if (n <= 0 || n > 200000) return nullptr;
    for (int i = 0; i < n; ++i) {
        void* o = *(void**)((char*)arr + OFF_ARRAY_DATA + (size_t)i * 8);
        if ((uintptr_t)o < 0x10000) continue;
        if (*(int*)((char*)o + OFF_CPD_UID) == uid) return o;
    }
    return nullptr;
}

/* 把模板的 NameAsStranger 换成「真姓 + 真名」 */
static void rn_patch_tpl(int i) {
    if (i < 0 || i >= RN_SLOTS) return;
    int uid = g_rn[i].uid;
    if (uid <= 0) return;

    void* tpl = (g_rn[i].tpl_patched) ? g_rn[i].tpl : tpl_by_uid(uid);
    if ((uintptr_t)tpl < 0x10000) return;
    /* v91：模板也要验类名 + uid（tpl_by_uid 走 FindObjectsOfTypeAll，返回值不可全信） */
    if (!obj_is_class(tpl, "CharacterPoolData") || *(int*)((char*)tpl + OFF_CPD_UID) != uid) return;

    if (!g_rn[i].tpl_patched) {
        g_rn[i].tpl = tpl;
        /* v90：模板的 NameAsStranger 马上被我们覆盖 → 同样只留文本快照 */
        snap_str(tpl, OFF_CPD_NAME_STRANGER, g_rn[i].orig_tpl_stranger_txt,
                 sizeof(g_rn[i].orig_tpl_stranger_txt), &g_rn[i].orig_tpl_stranger_null);
        g_rn[i].tpl_patched = 1;
    }

    char sb[64] = {0}, nb[64] = {0}, full[160];
    str_to_buf(*(void**)((char*)tpl + OFF_CPD_SURNAME), sb, sizeof(sb));
    str_to_buf(*(void**)((char*)tpl + OFF_CPD_GIVENNAME), nb, sizeof(nb));
    if (!sb[0] && !nb[0]) str_to_buf(*(void**)((char*)tpl + OFF_CPD_UNAME), nb, sizeof(nb));
    if (!sb[0] && !nb[0]) {
        if (g_log_tpl < 20) { g_log_tpl++; LOGI("[name] uid=%d 模板无姓无名，跳过", uid); }
        return;
    }
    int o = 0;
    for (const char* p = sb; *p && o < (int)sizeof(full) - 1; ++p) full[o++] = *p;
    for (const char* p = nb; *p && o < (int)sizeof(full) - 1; ++p) full[o++] = *p;
    full[o] = 0;

    /* v86 回退：模板侧也恢复成「姓+名」（称号功能已整体回退）。 */
    void* ns = A_string_new ? A_string_new(full) : nullptr;
    if ((uintptr_t)ns < 0x10000) return;
    *(void**)((char*)tpl + OFF_CPD_NAME_STRANGER) = ns;

    if (g_log_tpl < 20) {
        g_log_tpl++;
        LOGI("[name] uid=%d 模板陌生名 \"%s\" -> \"%s\"", uid,
             g_rn[i].orig_tpl_stranger_txt, full);
    }
}

static void rn_restore_tpl(int i) {
    if (i < 0 || i >= RN_SLOTS) return;
    if (!g_rn[i].tpl_patched) return;
    if ((uintptr_t)g_rn[i].tpl > 0x10000)
        *(void**)((char*)g_rn[i].tpl + OFF_CPD_NAME_STRANGER) =
            str_rebuild(g_rn[i].orig_tpl_stranger_txt, g_rn[i].orig_tpl_stranger_null);
    g_rn[i].tpl_patched = 0;
    g_rn[i].tpl = nullptr;
}

/* ---- v77：只读体检（不改任何东西）-------------------------------------
 * 在「分支展开」打补丁的那一刻，把讲解链 5 个角色的模板内容全打出来。
 * 这个时机很早、日志额度还没被刷屏的门对吃掉，所以结论可信：
 *   模板有没有找到 / 姓什么 / 名什么 / 陌生时姓名是什么 / 实例存不存在
 * v76 的教训：find_class 命名空间写错（CharacterPoolData 在 GameData 下），
 * 导致 tpl_by_uid 恒为 null，而日志上限又被 10075 吃光，看起来像"没生效"。 */
static int g_probe_log = 0;
void relax_name_probe(int uid) {
    if (uid <= 0) return;
    if (g_probe_log >= 20) return;
    g_probe_log++;

    void* tpl = tpl_by_uid(uid);
    char sb[48] = {0}, nb[48] = {0}, xb[64] = {0}, ub[64] = {0};
    if ((uintptr_t)tpl > 0x10000) {
        str_to_buf(*(void**)((char*)tpl + OFF_CPD_SURNAME), sb, sizeof(sb));
        str_to_buf(*(void**)((char*)tpl + OFF_CPD_GIVENNAME), nb, sizeof(nb));
        str_to_buf(*(void**)((char*)tpl + OFF_CPD_NAME_STRANGER), xb, sizeof(xb));
        str_to_buf(*(void**)((char*)tpl + OFF_CPD_UNAME), ub, sizeof(ub));
    }
    void* inst = nullptr;
    void* m = char_mgr();
    if ((uintptr_t)m > 0x10000) {
        typedef void* (*fn_getchar)(void*, int);
        inst = ((fn_getchar)(g_base + RVA_GET_CHARACTER))(m, uid);
    }
    LOGI("[probe] uid=%d 模板=%s 实例=%s 姓=\"%s\" 名=\"%s\" 陌生名=\"%s\" UName=\"%s\"",
         uid, ((uintptr_t)tpl > 0x10000) ? "有" : "无",
         ((uintptr_t)inst > 0x10000) ? "有" : "无", sb, nb, xb, ub);
}

/* ---- v79：只在「剧情真正播放」期间改名 ----------------------------------
 * 起止点用 StoryScenarioNode（剧情图里的「播放剧情」节点）：
 *     OnActive()            0x3DA0730  开始播
 *     Continue(string)      0x3DA0798  播完
 * 窗口期间还要求「窗口是从同一张图记下来的」，所以只影响正在播的那条剧情里的角色，
 * 不会把全城被重判放宽的 NPC 都改名。 */
static void*    g_play_graph = nullptr;   /* 正在播放的剧情图 */
static char     g_play_name[96] = {0};    /* 它的图名（给 StoryCloseNode 比对用） */
static uint64_t g_play_until = 0;
static int      g_play_state = 0;
static int      g_play_log = 0;
#define PLAY_HARD_CAP_MS 180000ull        /* 单段 scenario 的上限兜底（防止 Continue 没来） */
/* v80：节点之间的空档实测能到 1.8~19 秒（玩家读字 / 选项 / Hold 等待），
 * 所以 Continue 只**续期**、不缩短窗口。缩到 400ms 会在空档里把名字还原掉，
 * 屏幕上的气泡随即被刷成陌生名，下一段开始再改回来 —— 就是"先闪一下陌生名"。 */
#define PLAY_IDLE_MS     30000ull

static int playing_now(void) { return rn_now_ms() <= g_play_until; }

/* 窗口有效 且 属于当前正在播的图 */
static int rn_live_playing(int uid) {
    if (!playing_now()) return 0;
    int i = rn_find(uid);
    if (i < 0 || rn_now_ms() > g_rn[i].until) return 0;
    if (!g_rn[i].graph || g_rn[i].graph != g_play_graph) return 0;
    return 1;
}

/* 按「播放状态」同步改名：该改的改、该还原的还原 */
static void rn_sync(void) {
    int playing = playing_now();
    for (int i = 0; i < RN_SLOTS; ++i) {
        if (g_rn[i].uid <= 0) continue;
        int want = playing && rn_now_ms() <= g_rn[i].until &&
                   g_rn[i].graph && g_rn[i].graph == g_play_graph;
        if (want) {
            if (g_rn[i].stranger_u16_len <= 0 || !g_rn[i].real_txt[0]) refresh_rn_slot(i);
            rn_apply_rename(i);
            rn_patch_tpl(i);
        } else if (g_rn[i].renamed || g_rn[i].tpl_patched) {
            rn_restore_rename(i);
            rn_restore_tpl(i);
        }
    }
}

/* 播放状态变化检测（挂在很热的 hook 上，250ms 节流 + 只在状态真的变了才做事） */
static uint64_t g_sync_last = 0;
static void rn_maybe_sync(void) {
    if (!F_STORY) return;
    uint64_t now = rn_now_ms();
    if (now - g_sync_last < 250) return;
    g_sync_last = now;
    int playing = playing_now();
    if (playing != g_play_state) { g_play_state = playing; rn_sync(); }
}

void story_play_begin(void* playNode) {
    if (!F_STORY || (uintptr_t)playNode < 0x10000) return;
    void* g = *(void**)((char*)playNode + OFF_NODE_GRAPH);
    if ((uintptr_t)g < 0x10000) return;
    g_play_graph = g;
    {
        typedef void* (*fn_getname)(void*);
        void* s = ((fn_getname)(g_base + RVA_GET_NAME))(g);
        if ((uintptr_t)s > 0x10000) str_to_buf(s, g_play_name, sizeof(g_play_name));
        else g_play_name[0] = 0;
    }
    g_play_until = rn_now_ms() + PLAY_HARD_CAP_MS;
    g_play_state = 1;
    g_sync_last = rn_now_ms();
    if (g_play_log++ < 20) LOGI("[name] 开始播放剧情，名字切换只作用于这张图");
    rn_sync();
}

void story_play_end(void) {
    if (!F_STORY) return;
    if (!playing_now()) return;
    /* v80：只续期、不缩短 —— 缩短会在段间空档里把名字还原掉（见 PLAY_IDLE_MS 注释） */
    g_play_until = rn_now_ms() + PLAY_IDLE_MS;
}

/* v80：剧情**真正收尾**时立刻还原（StoryCloseNode.OnResult）。
 * 没有它只能靠 PLAY_IDLE_MS 的空闲超时兜底，名字会多留几十秒。 */
void story_play_end_now(void) {
    if (!F_STORY) return;
    g_play_until = 0;
    g_play_state = 0;
    rn_sync();
}

/* ---- v77：按 uid 提前登记 ----------------------------------------------
 * 由 flatten.cpp 在给剧情图打补丁时调用（很早）。v79 起这里**只登记不改名**，
 * 改名完全交给播放状态驱动。 */
#define PRE_OPEN_WIN_MS 300000ull
void relax_name_open(int uid, void* graph) {
    if (!F_STORY || uid <= 0) return;
    int i = rn_alloc(uid);
    if (i < 0) return;
    if ((uintptr_t)graph > 0x10000) g_rn[i].graph = graph;
    g_rn[i].until = rn_now_ms() + PRE_OPEN_WIN_MS;
}

static void rn_restore_rename(int i) {
    if (i < 0 || i >= RN_SLOTS) return;
    if (!g_rn[i].renamed || (uintptr_t)g_rn[i].renamed_inst < 0x10000) return;
    int uid = g_rn[i].uid;

    /* 只还原「同一个实例」：换档后旧实例已释放，写它就是 use-after-free */
    void* m = char_mgr();
    if ((uintptr_t)m < 0x10000) return;
    typedef void* (*fn_getchar)(void*, int);
    void* c = ((fn_getchar)(g_base + RVA_GET_CHARACTER))(m, uid);
    if (c != g_rn[i].renamed_inst) { g_rn[i].renamed = 0; return; }
    /* v91：只比指针不够 —— 地址可能已被别的对象复用（离队后常见），再验类名与 uid */
    if (!obj_is_class(c, "GameCharacterInstance") ||
        *(int*)((char*)c + OFF_GCI_TPL_UID) != uid) {
        g_rn[i].renamed = 0;
        return;
    }

    /* v90：按文本快照重建原始串再写回（原来是写缓存的裸指针 → 可能是被复用的垃圾，
     * 表现就是人物名字变成 "OutPort" 这种别的字符串）。 */
    *(void**)((char*)c + OFF_GCI_SURNAME) =
        str_rebuild(g_rn[i].orig_sur_txt, g_rn[i].orig_sur_null);
    *(void**)((char*)c + OFF_GCI_GIVENNAME) =
        str_rebuild(g_rn[i].orig_given_txt, g_rn[i].orig_given_null);
    g_rn[i].renamed = 0;
    if (g_rn_log < 60) { g_rn_log++; LOGI("[name] uid=%d 姓名已还原", uid); }
}

/* 剧情窗口内，被放宽的那个角色返回「称号·姓名」。
 *
 * ⚠️ 必须防重入（v63）：get_RealFullName / get_RealFullNameWithTitle 在「没有用陌生名」时
 * 会**尾调 get_FullNameWithTitle**，而后者被 hook_fullname_title 拦着 —— 于是
 *     hook_fullname_title → story_real_name → get_RealFullNameWithTitle → get_FullNameWithTitle
 *       → hook_fullname_title → … 无限递归，栈溢出直接崩（跟 v62 那个 TMP 崩溃是同一类死法）。
 * 线程局部标志一置，内层立刻返回，原版正常拼名字返回。 */

static void* story_real_name(void* self) {
    if (!F_STORY || (uintptr_t)self < 0x10000) return nullptr;
    int uid = *(int*)((char*)self + OFF_GCI_TPL_UID);
    if (uid <= 0) return nullptr;

    /* v79：只在「剧情真正播放中」且该角色属于正在播的那张图时才替换 */
    if (!rn_live_playing(uid)) return nullptr;

    const char* how = "?";
    void* real = real_name_of(self, &how);
    if (!real) { if (g_rn_log++ < 20) LOGI("[name] uid=%d 无可用真名（%s）", uid, how); return nullptr; }
    return real;
}

static int g_get_probe = 0;      /* v92-probe：getter 返回 ASCII 名的日志上限 */
static int g_fix_log = 0;        /* v94：名字被写成端口名后改回正名的日志上限 */

/* v94：**读侧消毒**。
 * v93 探针查明：实例的 m_givenName 字段确实被写成了 "OutPort"（sur 是空串），
 * 而模板里 姓/名/陌生名 都是对的 —— 也就是「别的东西」把这个字段写坏了，
 * 不是我们写名字那一步（v91 的类型校验一次没触发）。
 * 与其继续找那个写者，直接在 get_FullName / get_FullNameWithTitle 出口处把
 * 「端口名之类的 ASCII 垃圾」换成本该显示的名字：不动任何游戏字段，零风险。
 * 头顶名牌(HeadName) 与 对话/信息面板(Text (TMP)) 都是读这两个 getter，
 * 所以这里一改，两处都正常。 */
static void* rn_fix_ascii_name(void* self) {
    if (!obj_is_class(self, "GameCharacterInstance")) return nullptr;
    const char* how = "?";
    void* fix = real_name_of(self, &how);
    if (!fix || str_is_ascii_name(fix)) return nullptr;
    int uid = *(int*)((char*)self + OFF_GCI_TPL_UID);
    if (g_fix_log < 10) {
        g_fix_log++;
        char fb[64] = {0};
        str_to_buf(fix, fb, sizeof(fb));
        LOGI("[name] uid=%d 名字被写成端口名，按模板改成 \"%s\"（%s）", uid, fb, how);
    }
    return fix;
}

/* 名字 getter 的返回值体检：出现 ASCII 名（OutPort/HeadName 之类）就记一条 */
static void probe_name_result(const char* tag, void* self, void* r) {
#if !WULIN_NAMEPROBE
    (void)tag; (void)self; (void)r;
    return;
#else
    if (!str_is_ascii_name(r)) return;
    if (g_get_probe >= 30) return;
    g_get_probe++;
    char ab[40] = {0}, cls[64] = {0};
    str_to_buf(r, ab, sizeof(ab));
    obj_class_name(self, cls, sizeof(cls));
    LOGI("[probe:get] %s -> ASCII 名 \"%s\" self=%p cls=%s", tag, ab, self, cls);

    /* v93-probe：把「这个实例此刻的姓/名」和「它模板的四个名字字段」都打出来，
     * 一次就能看出是实例字段被写成端口名，还是模板字段被写坏。 */
    if (g_get_probe > 6) return;
    if (!obj_is_class(self, "GameCharacterInstance")) return;
    char sb[64] = {0}, gb[64] = {0};
    void* sur = *(void**)((char*)self + OFF_GCI_SURNAME);
    void* giv = *(void**)((char*)self + OFF_GCI_GIVENNAME);
    str_to_buf(sur, sb, sizeof(sb));
    str_to_buf(giv, gb, sizeof(gb));
    int uid = *(int*)((char*)self + OFF_GCI_TPL_UID);
    void* tpl = *(void**)((char*)self + OFF_GCI_TEMPLATE);
    char tc[64] = {0};
    obj_class_name(tpl, tc, sizeof(tc));
    char un[48] = {0}, st[64] = {0}, ts[48] = {0}, tg[48] = {0};
    if (obj_is_class(tpl, "CharacterPoolData")) {
        str_to_buf(*(void**)((char*)tpl + OFF_CPD_UNAME), un, sizeof(un));
        str_to_buf(*(void**)((char*)tpl + OFF_CPD_NAME_STRANGER), st, sizeof(st));
        str_to_buf(*(void**)((char*)tpl + OFF_CPD_SURNAME), ts, sizeof(ts));
        str_to_buf(*(void**)((char*)tpl + OFF_CPD_GIVENNAME), tg, sizeof(tg));
    }
    LOGI("[probe:det] uid=%d sur=%p\"%s\" giv=%p\"%s\" | tpl=%p(%s) uName=\"%s\" 陌生名=\"%s\" 姓=\"%s\" 名=\"%s\"",
         uid, sur, sb, giv, gb, tpl, tc[0] ? tc : "?", un, st, ts, tg);
#endif
}

static void* hook_fullname(void* self) {
    void* r = story_real_name(self);
    if (r) return r;
    void* v = orig_fullname ? orig_fullname(self) : nullptr;
    probe_name_result("get_FullName", self, v);
    if (str_is_ascii_name(v)) {                    /* v94：读侧消毒 */
        void* fix = rn_fix_ascii_name(self);
        if (fix) return fix;
    }
    return v;
}

static void* hook_fullname_title(void* self) {
    void* r = story_real_name(self);
    if (r) return r;
    void* v = orig_fullname_title ? orig_fullname_title(self) : nullptr;
    probe_name_result("get_FullNameWithTitle", self, v);
    if (str_is_ascii_name(v)) {                    /* v94：读侧消毒 */
        void* fix = rn_fix_ascii_name(self);
        if (fix) return fix;
    }
    return v;
}

/* ---- v79：剧情播放的起止钩子 --------------------------------------------
 * StoryScenarioNode 就是剧情图里的「播放剧情」节点：
 *   OnActive()            0x3DA0730  开始播这段 scenario → 打开「播放中」状态
 *   Continue(string)      0x3DA0798  播完走向下一个节点 → 400ms 后关闭
 * 只有「播放中」且窗口来自同一张图时，才把角色姓名换成熟悉后姓名。 */
static void* (*orig_ssn_onactive)(void* self) = nullptr;
static void      (*orig_ssn_continue)(void* self, void* port) = nullptr;
static int g_play_hook_log = 0;

static void* hook_ssn_onactive(void* self) {
    /* v80：先改名再调原函数 —— 气泡是在 OnActive 里生成的，放到之后就会先渲染一遍陌生名 */
    story_play_begin(self);
    void* r = orig_ssn_onactive ? orig_ssn_onactive(self) : nullptr;
    if (g_play_hook_log++ < 15) {
        char kb[64] = {0};
        void* sk = ((uintptr_t)self > 0x10000) ? *(void**)((char*)self + 0x60) : nullptr;
        if ((uintptr_t)sk > 0x10000) str_to_buf(sk, kb, sizeof(kb));
        LOGI("[name] 播放开始 ScenarioKey=%s", kb);
    }
    return r;
}

static void hook_ssn_continue(void* self, void* port) {
    if (orig_ssn_continue) orig_ssn_continue(self, port);
    story_play_end();
}

/* v80：剧情收尾（StoryCloseNode.OnResult）→ 立刻还原姓名。
 * 只处理「图名与正在播的那张一致」的收尾，别让别的剧情收尾把当前窗口关掉。 */
static void (*orig_scn_onresult)(void* self) = nullptr;
static int g_close_log = 0;

static void hook_scn_onresult(void* self) {
    if (orig_scn_onresult) orig_scn_onresult(self);
    if (!F_STORY || (uintptr_t)self < 0x10000) return;
    char nb[96] = {0};
    void* gn = *(void**)((char*)self + OFF_SCN_GRAPHNAME);
    if ((uintptr_t)gn > 0x10000) str_to_buf(gn, nb, sizeof(nb));
    if (!nb[0] || !g_play_name[0] || strcmp(nb, g_play_name) != 0) return;
    if (g_close_log++ < 15) LOGI("[name] 剧情收尾（%s）→ 还原姓名", nb);
    story_play_end_now();
}

static bool hook_check_joined(void* self, int io, void* line) {
    g_team_cond_hits++;
    flatten_try(self);                      /* v65：顺手把「分支展开」补丁套到这张图上（只做一次） */
    /* v70：被「分支展开」接管的条件恒真。用基类实现 = 丢掉这一个条件，
     * 但 partnerID/Negate 原值仍在，下面 mark_relaxed 才能继续为「姓名替换」开窗口。 */
    if (flatten_cond_always_true(self)) {
        mark_relaxed(self);
        if (base_check_all) return base_check_all(self, io, line);
        return true;
    }
    if (F_STORY && base_check_all && !keep_original_for(self)) {
        mark_relaxed(self);                 /* v61：记下"这次放宽放过了谁"，用于显示真名 */
        return base_check_all(self, io, line);
    }
    return orig_check_joined ? orig_check_joined(self, io, line) : false;
}

static bool hook_check_partner(void* self, int io, void* line) {
    g_team_cond_hits++;
    flatten_try(self);
    if (flatten_cond_always_true(self)) {
        mark_relaxed(self);
        if (base_check_all) return base_check_all(self, io, line);
        return true;
    }
    if (F_STORY && base_check_all && !keep_original_for(self)) {
        mark_relaxed(self);
        return base_check_all(self, io, line);
    }
    return orig_check_partner ? orig_check_partner(self, io, line) : false;
}

/* ---- 伙伴邀请/离队：离队容错（v50） --------------------------------------
 *
 * 放松「某角色在队」的判定之后，有些图的对话会在角色**不在队**时也播出来。
 * 对话紧接着的 ChangePartnerNode(remove) 要把他踢出队伍 —— 人本来就不在，
 * 动作落空，后面那对「他在队 / 他不在队」的流程判断就可能两边都进不去，
 * 剧情从此不动（EJ-004「大梁捞朱」实测：洛天雄不在队，对话完就卡住）。
 *
 * 对策：remove 一个不在队的角色时，当作已经成功，直接不调原版。
 * 反汇编确认 ChangePartnerNode.OnResult(0x3DA7878) 本身是空壳 ——
 * 只是「取静态委托 OnResultAction，非空就尾调」；跳过它 = 什么都不做，
 * 正是我们要的「已满足」语义，而且不会误伤真正在队的离队流程。
 *
 * 队伍名单访问链（反汇编 PlayerTeamManager.get_TeamSize 0x22B2354）：
 *   PlayerTeamManager(+0x60) -> [+0x20] SaveObjectPlayerTeam -> [+0x90] List<int>
 *   List<int>: [+0x10] _items, [+0x18] _size
 */
static void (*orig_change_partner)(void* self) = nullptr;
static int g_cp_log = 0;

static void* g_ptm_q = nullptr;
static uint64_t g_ptm_qt = 0;

static void* ptm_quick(void) {
    uint64_t now = rule_now_ms();
    if ((uintptr_t)g_ptm_q > 0x10000 && now - g_ptm_qt < CHAR_MGR_TTL_MS) return g_ptm_q;
    void* k = find_class("WuLin", "PlayerTeamManager");
    void* inst = k ? find_object_of_type(k) : nullptr;
    g_ptm_q = ((uintptr_t)inst > 0x10000) ? inst : nullptr;
    g_ptm_qt = now;
    return g_ptm_q;
}

/* 把队伍里的角色 id 拷进 out，返回人数；拿不到返回 -1（此时一律走原版，不做干预）
 *
 * ⚠️ v70 修正：`_items` 指向的是 il2cpp **数组对象**，元素从 +0x20 开始
 * （klass 0x00 | monitor 0x08 | bounds 0x10 | max_length 0x18 | 元素 0x20）。
 * 老代码直接 `int* arr = (int*)items` 从对象头读，读到的是 bounds 指针的高低半字
 * 和 max_length，只有从 [3] 起才勉强对上元素 —— 于是 team_has() 一直在和垃圾比对，
 * 几乎总返回「不在队」。之前「离队容错」看似正常，是因为那两次角色本来就不在队。
 */
static int team_ids(int* out, int cap) {
    void* m = ptm_quick();
    if ((uintptr_t)m < 0x10000) return -1;
    void* dc = *(void**)((char*)m + OFF_PTM_CONTAINER);
    if ((uintptr_t)dc < 0x10000) return -1;
    void* so = *(void**)((char*)dc + OFF_SOC_DATA);
    if ((uintptr_t)so < 0x10000) return -1;
    void* lst = *(void**)((char*)so + OFF_SOPT_LIST);
    if ((uintptr_t)lst < 0x10000) return -1;
    int n = *(int*)((char*)lst + OFF_LIST_SIZE);
    void* items = *(void**)((char*)lst + OFF_LIST_ITEMS);
    if ((uintptr_t)items < 0x10000 || n < 0 || n > 32) return -1;
    int* arr = (int*)((char*)items + OFF_ARRAY_DATA);
    int k = 0;
    for (int i = 0; i < n && k < cap; ++i) out[k++] = arr[i];
    return n;
}

/* 1=在队 0=不在队 -1=查不到 */
static int team_has(int uid) {
    if (uid <= 0) return -1;
    int ids[16];
    int n = team_ids(ids, 16);
    if (n < 0) return -1;
    for (int i = 0; i < n; ++i) if (ids[i] == uid) return 1;
    return 0;
}

static void hook_change_partner(void* self) {
    if (!F_STORY || (uintptr_t)self < 0x10000) {
        if (orig_change_partner) orig_change_partner(self);
        return;
    }
    int st = *(int*)((char*)self + OFF_CP_STATE);        /* add=0 / remove=1 */
    if (st == 1) {
        int mode = *(int*)((char*)self + OFF_CP_MODE);   /* ID=0 / 队中位置=1 */
        int pid = 0;
        int skip = 0;                                     /* 1 = 队伍里根本没有这个角色 */
        if (mode == 0) {
            pid = *(int*)((char*)self + OFF_CP_PARTNER_ID);
            if (pid <= 0) skip = 1;                       /* 没指定角色，没什么可离的 */
        } else {
            /* 按队中位置：Index 越界 = 队伍没那么多人（释法寺 School-2020-1 就是 Index=5，
             * 玩家队伍只有 2 人）。v53 之前这里 pid 留 0，补丁不生效就退回原版，
             * 原版去踢一个不存在的队员 → 场景卡死。v54 起按"已成功"跳过。 */
            int idx = *(int*)((char*)self + OFF_CP_INDEX);
            int ids[16];
            int n = team_ids(ids, 16);
            if (n >= 0 && idx >= 0 && idx < n) pid = ids[idx];
            else skip = 1;
        }
        if (skip) {
            if (g_cp_log++ < 40)
                LOGI("[partner] remove(mode=%d) 队伍里没有这个角色 → 视为成功，跳过原版", mode);
            return;
        }
        int has = team_has(pid);
        if (has == 0) {
            if (g_cp_log++ < 40)
                LOGI("[partner] remove uid=%d 不在队 → 视为成功，跳过原版", pid);
            return;                                      /* 不调原版 = 什么都不做 = 已满足 */
        }
        if (g_cp_log++ < 40)
            LOGI("[partner] remove uid=%d has=%d → 走原版", pid, has);
    }
    if (orig_change_partner) orig_change_partner(self);
}

/* ---- 主界面「鸣谢」按钮改字（v59/v60，v63 收口） -------------------------
 * 历史：v58 hook UGUI Text.set_text —— 一次没命中；v59 加 get_text —— 还是没命中；
 * v60 才确认那按钮是 TMPro.TMP_Text。
 *
 * v63 实测日志：`[ui] 鸣谢按钮文字 -> mod（TMP set_text）` —— 干活的一直是 TMP set_text。
 *
 * 于是把两条 UGUI Text 的 hook 全撤了，原因不只是「没用」：
 *   UnityEngine.UI.Text.get_text  0x4B9A414   ← 只有 8 字节
 *   UnityEngine.UI.Text.set_text  0x4B9A41C   ← 紧挨着，相距 8 字节
 * 补丁固定 16 字节，装 get 时把 set_text 开头写成自己的字面量（目标地址），
 * 再装 set 又把那个字面量覆盖成指令字节 → 一旦有人调 get_text 就 br 到
 * 0xD63F020058000070，必崩。游戏全程只用 TMP，所以这颗雷一直没响。
 * 留着没有任何收益，只有风险，删掉。 */
static void* g_mod_str = nullptr;
static int g_thx_log = 0;

/* ---- v62：文本层替换「陌生名 → 熟悉后姓名」 ----------------------------
 * 不去猜对话框到底调 FullName 还是 GivenName：名字最终一定进 TMP 的 set_text，
 * 所以在 set_text 入口按字符串比对替换，最稳。
 * 只替换「被放宽的那个角色」的陌生名（m_givenName，形如 @冷峻严肃的中年人），
 * 其它文本一律原样放行。 */

/* v90：原来的 il2str_eq(a,b) 需要两个「活着的」托管字符串；现在陌生名只留文本快照，
 * 比对统一走 snap_match_u16()。 */

static void* swap_if_stranger(void* str) {
    if (!F_STORY || (uintptr_t)str < 0x10000) return str;
    if (!il2str_len_ok(str)) return str;

    /* v72：遍历所有「最近放宽命中」的槽位，命中谁的陌生名就换成谁的真名。
     * 老版本只比对最近那一个 uid —— 被刷屏的门对钉住后就再也换不了别人。 */
    uint64_t now = rn_now_ms();
    /* v79：文本层也一样，只在剧情播放期间、且只认当前图里的角色 */
    if (rn_now_ms() > g_play_until) return str;
    for (int i = 0; i < RN_SLOTS; ++i) {
        if (g_rn[i].uid <= 0) continue;
        if (g_rn[i].graph != g_play_graph) continue;
        if (now > g_rn[i].until) continue;
        /* 放宽命中那一刻角色实例可能还没创建（尤其读档后刚进场景），缓存是空的。
         * 按 500ms 节流补取一次，避免整段对话都换不到名字。 */
        if (g_rn[i].stranger_u16_len <= 0 || !g_rn[i].real_txt[0]) {
            static uint64_t last_try = 0;
            if (now - last_try < 500) continue;
            last_try = now;
            refresh_rn_slot(i);
            if (g_rn[i].stranger_u16_len <= 0 || !g_rn[i].real_txt[0]) continue;
        }
        if (!snap_match_u16(str, g_rn[i].stranger_u16, g_rn[i].stranger_u16_len)) continue;
        if (g_rn_log++ < 8) LOGI("[name] 文本层替换：陌生名 -> 熟悉后姓名 (uid=%d)", g_rn[i].uid);
        /* v90：现造一个新字符串交回去 —— 缓存里只留文本，不再返回可能已被回收的指针 */
        void* fresh = str_rebuild(g_rn[i].real_txt, 0);
        return ((uintptr_t)fresh > 0x10000) ? fresh : str;
    }
    return str;
}

/* 清掉「放宽命中窗口」的全部状态。
 * 读档、换档都要调 —— stranger/real 是裸指针，
 * 指向旧存档里角色实例持有的字符串，实例一销毁就是悬空指针。 */
static void clear_relax_name_state(void) {
    g_relax_uid = 0;
    g_play_graph = nullptr;
    g_play_until = 0;
    g_play_state = 0;
    /* v90：槽位里只缓存文本，不会有悬空指针了；这里仍然先还原再清空 */
    for (int i = 0; i < RN_SLOTS; ++i) rn_clear_slot(i, 1);
}

static int is_credits_text(void* s) {
    if ((uintptr_t)s < 0x10000) return 0;
    if (*(int*)((char*)s + OFF_IL2STR_LEN) != 2) return 0;
    const unsigned short* p = (const unsigned short*)((char*)s + OFF_IL2STR_CHARS);
    if (p[0] == 0x9CF4 && p[1] == 0x8B1D) return 1;   /* 鳴謝 */
    if (p[0] == 0x9E23 && p[1] == 0x8C22) return 1;   /* 鸣谢 */
    return 0;
}

/* 每次都新建一个 "mod" 字符串。
 * il2cpp 的 string 是托管对象，只被我们静态变量指着的实例 GC 看不到（我们的 .data 不在
 * GC 根集合里），跨一次 GC 就是悬空指针。这里干脆不缓存 —— 调用频率极低（只在命中
 * 「鸣谢」时），新建一个 3 字符的字符串的代价可以忽略，换来的是永不悬空。 */
static void* mod_string(void) {
    if (A_string_new) g_mod_str = A_string_new("mod");
    return g_mod_str;
}

/* v60：主界面那个按钮实测不是 UGUI Text（v58 拦 set_text、v59 拦 get_text 全都没命中），
 * 是 TMPro.TMP_Text（TextMeshProUGUI 没 override，直接用基类的 set_text）。
 * m_text 同样在 +0xE0，所以判定逻辑完全复用。 */
static void (*orig_tmp_set)(void* self, void* str) = nullptr;
static void* (*orig_tmp_get)(void* self) = nullptr;

static void* hook_tmp_get(void* self) {
    void* s = orig_tmp_get ? orig_tmp_get(self) : nullptr;
    if (is_credits_text(s)) {
        void* m = mod_string();
        if ((uintptr_t)m > 0x10000) {
            if (g_thx_log++ < 3) LOGI("[ui] 鸣谢按钮显示 -> mod（TMP get_text）");
            return m;
        }
    }
    return s;
}

static void hook_tmp_set(void* self, void* str) {
#if WULIN_NAMEPROBE
    /* v92-probe：TMP 里出现纯 ASCII 短串就记一条（含 GameObject 名，方便认是哪块 UI）；
     * 带 "Port" 的单独记、不受普通 ASCII 的额度限制 —— 那是名字变 OutPort 的直接线索。 */
    if ((uintptr_t)str > 0x10000 && il2str_len_ok(str)) {
        int L = *(int*)((char*)str + OFF_IL2STR_LEN);
        if (L >= 3 && L <= 32) {
            const unsigned short* p = (const unsigned short*)((char*)str + OFF_IL2STR_CHARS);
            char ab[40];
            int n = (L < (int)sizeof(ab) - 1) ? L : (int)sizeof(ab) - 1;
            int ascii = 1;
            for (int k = 0; k < n; ++k) {
                unsigned short c = p[k];
                ab[k] = (char)((c < 0x80) ? c : '?');
                if (c < 0x20 || c > 0x7E) ascii = 0;
            }
            ab[n] = 0;
            int has_port = (strstr(ab, "Port") != nullptr);
            if (has_port && g_port_probe < 10) {
                g_port_probe++;
                char go[80] = {0};
                comp_go_name(self, go, sizeof(go));
                LOGI("[probe:port] TMP 文本=\"%s\" tmp=%p go=\"%s\"", ab, self, go);
            } else if (ascii && g_ascii_log < 30) {
                g_ascii_log++;
                char go[80] = {0};
                comp_go_name(self, go, sizeof(go));
                LOGI("[probe:tmp] ASCII 文本 \"%s\" tmp=%p go=\"%s\"", ab, self, go);
            }
        }
    }
#endif
    if (orig_tmp_set && is_credits_text(str)) {
        void* m = mod_string();
        if ((uintptr_t)m > 0x10000) {
            if (g_thx_log++ < 3) LOGI("[ui] 鸣谢按钮文字 -> mod（TMP set_text）");
            orig_tmp_set(self, m);
            return;
        }
    }
    if (orig_tmp_set) orig_tmp_set(self, swap_if_stranger(str));
}

/* ---- v73：在气泡入口直接改名字 ------------------------------------------
 * v72 的日志给出了否证：
 *     [name] uid=10084 显示 "称号·姓名"（称号·姓名 / 模板正式名）   ← getter 已经返回真名
 *     文本层替换: 0                                              ← 文本比对一次没命中
 * 但画面上仍是陌生名 ⇒ 对话框的名字既不是 get_FullName 的返回值，也不是 m_givenName 那段文本。
 * 它走的是 NpcTalkingBubble：Init(adaptor, content) 创建气泡时自己决定 nameText(+0x48) 的内容。
 * 所以这里在 Init 之后直接把 nameText 改成「熟悉后姓名」—— 唯一确定能显示出来的地方。 */
static void (*orig_bubble_init)(void* self, void* adaptor, void* content) = nullptr;
static int g_bn_log = 0;
static int g_bn_probe = 0;     /* v91-probe：气泡名日志上限 */

static int rn_live(int uid) {
    int i = rn_find(uid);
    return (i >= 0 && rn_now_ms() <= g_rn[i].until) ? 1 : 0;
}

static void hook_bubble_init(void* self, void* adaptor, void* content) {
    if (orig_bubble_init) orig_bubble_init(self, adaptor, content);

#if WULIN_NAMEPROBE
    /* v91-probe：把气泡「游戏自己写进去的名字」打出来 —— 名字位置出现端口名
     * （OutPort 之类）时，这条能直接定位是气泡在显示它。 */
    if (g_bn_probe < 20 && (uintptr_t)self > 0x10000 && orig_tmp_get) {
        void* nt = *(void**)((char*)self + OFF_BUBBLE_NAMETEXT);
        if ((uintptr_t)nt > 0x10000) {
            void* t = orig_tmp_get(nt);
            if ((uintptr_t)t > 0x10000) {
                char tb[64];
                str_to_buf(t, tb, sizeof(tb));
                if (tb[0]) { g_bn_probe++; LOGI("[probe:bubble] 气泡名=\"%s\"", tb); }
            }
        }
    }
#endif

    if (!F_STORY) return;
    if ((uintptr_t)self < 0x10000 || (uintptr_t)content < 0x10000) return;

    void* ch = *(void**)((char*)content + OFF_TC_CHARACTER);
    if ((uintptr_t)ch < 0x10000) return;
    int uid = *(int*)((char*)ch + OFF_GCI_TPL_UID);
    if (!rn_live(uid)) return;

    void* nm = real_name_of(ch, nullptr);
    if (!nm) return;

    void* nameText = *(void**)((char*)self + OFF_BUBBLE_NAMETEXT);
    if ((uintptr_t)nameText < 0x10000) return;
    if (!orig_tmp_set) return;

    orig_tmp_set(nameText, nm);      /* 直接调原版 set_text，绕开我们自己的 hook */

    if (g_bn_log++ < 40) {
        char rb[64];
        str_to_buf(nm, rb, sizeof(rb));
        if (g_log_name_sink++ < 8) LOGI("[name] 气泡改名 uid=%d → \"%s\"", uid, rb);
    }
}

/* ---- v74：钩「对话节点」的说话人姓名产出函数 -----------------------------
 * v72 的否证：getter 已返回真名、文本比对零命中，画面却仍是陌生名。
 * 于是往回一层找到真正产出「对话框中说话人姓名」的函数：
 *   TalkMessageNode.GetNpcNameRuntime()     0x3D865D0
 *   SlgTalkMessageNode.GetNpcNameRuntime()  0x26B117C   （SLG 版，同一语义）
 * 两个都挂，谁生效看安装日志。Postfix 里按 NpcID 查窗口，命中就换成熟悉后姓名。 */
static void* (*orig_tmn_name)(void* self) = nullptr;
static void* (*orig_slgtmn_name)(void* self) = nullptr;
static int g_nn_log = 0;

static void* npc_name_common(void* self, void* (*orig)(void*), const char* tag) {
    void* r = orig ? orig(self) : nullptr;
    probe_name_result(tag, self, r);          /* v92-probe：ASCII 名立刻记一条 */
    if (!F_STORY || (uintptr_t)self < 0x10000) return r;

    int uid = *(int*)((char*)self + OFF_TMN_NPCID);
    if (g_nn_log < 40) {
        g_nn_log++;
        char ob[64] = {0};
        if ((uintptr_t)r > 0x10000) str_to_buf(r, ob, sizeof(ob));
        LOGI("[name] %s uid=%d 原本=\"%s\"", tag, uid, ob);
    }

    int i = rn_find(uid);
    if (i < 0 || rn_now_ms() > g_rn[i].until) return r;
    if (!g_rn[i].real_txt[0]) refresh_rn_slot(i);
    if (!g_rn[i].real_txt[0]) return r;

    if (g_nn_log < 60) {
        g_nn_log++;
        LOGI("[name] %s uid=%d 替换 -> \"%s\"", tag, uid, g_rn[i].real_txt);
    }
    /* v90：现造字符串，别把缓存里的裸指针交出去（可能已被 GC 回收并复用） */
    void* fresh = str_rebuild(g_rn[i].real_txt, 0);
    return ((uintptr_t)fresh > 0x10000) ? fresh : r;
}

static void* hook_tmn_name(void* self) { return npc_name_common(self, orig_tmn_name, "对话名"); }
static void* hook_slgtmn_name(void* self) { return npc_name_common(self, orig_slgtmn_name, "SLG对话名"); }

/* ---- 诊断（v51）：进场区域条件 ------------------------------------------
 * EnterAreaCondition 的判定（反汇编 0x268DAD0）：
 *     ok = (self.TotalMapID == <某单例的 +0x94>) ;
 *     return (self.Negate != ok) ;
 * 但那个"当前地图 id"取自哪个单例、什么字段，静态追不下去。
 * 这里把槽位值和 PlayerTeamManager.CurMapId 一起打出来，就能一眼看出差在哪。 */
static bool (*orig_enter_area)(void* self) = nullptr;
static int g_ea_log = 0;

static bool hook_enter_area(void* self) {    bool r = orig_enter_area ? orig_enter_area(self) : false;
    flatten_try(self);                      /* v65：进场区域条件是最早能拿到图对象的点，先套补丁 */
    if (!F_STORY || (uintptr_t)self < 0x10000) return r;
    if (g_ea_log >= 200) return r;

    int total = (int)*(unsigned char*)((char*)self + OFF_EA_TOTALMAP);
    if (total != 1) return r;                       /* 只关心"全图"式条件 */
    int mapid = *(int*)((char*)self + OFF_EA_TOTALMAPID);
    if (mapid != 20200) return r;                   /* v53：只打大梁城，避免噪声吃满配额 */
    int neg = (int)*(unsigned char*)((char*)self + OFF_EA_NEGATE);

    int cur = -1;
    void* m = ptm_quick();
    if ((uintptr_t)m > 0x10000) {
        typedef int (*fn_curmap)(void*);
        cur = ((fn_curmap)(g_base + RVA_PTM_CUR_MAPID))(m);
    }
    char gb[128];
    void* g = *(void**)((char*)self + OFF_NODE_GRAPH);
    const char* gn = ((uintptr_t)g > 0x10000) ? obj_name(g, gb, sizeof(gb)) : "?";

    g_ea_log++;
    LOGI("[area] %s  TotalMapID=%d Negate=%d -> %d   (CurMapId=%d)",
         gn, mapid, neg, (int)r, cur);
    return r;
}

/* ---- ContinueArrange 递归看门狗 ------------------------------------------
 * 放宽「队友在队 / 有伙伴」之后，某些剧情图里的 Hold 节点会立刻满足：
 *   StoryManager.ContinueArrange <-> HoldNode.OnActive 在同一帧里互相递归，
 * 几千层后原生栈溢出，进程直接消失（PC 端实测：Windows 事件日志
 * Application Error id=1000，错误模块 GameAssembly.dll，异常代码 0xc00000fd）。
 *
 * 早先那两个 hook 里的「同一对象每秒 20 次」闸门不够用 —— 它按 self 分槽，
 * 递归时若每层命中的是不同条件实例，各自都能放行 20 次，总深度照样爆栈。
 * 这里改在递归的必经点 ContinueArrange 上按「真实调用栈深度」限深：
 * 超过 16 层就跳过这一层推进。深度用 __thread，下一帧从 0 开始，
 * 剧情继续往下走，不会把进度永久堵死。
 * ContinueArrange 栈帧 0xd0 字节，16 层约 3.3 KB，离 8 MB 的线程栈很远。 */
/* v55 修正：阈值从 16 抬到 256（与 PC 端一致）。
 *
 * 原注释的换算是「ContinueArrange 栈帧 0xd0 字节，16 层约 3.3 KB，离 8 MB 线程栈很远」——
 * 也就是说 16 这个数字是为了「绝对安全」随手取的小值，不是按真实剧情深度定的。
 * 但 ContinueArrange 是链式递归推进：一个节点推下一个，链长 = 剧情节点数。
 * 释法寺 Spe-2020 有 315 个节点，实测走到 16 层就被这里砍断 → 剧情停在半路、UI 不出。
 * 8 MB / 0xd0 ≈ 四万层，256 层只占 52 KB，与 PC 端（CA_MAX_DEPTH=256）对齐即可。 */
#define CA_MAX_DEPTH 256
static void (*orig_continue_arrange)(void* self, void* node, void* port) = nullptr;
static __thread int g_ca_depth = 0;
static int g_ca_warn = 0;
static int g_ca_max_seen = 0;

static const char* node_class_name(void* node) {
    if ((uintptr_t)node < 0x10000 || !A_obj_get_class || !A_class_get_name) return "?";
    void* k = A_obj_get_class(node);
    if ((uintptr_t)k < 0x10000) return "?";
    const char* n = A_class_get_name(k);
    return n ? n : "?";
}

static void hook_continue_arrange(void* self, void* node, void* port) {
    if (!F_STORY || !orig_continue_arrange) {
        if (orig_continue_arrange) orig_continue_arrange(self, node, port);
        return;
    }
    if (g_ca_depth >= CA_MAX_DEPTH) {
        if (g_ca_warn < 5) {
            g_ca_warn++;
            char gb[128];
            void* g = node ? *(void**)((char*)node + OFF_NODE_GRAPH) : nullptr;
            const char* gn = ((uintptr_t)g > 0x10000) ? obj_name(g, gb, sizeof(gb)) : "?";
            LOGI("剧情: ContinueArrange 递归 %d 层触顶，阻断这一层  图=%s 节点=%s",
                 g_ca_depth, gn, node_class_name(node));
        }
        return;
    }
    if (g_ca_depth > g_ca_max_seen) {
        g_ca_max_seen = g_ca_depth;
        if (g_ca_max_seen == 32 || g_ca_max_seen == 64 || g_ca_max_seen == 128)
            LOGI("剧情: ContinueArrange 深度创新高 %d 层（供判定阈值是否合适）", g_ca_max_seen);
    }
    g_ca_depth++;
    orig_continue_arrange(self, node, port);
    g_ca_depth--;
}

/* ---- Hold 只放行一次（治「剧情反复触发」） ------------------------------
 * HoldNode.checkSucessTimes(+0x70) 是游戏自己的「这个 Hold 已经成功过」标志：
 * addTimes() 置 1、clearTimes() 置 0。但 CheckCondition()(0x3D9DEFC) 根本不读它 ——
 * 反汇编确认它只做条件端口 GetConnections → 逐个 CheckAllConnectNode。
 * 原版靠「条件为假」实现一次性；我们把条件放宽（走基类）之后，
 * times 早已被置 1，结果却照样 true，剧情于是反复触发。
 *
 * PC 端实测（2026-10-20 BepInEx 日志）：
 *   Hold "流程判断" times=0 result=true      ← 第一次，该放行
 *   Hold "流程判断" times=1 result=true      ← 已经成功过，还在放行 ← 就是这里
 *   Hold "流程判断" times=1 result=false     ← 原版本来就会挡住的
 * 且 frameHits 恒为 1，说明不是同帧空转（那条路径由上面的 ContinueArrange 看门狗管）。
 *
 * 对策：times 非 0 的 Hold，若这次仍被判为「可继续」，就压回 false。
 * 每个 Hold 只可能因为放宽而通过一次；游戏若用 clearTimes() 归零，之后还能再过一次，
 * 语义与原版一致。 */
static bool (*orig_hold_check)(void* self) = nullptr;
static int g_hold_once_warn = 0;

static const char* obj_name(void* o, char* buf, int cap) {
    buf[0] = 0;
    if ((uintptr_t)o < 0x10000) return "?";
    typedef void* (*fn_getname)(void*);
    void* s = ((fn_getname)(g_base + RVA_GET_NAME))(o);
    if ((uintptr_t)s < 0x10000) return "?";
    return str_to_buf(s, buf, cap);
}

/* 非拦截名单：名单内的剧情图，不做放宽、也不做 times 压制 —— 完全按原版判断。
 * 名单外：正常放宽（只放宽「角色在队」这一条），并对 times 非 0 的 Hold 做压制兜底。
 *
 * 留空 = 全部按「放宽 + 兜底压制」跑。
 * 已知候选（PC 端全量对比得出，存档里确实因为「角色在队」才通过的图）：
 *   R10049-0 / R10112-2 / R10136-5 / R20004-4 / Role3-1 / R10036-3 / R10060-2-1..6
 * 条目末尾带 * 时按前缀匹配（例如 "R10060-2-*"）。 */
static const char* k_story_blacklist[] = {
    "",
};
static const int k_story_blacklist_n =
    (int)(sizeof(k_story_blacklist) / sizeof(k_story_blacklist[0]));

static int story_blacklisted(const char* g) {
    if (!g || !g[0] || g[0] == '?') return 0;
    for (int i = 0; i < k_story_blacklist_n; ++i) {
        const char* e = k_story_blacklist[i];
        size_t L = strlen(e);
        if (L == 0) continue;
        if (e[L - 1] == '*') {
            if (strncmp(g, e, L - 1) == 0) return 1;
        } else if (strcmp(g, e) == 0) {
            return 1;
        }
    }
    return 0;
}

static bool hold_check_core(void* self) {
    rn_maybe_sync();   /* v79：检测「剧情播放中」状态变化，过渡时统一改名/还原 */
    /* v65：HoldNode 就是「流程判断」，这是最靠近执行点的地方 —— 在判定前把展开补丁套上，
     * 保证引擎读 OutPort 时用的已经是改过的连接。 */
    flatten_try(self);
    /* 判据不再看"图名像不像角色剧情"，而是看**这次判定到底有没有碰队友条件**：
     *   - 没碰（计数没变）→ 这个 Hold 与放宽无关，一行都不碰；
     *   - 碰过 + times!=0 + 这次仍然通过 → 是放宽把"一次性"变成了"可重复"→ 压回 false。 */
    int before = g_team_cond_hits;
    bool r = orig_hold_check ? orig_hold_check(self) : false;
    if (!F_STORY || !r) return r;
    if (g_team_cond_hits == before) return r;      /* 本次判定没碰队友条件：不干预 */
    if ((uintptr_t)self < 0x10000) return r;

    int times = *(int*)((char*)self + OFF_HOLD_TIMES);
    if (times == 0) return true;          /* 还没成功过：这次正常放行 */

    /* 三条例外（交给原版语义，不做压制）：
     *   - R{角色id}- 图：条件已由 keep_original_for 精确判定，再压制会让「入队后」也只剩一次；
     *   - 固定名单（主线/师父线/点名压制的图），同样按原版；
     *   - 手动名单。 */
    char gb[128];
    void* g = *(void**)((char*)self + OFF_NODE_GRAPH);
    const char* gn = ((uintptr_t)g > 0x10000) ? obj_name(g, gb, sizeof(gb)) : "?";
    /* v108：去掉「R{角色id}- 整族豁免」——苗彩蝶(10005) 的 R10005-* 就是因此循环触发。 */
    for (size_t i = 0; i < sizeof(k_fixed_original) / sizeof(k_fixed_original[0]); ++i)
        if (strcmp(gn, k_fixed_original[i]) == 0) return true;
    if (story_blacklisted(gn)) return true;

    /* 其余：队友条件被放宽后，这个 Hold 已经通过过还要再过 → 压回 false。 */
    if (g_hold_once_warn < 8) {
        g_hold_once_warn++;
        char nb[128];
        LOGI("[once] 剧情图 %s 的 Hold \"%s\" times=%d 已通过过，压制本次（放宽引起的重复触发）",
             gn, obj_name(self, nb, sizeof(nb)), times);
    }
    return false;
}

/* v87：与 PC 的 Patch_FlattenGate 对齐 —— 被「分支展开」接管的门整门判真。
 *
 * 为什么要在门这一层补一刀：手机原来只在 IsJoinedTeamCondition / HavePartnerCondition
 * 的 CheckAllConnectNode 里绕过条件，门的条件若不是这两类就接管不到 —— 于是
 * 「出口已经改指 G1、门却按原版判假」，队里没人时在队侧内容根本不播，看起来像没展开。
 * PC 从 1.10.0 起就是在 HoldNode.CheckCondition 上整门判真，与条件类型无关。
 *
 * 优先级：**高于**上面「Hold 只放行一次」的压制。PC 端两个 Postfix 的生效顺序也是
 * Patch_FlattenGate 盖住 HoldOnceGuard（注册顺序决定），这里用「先核心、后强制」显式对齐。
 * 另外 flatten_gate_forced 故意不看 F_FLAT —— 线已改指，门必须继续放行，否则卡死。 */
static bool hook_hold_check(void* self) {
    bool r = hold_check_core(self);
    if (!r && flatten_gate_forced(self)) return true;
    return r;
}

/* ---- AssetBundle CRC 放行 ----
 * mod 的 *.bundle 被本工程改写过（数据移植 / UnityPy 重存），
 * catalog 里记录的 CRC 与实际文件对不上时 Unity 直接拒载：
 *   "CRC Mismatch. Provided xxxxxxxx, calculated xxxxxxxx from data.
 *    Will not load AssetBundle 'xxx.bundle'"
 * 这里把 AssetBundleRequestOptions.get_Crc() 恒返回 0（= 不校验），
 * 顺手把实例字段 m_Crc(+0x18) 清零，防止有人直接读字段。 */
static unsigned int (*orig_get_crc)(void* self) = nullptr;

static unsigned int hook_get_crc(void* self) {
    unsigned int v = orig_get_crc ? orig_get_crc(self) : 0;
    if (v) {
        LOGI("[mod] AssetBundle CRC %08x 放行（不校验）", v);
        *(unsigned int*)((char*)self + 0x18) = 0;
    }
    return 0;
}

/* get_Crc 常被 IL2CPP 内联，真正兜底的是这里：
 * AssetBundle.LoadFromFileAsync_Internal(string path, uint crc, ulong offset)
 * 把 crc 参数改成 0 = 不校验。
 *
 * v64：顺便把 path 原文打出来（每个 bundle 只打一次，最多若干条），
 * 用来确认剧情 bundle 的加载路径形态 —— 「分支展开」要在这之前把字节改掉：
 *   · 若是 jar:file:///…/base.apk!/assets/…  → 得自己读 APK 里的 zip 条目
 *   · 若是普通文件路径（已解到缓存）        → 直接 open/read 即可 */
static void* (*orig_load_file_async)(void* path, unsigned int crc, unsigned long long offset) = nullptr;
static int g_ab_log = 0;
static int g_ab_story_log = 0;

static void* hook_load_file_async(void* path, unsigned int crc, unsigned long long offset) {
    char pb[320];
    pb[0] = 0;
    str_to_buf(path, pb, sizeof(pb));

    int is_story = (strstr(pb, "story_assets") != nullptr);
    if (is_story ? (g_ab_story_log++ < 8) : (g_ab_log++ < 25)) {
        LOGI("[ab] LoadFromFileAsync%s crc=%08x off=%llu path=%s",
             is_story ? "(剧情)" : "", crc, offset, pb);
    }

    if (crc) {
        if (g_ab_log < 25) LOGI("[mod] LoadFromFileAsync crc=%08x -> 0（跳过 CRC 校验）", crc);
        crc = 0;
    }
    return orig_load_file_async ? orig_load_file_async(path, crc, offset) : nullptr;
}

/* ---- DynamicModifier 懒加载 ----
 * mod 里的 DynamicModifier asset 没被提前注册进 cachedDynamicModifierDict 时，游戏会报
 *   "获取DynamicModifier失败：Item/ShiQuanDaBuWan"
 * 这里在查找失败后，自己按 "DynamicModifier/<key>.asset" 从 mod 的 Addressables 补加载。 */
static void* (*orig_get_modifier)(void* key) = nullptr;
static void* (*load_asset_from_mod)(void* type, void* key) = nullptr;
static void* g_dm_type = nullptr;

static void* hook_get_modifier(void* key) {
    void* r = orig_get_modifier ? orig_get_modifier(key) : nullptr;
    if (r) return r;

    char kbuf[192];
    str_to_buf(key, kbuf, sizeof(kbuf));
    if (!kbuf[0]) return nullptr;

    char full[256];
    snprintf(full, sizeof(full), "DynamicModifier/%s.asset", kbuf);

    if (!g_dm_type) {
        void* k = find_class("WuLin", "DynamicModifier");
        if (k) g_dm_type = class_type(k);
    }
    if (!g_dm_type || !load_asset_from_mod) {
        LOGI("[mod] GetModifier(\"%s\") 失败；懒加载不可用 type=%p fn=%p", kbuf,
             g_dm_type, (void*)load_asset_from_mod);
        return nullptr;
    }
    void* s = A_string_new(full);
    void* o = load_asset_from_mod(g_dm_type, s);
    LOGI("[mod] GetModifier(\"%s\") 未注册 → 懒加载 %s => %p", kbuf, full, o);
    return o;
}

/* ================================================= 暗取面板诊断 */
/* StealPanelUI.Init 里 ShowPack 传的是 npc.backPack(0x140)，
 * 而「暗取池」是 npc.stealBackPack(0x150) —— 两个不同的包。
 * 把两边内容都打进日志，才知道玩家在暗取面板里到底看得见哪一份。 */
static void (*orig_steal_init)(void* self) = nullptr;

static void dump_pack_tids(const char* tag, void* pack) {
    if ((uintptr_t)pack < 0x10000) { LOGI("暗取: %s = null", tag); return; }
    void* list = *(void**)((char*)pack + 0x48);
    if ((uintptr_t)list < 0x10000) { LOGI("暗取: %s 无 contents", tag); return; }
    void* arr = *(void**)((char*)list + 0x10);
    int n = *(int*)((char*)list + 0x18);
    int alen = ((uintptr_t)arr > 0x10000) ? *(int*)((char*)arr + 0x18) : 0;
    char buf[256];
    int o = 0, shown = 0;
    for (int i = 0; i < n && i < alen && shown < 12; ++i) {
        void* it = ((void**)((char*)arr + 0x20))[i];
        if ((uintptr_t)it < 0x10000) continue;
        int tid = *(int*)((char*)it + 0x1C);
        int st = *(int*)((char*)it + 0x20);
        if (o < (int)sizeof(buf) - 24)
            o += snprintf(buf + o, sizeof(buf) - o, "%d/%d ", tid, st);
        shown++;
    }
    buf[o] = 0;
    LOGI("暗取: %s n=%d [%s]", tag, n, buf);
}

static void hook_steal_init(void* self) {
    if (orig_steal_init) orig_steal_init(self);
    static void* k_sm = nullptr;
    if (!k_sm) k_sm = find_class("WuLin", "StealManager");
    void* sm = k_sm ? find_object_of_type(k_sm) : nullptr;
    if ((uintptr_t)sm < 0x10000) return;
    void* role = *(void**)((char*)sm + OFF_SM_SELECT_ROLE);
    if ((uintptr_t)role < 0x10000) return;
    void* gi = *(void**)((char*)role + OFF_ROLE_DATA);
    if ((uintptr_t)gi < 0x10000) return;
    dump_pack_tids("backPack", *(void**)((char*)gi + 0x140));
    dump_pack_tids("stealBackPack", *(void**)((char*)gi + 0x150));
    dump_pack_tids("dropOnDeadBackPack", *(void**)((char*)gi + 0x148));
}

/* ================================================================ 安装 */
static void install_tweaks() {
    g_i2cpp = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
    if (g_i2cpp) {
        A_domain_get = (fn_i2c_get)dlsym(g_i2cpp, "il2cpp_domain_get");
        A_get_assemblies = (fn_i2c_asm)dlsym(g_i2cpp, "il2cpp_domain_get_assemblies");
        A_get_image = (fn_i2c_img)dlsym(g_i2cpp, "il2cpp_assembly_get_image");
        A_class_from_name = (fn_i2c_cls)dlsym(g_i2cpp, "il2cpp_class_from_name");
        A_class_get_type = (fn_i2c_cls_ty)dlsym(g_i2cpp, "il2cpp_class_get_type");
        A_type_get_object = (fn_i2c_ty_obj)dlsym(g_i2cpp, "il2cpp_type_get_object");
        A_object_new = (fn_i2c_new)dlsym(g_i2cpp, "il2cpp_object_new");
        A_string_new = (fn_i2c_str)dlsym(g_i2cpp, "il2cpp_string_new");
        A_array_new = (fn_i2c_arr)dlsym(g_i2cpp, "il2cpp_array_new");
        A_class_init = (fn_i2c_clsinit)dlsym(g_i2cpp, "il2cpp_runtime_class_init");
        A_obj_get_class = (fn_i2c_objcls)dlsym(g_i2cpp, "il2cpp_object_get_class");
        A_class_get_name = (fn_i2c_clsname)dlsym(g_i2cpp, "il2cpp_class_get_name");
        LOGI("il2cpp api: dom=%p asm=%p img=%p cls=%p ty=%p tyobj=%p new=%p str=%p",
             (void*)A_domain_get, (void*)A_get_assemblies, (void*)A_get_image,
             (void*)A_class_from_name, (void*)A_class_get_type, (void*)A_type_get_object,
             (void*)A_object_new, (void*)A_string_new);
    } else {
        LOGE("libil2cpp.so dlopen 失败");
    }
    cache_images();
    LOGI("程序集 %zu 个", g_nimg);

    /* ---- mod 的 bundle CRC 放行（必须早于任何 mod 加载） ---- */
    hook_install("Addressables.Crc", (void*)(g_base + RVA_ABRO_GET_CRC),
                 (void*)hook_get_crc, (void**)&orig_get_crc);
    hook_install("AssetBundle.LoadFromFileAsync", (void*)(g_base + RVA_AB_LOAD_FROM_FILE_ASYNC),
                 (void*)hook_load_file_async, (void**)&orig_load_file_async);

    /* ---- DynamicModifier 懒加载（patch 里的动态修改器没注册时兜底） ----
     * 实测这条路走不通：运行时 LoadAssetFromMod 取不到，返回 null 后
     * DynamicModifierInstance.get_IsValid 抛 NullReferenceException，
     * 导致「十全大补丸」连用都用不了。默认关闭。 */
#if WULIN_DM_LAZY
    load_asset_from_mod = (void* (*)(void*, void*))(g_base + RVA_MOD_LOAD_ASSET_FROM_MOD);
    hook_install("DynamicModifier.GetModifier", (void*)(g_base + RVA_DM_GET_MODIFIER),
                 (void*)hook_get_modifier, (void**)&orig_get_modifier);
#endif

    /* ---- 易物强化（作价恒占优含搬空 + 池子） ---- */
    trade_install();

    /* ---- 暗取面板诊断（看清暗取面板显示的是 backPack 还是 stealBackPack） ---- */
    hook_install("暗取.Init", (void*)(g_base + RVA_STEAL_UI_INIT),
                 (void*)hook_steal_init, (void**)&orig_steal_init);

    /* ---- 水面可走 ---- */
    water_install();

    /* ---- 大地图 / 马车 ---- */
    hook_install("地图.Contains", (void*)(g_base + RVA_HASHSET_CONTAINS),
                 (void*)hook_contains, (void**)&orig_contains);
    hook_install("地图.IsMapEnable", (void*)(g_base + RVA_MAP_IS_ENABLE),
                 (void*)hook_is_map_enable, (void**)&orig_is_map_enable);
    hook_install("马车.IsPassIcon", (void*)(g_base + RVA_CAB_IS_PASS),
                 (void*)hook_is_pass_icon, (void**)&orig_is_pass_icon);
    hook_install("地图.initIcon", (void*)(g_base + RVA_INIT_ICON),
                 (void*)hook_initIcon, (void**)&orig_initIcon);
    hook_install("马车.UpdateIconShow", (void*)(g_base + RVA_CAB_UPDATE_ICON),
                 (void*)hook_cabIcon, (void**)&orig_cabIcon);

    /* ---- 剧情里的「图标显示」类条件：判定期间抑制地图解锁（v48） ----
     * 不抑制的话，地图解锁会把「某图标没显示」判成「显示了」，
     * 让这类剧情门槛恒真（R10112-2「烈酒与故事」就是这么被提前获知的）。 */
    if (hook_install("剧情.大地图图标条件", (void*)(g_base + RVA_CITY_VISIBLE_CHECK),
                     (void*)hook_city_visible_cc, (void**)&orig_city_visible_cc))
        LOGI("剧情条件: 判断大地图图标显示 @ %p（判定期间走原版）",
             (void*)(g_base + RVA_CITY_VISIBLE_CHECK));
    if (hook_install("剧情.马车图标条件", (void*)(g_base + RVA_CAB_PASS_COND),
                     (void*)hook_cab_pass_cc, (void**)&orig_cab_pass_cc))
        LOGI("剧情条件: 判断马车图标是否通过 @ %p（判定期间走原版）",
             (void*)(g_base + RVA_CAB_PASS_COND));

    /* ---- 剧情条件 ----
     * 两个子类 hook 都把「队友在队」判断换成基类实现，不再整函数恒真。 */
    base_check_all = (fn_check)(g_base + RVA_STORY_BASE);
    LOGI("剧情条件: 基类 CheckAllConnectNode @ %p", (void*)base_check_all);
    hook_install("剧情.IsJoinedTeam", (void*)(g_base + RVA_STORY_JOINED),
                 (void*)hook_check_joined, (void**)&orig_check_joined);
    hook_install("剧情.HavePartner", (void*)(g_base + RVA_STORY_PARTNER),
                 (void*)hook_check_partner, (void**)&orig_check_partner);

    /* ContinueArrange 递归看门狗：条件放宽后 Hold 空转会在这一层栈溢出 */
    hook_install("剧情.ContinueArrange", (void*)(g_base + RVA_STORY_CONTINUE),
                 (void*)hook_continue_arrange, (void**)&orig_continue_arrange);

    /* 兜底：名单外的剧情图，times 非 0 就不再放行（防未知的循环事件）。
     * 名单内的图在这里直接放行，交给原版判断。 */
    hook_install("剧情.HoldCheck", (void*)(g_base + RVA_HOLD_CHECK),
                 (void*)hook_hold_check, (void**)&orig_hold_check);

    /* v96：控制台 join 指令（静默，命中即吞） */
    join_install_hooks();

    /* v50：离队容错 —— remove 一个不在队的角色时当作已成功，避免剧情卡死 */
    if (hook_install("剧情.伙伴离队容错", (void*)(g_base + RVA_CHANGE_PARTNER),
                     (void*)hook_change_partner, (void**)&orig_change_partner)) {
        LOGI("剧情: 伙伴离队容错 已挂 @ 0x%X", RVA_CHANGE_PARTNER);
    }

    /* v51 诊断：进场区域条件的实际判定值 */
    if (hook_install("诊断.进场区域条件", (void*)(g_base + RVA_ENTER_AREA_CC),
                     (void*)hook_enter_area, (void**)&orig_enter_area)) {
        LOGI("诊断: 进场区域条件 已挂 @ 0x%X", RVA_ENTER_AREA_CC);
    }

    /* v63：UGUI Text 的 get/set 两条 hook 已撤（详见文件里「鸣谢」那一节的注释）。
     * 实测干活的是下面 TMP 的 set_text；而那两个函数相距只有 8 字节、补丁要 16 字节，
     * 装在一起会互相踩掉字面量池 → 一旦有人调用就跳到垃圾地址。 */
    LOGI("界面: UGUI Text get/set hook 已弃用（改走 TMP）");

    /* v60：TMPro 那套（主界面按钮实测走这个）
     * v63：get_text 保留 —— 它在 v62 之前的崩溃就是这段代码的跳板重定位 bug 引起的，
     * 已在 hook.cpp 里修好（内部 cbz 分支现在会绝对化到跳板槽位）。 */
    if (hook_install("界面.鸣谢TMPget", (void*)(g_base + RVA_TMP_TEXT_GET),
                     (void*)hook_tmp_get, (void**)&orig_tmp_get)) {
        LOGI("界面: TMP get_text 已挂 @ 0x%X", RVA_TMP_TEXT_GET);
    }
    if (hook_install("界面.鸣谢TMPset", (void*)(g_base + RVA_TMP_TEXT_SET),
                     (void*)hook_tmp_set, (void**)&orig_tmp_set)) {
        LOGI("界面: TMP set_text 已挂 @ 0x%X", RVA_TMP_TEXT_SET);
    }

    /* v61：剧情期间显示「熟悉后姓名」
     * 只需要名字 getter 的地址（直接调），不需要 hook 它。
     * v86：回退成 get_RealFullName（「姓+名」）；v84/v85 试过带称号的版本，
     *      实测手机端显示层不认，见 rn_apply_rename 的注释。 */
    orig_realfullname = (void* (*)(void*))(g_base + RVA_GCI_REALFULLNAME);
    LOGI("名字: get_RealFullName = %p（直接调用，不 hook）", (void*)orig_realfullname);
    if (hook_install("名字.get_FullName", (void*)(g_base + RVA_GCI_FULLNAME),
                     (void*)hook_fullname, (void**)&orig_fullname)) {
        LOGI("名字: get_FullName 已挂 @ 0x%X", RVA_GCI_FULLNAME);
    }
    if (hook_install("名字.get_FullNameWithTitle", (void*)(g_base + RVA_GCI_FULLNAME_TITLE),
                     (void*)hook_fullname_title, (void**)&orig_fullname_title)) {
        LOGI("名字: get_FullNameWithTitle 已挂 @ 0x%X", RVA_GCI_FULLNAME_TITLE);
    }
    /* v79：剧情播放起止（名字切换只在播放期间生效） */
    if (hook_install("名字.播放开始", (void*)(g_base + RVA_SSN_ONACTIVE),
                     (void*)hook_ssn_onactive, (void**)&orig_ssn_onactive)) {
        LOGI("名字: StoryScenarioNode.OnActive 已挂 @ 0x%X", RVA_SSN_ONACTIVE);
    }
    if (hook_install("名字.播放结束", (void*)(g_base + RVA_SSN_CONTINUE),
                     (void*)hook_ssn_continue, (void**)&orig_ssn_continue)) {
        LOGI("名字: StoryScenarioNode.Continue 已挂 @ 0x%X", RVA_SSN_CONTINUE);
    }
    if (hook_install("名字.剧情收尾", (void*)(g_base + RVA_SCN_ONRESULT),
                     (void*)hook_scn_onresult, (void**)&orig_scn_onresult)) {
        LOGI("名字: StoryCloseNode.OnResult 已挂 @ 0x%X", RVA_SCN_ONRESULT);
    }

    /* v73：气泡入口直接改名 —— 这是唯一确认能把「熟悉后姓名」显示出来的位置 */
    if (hook_install("名字.气泡Init", (void*)(g_base + RVA_BUBBLE_INIT),
                     (void*)hook_bubble_init, (void**)&orig_bubble_init)) {
        LOGI("名字: NpcTalkingBubble.Init 已挂 @ 0x%X", RVA_BUBBLE_INIT);
    }
    /* v74：对话框说话人姓名的产出函数（两个版本都挂） */
    if (hook_install("名字.对话名Runtime", (void*)(g_base + RVA_TMN_NPCNAME_RUNTIME),
                     (void*)hook_tmn_name, (void**)&orig_tmn_name)) {
        LOGI("名字: TalkMessageNode.GetNpcNameRuntime 已挂 @ 0x%X", RVA_TMN_NPCNAME_RUNTIME);
    }
    if (hook_install("名字.SLG对话名Runtime", (void*)(g_base + RVA_SLGTMN_NPCNAME_RUNTIME),
                     (void*)hook_slgtmn_name, (void**)&orig_slgtmn_name)) {
        LOGI("名字: SlgTalkMessageNode.GetNpcNameRuntime 已挂 @ 0x%X", RVA_SLGTMN_NPCNAME_RUNTIME);
    }

#if WULIN_PROBE
    probe_install();
#endif

    /* ---- 面板（最后装：它自己会建 UI） ---- */
#if WULIN_PANEL
    ui_install();
#else
    LOGI("面板已关闭（本版只测功能，全部默认开启）");
#endif

    LOGI("全部功能默认开启（面板可逐项关闭）");
}

/* ============================================================ 启动流程 */
static int phdr_cb(struct dl_phdr_info* info, size_t, void* data) {
    const char* n = info->dlpi_name;
    if (!n || !*n) return 0;
    const char* b = strrchr(n, '/');
    b = b ? b + 1 : n;
    if (strcmp(b, "libil2cpp.so") == 0) {
        *(uintptr_t*)data = (uintptr_t)info->dlpi_addr;
        return 1;
    }
    return 0;
}

static uintptr_t find_i2cpp() {
    uintptr_t base = 0;
    dl_iterate_phdr(phdr_cb, &base);
    return base;
}

static void* worker(void*) {
    LOGI("worker started, 等待 libil2cpp.so ...");
    void* h = nullptr;
    for (int i = 0; i < 1200 && !h; ++i) {
        h = dlopen("libil2cpp.so", RTLD_NOW | RTLD_NOLOAD);
        if (!h) usleep(50000);
    }
    if (!h) { LOGE("libil2cpp.so 一直没出现"); return nullptr; }

    g_base = find_i2cpp();
    LOGI("libil2cpp base = 0x%lx", (unsigned long)g_base);

    fn_i2c_get domain_get = (fn_i2c_get)dlsym(h, "il2cpp_domain_get");
    if (!domain_get) { LOGE("找不到 il2cpp_domain_get"); return nullptr; }

    /* 运行时初始化完成前绝对不能碰 il2cpp API（会和 Runtime::Init 撞车） */
    sleep(8);
    for (int i = 0; i < 2400; ++i) {
        if (domain_get()) {
            LOGI("il2cpp runtime READY");
            install_tweaks();
            return nullptr;
        }
        usleep(50000);
    }
    LOGE("il2cpp 运行时超时未就绪");
    return nullptr;
}

__attribute__((constructor))
static void wulin_on_load() {
    unlink(LOG_PATH);
    LOGI("=========== libwulin.so %s 已加载 (pid %d, 游戏 %s) ===========", MOD_VER, getpid(),
         GAME_VERSION);
    pthread_t t;
    if (pthread_create(&t, nullptr, worker, nullptr) != 0) {
        LOGE("pthread_create 失败");
        return;
    }
    pthread_detach(t);
}



