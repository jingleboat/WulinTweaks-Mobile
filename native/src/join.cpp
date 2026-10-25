/* 控制台指令 `join <人物ID|人物名> [1]` —— 手机版（v96）
 *
 * 与 PC 1.11.21 对齐，全部走游戏自己的字段/接口，**不打印任何界面文字**：
 *   入场：hook ConsoleUI.InvokeCommand(string)（RVA 0x244FA1C），命中即吞掉
 *   ① 入队        PlayerTeamManager.AddCharacter（先查 HasCharacterAsMember / TeamSize < MaxTeamSize）
 *   ② 复活        m_isAlive → SetAlive(true,false)，血为 0 再 FullyRecover()
 *   ③ 解除陌生    SetRelation 双向 100 + ChangeAdditionPropWithLog("初次见面", +1)
 *                 + m_surName / m_givenName 清空（陌生状态时）
 *   ④ 可入家园    CharacterTeamMemberExtendData.CouldBackToFief = true
 *                 + m_TeamMemberState 的 leaveTeamMap/Pos/Dir
 *                 + StoryManager.WriteString("UnlockInviting-<uid>")
 *   ⑤ 结缘        SetBond + InstantLearnKungfu + ApplyBuff(85xx/89xx/88xx)
 *
 * RVA / 偏移全部来自 dumpout-intl/dump.cs（国服国际版 V1.2.0820B131）。
 * 约定：CALL(ret, rva, 参数类型...)(args..., nullptr) —— 末尾 nullptr 是 il2cpp 的 MethodInfo*。
 */
#include "common.h"
#include "hook.h"
#include "join.h"

#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <time.h>
#include <dlfcn.h>

#define CALL(ret, rva, ...) ((ret (*)(__VA_ARGS__, void*))(g_base + (rva)))

/* ---------------------------------------------------------------- RVA */
#define RVA_CONSOLE_INVOKE_STR   0x244FA1C   /* ConsoleUI.InvokeCommand(string) */
#define RVA_CONSOLE_INTERNAL     0x244FB90   /* ConsoleUI.InvokeCommandInternal()：Enter 键/按钮走这条 */
#define OFF_CONSOLE_TEXTINPUT    0xA0        /* ConsoleUI.textInput（TMP_InputField） */

#define RVA_GCI_INSTANCE_ID      0x223AE50   /* int get_InstanceId() */
#define RVA_GCI_TEMPLETE_UID     0x223AE58   /* int get_TempleteUid() */
#define RVA_GCI_IS_ALIVE         0x223AE8C   /* bool get_IsAlive() */
#define RVA_GCI_STRANGER         0x223B8AC   /* bool get_IsUsingStangerName() */
#define RVA_GCI_CUR_HP           0x223BCE8   /* int get_CurHp() */
#define RVA_GCI_SET_RELATION     0x223FFA4   /* void SetRelation(int otherId,int relation) */
#define RVA_GCI_SET_SURNAME      0x2246CC8   /* void SetSurName(string) */
#define RVA_GCI_SET_GIVENNAME    0x2246D3C   /* void SetGivenName(string) */
#define RVA_GCI_LEARN_KUNGFU     0x2249514   /* KungfuInstance InstantLearnKungfu(int,int,bool) */
#define RVA_GCI_CHG_ADDPROP      0x224C3FC   /* void ChangeAdditionPropWithLog(string,int) */
#define RVA_GCI_FULLY_RECOVER    0x2251D80   /* void FullyRecover(bool) */
#define RVA_GCI_SET_ALIVE        0x2252A9C   /* void SetAlive(bool,bool) */
#define RVA_GCI_APPLY_BUFF       0x2257444   /* BuffInstance ApplyBuff(int,int,int,Decimal[]) */
#define RVA_GCI_SET_BOND         0x225A78C   /* void SetBond(int,BondType,bool) */

#define RVA_PTM_MAX_SIZE         0x22B234C   /* int get_MaxTeamSize() */
#define RVA_PTM_TEAM_SIZE        0x22B2354   /* int get_TeamSize() */
#define RVA_PTM_PLAYER           0x22B26F0   /* GameCharacterInstance get_PlayerDataInstance() */
#define RVA_PTM_CUR_MAP          0x22B2430   /* int get_CurMapId() */
#define RVA_PTM_HAS_MEMBER       0x22B72E0   /* bool HasCharacterAsMember(GCI,bool) */
#define RVA_PTM_ADD              0x22B78C0   /* bool AddCharacter(GCI) */

#define RVA_GCM_GET_CHARACTER    0x2263114   /* GCI GetCharacter(int) */
#define RVA_CFG_POOL_DATA        0x2269B88   /* CharacterPoolDataScriptObject get_CharacterPoolDataScriptObject() */

#define RVA_SM_HAVE_STRING       0x2309778   /* bool HaveString(string) */
#define RVA_SM_WRITE_STRING      0x2309800   /* void WriteString(string) */

#define RVA_CTMED_GETDATA        0x3D376F0   /* CharacterTeamMemberExtendData GetData(int) */

/* ---------------------------------------------------------------- 偏移 */
#define OFF_BDS_UID              0x10        /* BaseDataStorageClass.Uid / UName */
#define OFF_BDS_UNAME            0x18
#define OFF_CPD_STRANGER_NAME    0x20        /* CharacterPoolData.NameAsStranger */
#define OFF_CPD_SURNAME          0x28
#define OFF_CPD_GIVENNAME        0x30
#define OFF_CTMED_FIEF           0x50        /* CharacterTeamMemberExtendData.CouldBackToFief */
#define OFF_POOL_ARRAY           0x58        /* CharacterPoolDataScriptObject.CharacterPoolData[] */
#define OFF_ARRAY_LEN            0x18        /* il2cpp 数组：length 0x18，元素 0x20 */
#define OFF_ARRAY_DATA           0x20
#define OFF_TMS_JOINED           0x10        /* TeamMemberState.isJoined */
#define OFF_TMS_LEAVE_MAP        0x1C        /* leaveTeamMap */
#define OFF_TMS_LEAVE_POS        0x20        /* leaveTeamPos (Vector3Int) */
#define OFF_TMS_LEAVE_DIR        0x2C        /* leaveTeamDir */

/* ---------------------------------------------------------------- 缓存 */
static void* g_cfg = nullptr;
static void* g_ptm = nullptr;
static void* g_sm = nullptr;
static uint64_t g_cache_t = 0;

static uint64_t jnow_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000ull + (uint64_t)(ts.tv_nsec / 1000000);
}

static void* mng(void** slot, const char* ns, const char* name)
{
    uint64_t now = jnow_ms();
    if (*slot && (uintptr_t)*slot > 0x10000 && now - g_cache_t < 60000) return *slot;
    void* k = find_class(ns, name);
    void* inst = k ? find_object_of_type(k) : nullptr;
    *slot = ((uintptr_t)inst > 0x10000) ? inst : nullptr;
    g_cache_t = now;
    return *slot;
}

static void* cfg()  { return mng(&g_cfg, "WuLin", "GameConfig"); }
static void* ptm()  { return mng(&g_ptm, "WuLin", "PlayerTeamManager"); }
static void* sm()   { return mng(&g_sm,  "WuLin", "StoryManager"); }

static void* str(const char* s) { return s ? A_string_new(s) : nullptr; }

static void* get_char(int uid)
{
    void* m = mng(&g_cfg, "WuLin", "GameCharacterManager");
    if (!m) return nullptr;
    void* c = CALL(void*, RVA_GCM_GET_CHARACTER, void*, int)(m, uid, nullptr);
    return ((uintptr_t)c > 0x10000) ? c : nullptr;
}

static int  gci_uid(void* inst)   { return CALL(int,  RVA_GCI_TEMPLETE_UID, void*)(inst, nullptr); }
static int  gci_iid(void* inst)   { return CALL(int,  RVA_GCI_INSTANCE_ID,  void*)(inst, nullptr); }
static bool gci_alive(void* inst) { return CALL(bool, RVA_GCI_IS_ALIVE,     void*)(inst, nullptr); }

/* ---------------------------------------------------------------- ① 复活 */
static int revive(void* inst)
{
    if (!inst) return 0;
    if (gci_alive(inst)) return 0;
    CALL(void, RVA_GCI_SET_ALIVE, void*, bool, bool)(inst, true, false, nullptr);
    int hp = CALL(int, RVA_GCI_CUR_HP, void*)(inst, nullptr);
    if (hp <= 0) CALL(void, RVA_GCI_FULLY_RECOVER, void*, bool)(inst, false, nullptr);
    return gci_alive(inst) ? 1 : 0;
}

/* ---------------------------------------------------------------- ② 入队 */
static int add_team(void* inst)
{
    void* t = ptm();
    if (!t) return 0;
    if (CALL(bool, RVA_PTM_HAS_MEMBER, void*, void*, bool)(t, inst, false, nullptr)) return 0;
    int size = CALL(int, RVA_PTM_TEAM_SIZE, void*)(t, nullptr);
    int max  = CALL(int, RVA_PTM_MAX_SIZE,  void*)(t, nullptr);
    if (size >= max) return 0;
    return CALL(bool, RVA_PTM_ADD, void*, void*)(t, inst, nullptr) ? 1 : 0;
}

/* ---------------------------------------------------------------- ③ 解除陌生 */
static void unstranger(void* inst)
{
    if (!inst) return;
    void* t = ptm();
    if (t)
    {
        void* me = CALL(void*, RVA_PTM_PLAYER, void*)(t, nullptr);
        if ((uintptr_t)me > 0x10000)
        {
            int meId = CALL(int, RVA_GCI_INSTANCE_ID, void*)(me, nullptr);
            int herId = CALL(int, RVA_GCI_INSTANCE_ID, void*)(inst, nullptr);
            CALL(void, RVA_GCI_SET_RELATION, void*, int, int)(inst, meId, 100, nullptr);
            CALL(void, RVA_GCI_SET_RELATION, void*, int, int)(me, herId, 100, nullptr);
        }
    }

    /* 初次见面：陌生状态下补 1（她若已是熟人就跳过，避免越加越多） */
    if (CALL(bool, RVA_GCI_STRANGER, void*)(inst, nullptr))
    {
        void* key = str("初次见面");
        CALL(void, RVA_GCI_CHG_ADDPROP, void*, void*, int)(inst, key, 1, nullptr);
        /* 清陌生名：实例里存的是不带 @ 的陌生名，@ 是显示层加的 */
        CALL(void, RVA_GCI_SET_SURNAME, void*, void*)(inst, str(""), nullptr);
        CALL(void, RVA_GCI_SET_GIVENNAME, void*, void*)(inst, str(""), nullptr);
    }
}

/* ---------------------------------------------------------------- ④ 可入家园 */
static void fief_ready(void* inst)
{
    if (!inst) return;
    int uid = gci_uid(inst);
    if (uid <= 0) return;

    /* CouldBackToFief：数据表里已有该 uid 的记录就置真（原表只有 90 行，没有记录时跳过） */
    void* so = mng(&g_cfg, "WuLin", "CharacterTeamMemberExtendDataScriptObject");
    if (!so) so = find_object_of_type(find_class("WuLin", "CharacterTeamMemberExtendDataScriptObject"));
    if (so)
    {
        void* rec = CALL(void*, RVA_CTMED_GETDATA, void*, int)(so, uid, nullptr);
        if ((uintptr_t)rec > 0x10000) *(bool*)((char*)rec + OFF_CTMED_FIEF) = true;
        else LOGI("[join] uid=%d 家园：数据表没有该角色的记录，跳过 CouldBackToFief", uid);
    }

    /* 队友状态：存在就补离队回程点（不存在则不动，手机端没有 InitTeamMemberState 可用） */
    void* tms = *(void**)((char*)inst + OFF_TEAM_STATE);
    if ((uintptr_t)tms > 0x10000)
    {
        if (*(int*)((char*)tms + OFF_TMS_LEAVE_MAP) <= 0)
        {
            void* t = ptm();
            int map = 0;
            if (t) map = CALL(int, RVA_PTM_CUR_MAP, void*)(t, nullptr);
            if (map > 0) *(int*)((char*)tms + OFF_TMS_LEAVE_MAP) = map;
        }
    }

    /* UnlockInviting 标签：家园「未完成相关事件」就是缺它 */
    void* s = sm();
    if (s)
    {
        char tag[64];
        snprintf(tag, sizeof(tag), "UnlockInviting-%d", uid);
        void* ts = str(tag);
        if (!CALL(bool, RVA_SM_HAVE_STRING, void*, void*)(s, ts, nullptr))
            CALL(void, RVA_SM_WRITE_STRING, void*, void*)(s, ts, nullptr);
    }
}

/* ---------------------------------------------------------------- ⑤ 结缘 */
static int bond_kungfu(int uid)
{
    switch (uid)
    {
    case 20025: return 300001; case 10092: return 300002; case 10084: return 300003;
    case 10064: return 300005; case 10056: return 300006; case 10009: return 300007;
    case 10081: return 300008; case 10008: return 300009; case 20063: return 300011;
    case 10052: return 300013; case 10108: return 300021; case 10039: return 300022;
    default: return 0;
    }
}

static void make_bond(void* inst)
{
    if (!inst) return;
    int uid = gci_uid(inst);

    void* t = ptm();
    void* me = t ? CALL(void*, RVA_PTM_PLAYER, void*)(t, nullptr) : nullptr;
    if ((uintptr_t)me <= 0x10000) return;

    int meId = CALL(int, RVA_GCI_INSTANCE_ID, void*)(me, nullptr);
    int type = (uid == 20025 || uid == 20063) ? 1 : 2;      /* Husband=1 / Wife=2 */
    CALL(void, RVA_GCI_SET_BOND, void*, int, int, bool)(inst, meId, type, true, nullptr);

    int kf = bond_kungfu(uid);
    if (kf <= 0) return;
    CALL(void*, RVA_GCI_LEARN_KUNGFU, void*, int, int, bool)(inst, kf, 1, false, nullptr);

    int xx = kf % 100;
    CALL(void*, RVA_GCI_APPLY_BUFF, void*, int, int, int, void*)(inst, 8500 + xx, 0, 1, nullptr, nullptr);
    CALL(void*, RVA_GCI_APPLY_BUFF, void*, int, int, int, void*)(inst, 8900 + xx, 0, 1, nullptr, nullptr);
    CALL(void*, RVA_GCI_APPLY_BUFF, void*, int, int, int, void*)(me,   8800 + xx, 0, 1, nullptr, nullptr);
}

/* ---------------------------------------------------------------- 名字 → uid */
static int parse_r_id_str(const char* s)
{
    if (!s) return 0;
    int v = 0, n = 0;
    for (const char* p = s; *p; ++p)
    {
        if (*p == ' ' || *p == '\t') break;
        if (*p < '0' || *p > '9') return 0;
        v = v * 10 + (*p - '0');
        if (++n > 9) return 0;
    }
    return n > 0 ? v : 0;
}

static int utf8_eq(const char* a, const char* b) { return a && b && strcmp(a, b) == 0; }

static int resolve(const char* token)
{
    int id = parse_r_id_str(token);
    if (id > 0) return id;

    void* c = cfg();
    if (!c) return 0;
    void* pool = CALL(void*, RVA_CFG_POOL_DATA, void*)(c, nullptr);
    if ((uintptr_t)pool <= 0x10000) return 0;
    void* arr = *(void**)((char*)pool + OFF_POOL_ARRAY);
    if ((uintptr_t)arr <= 0x10000) return 0;
    int n = *(int*)((char*)arr + OFF_ARRAY_LEN);
    if (n <= 0 || n > 20000) return 0;

    int best = 0;
    char full[128], name[128];
    for (int i = 0; i < n; ++i)
    {
        void* e = *(void**)((char*)arr + OFF_ARRAY_DATA + (size_t)i * 8);
        if ((uintptr_t)e <= 0x10000) continue;

        const char* un = (const char*)str_to_buf(*(void**)((char*)e + OFF_BDS_UNAME), name, sizeof(name));
        const char* sn = (const char*)str_to_buf(*(void**)((char*)e + OFF_CPD_SURNAME), full, sizeof(full));
        char gn[64];
        const char* g = (const char*)str_to_buf(*(void**)((char*)e + OFF_CPD_GIVENNAME), gn, sizeof(gn));

        char joined[160];
        snprintf(joined, sizeof(joined), "%s%s", sn ? sn : "", g ? g : "");

        if (!utf8_eq(un, token) && !utf8_eq(joined, token)) continue;
        int uid = *(int*)((char*)e + OFF_BDS_UID);
        if (uid > 0 && (best == 0 || uid < best)) best = uid;   /* 同名克隆取最小 uid */
    }
    return best;
}

/* ---------------------------------------------------------------- 指令本体 */
static int join_handle(const char* text)
{
    if (!text) return 0;
    while (*text == ' ' || *text == '\t') ++text;
    if (strncmp(text, "join", 4) != 0 && strncmp(text, "JOIN", 4) != 0) return 0;
    const char* p = text + 4;
    if (*p && *p != ' ' && *p != '\t') return 0;
    while (*p == ' ' || *p == '\t') ++p;
    if (!*p) { LOGI("[join] 用法：join <人物ID|人物名> [1]"); return 1; }

    char target[128];
    int ti = 0;
    while (*p && *p != ' ' && *p != '\t' && ti < (int)sizeof(target) - 1) target[ti++] = *p++;
    target[ti] = 0;

    int bond = 0;
    while (*p == ' ' || *p == '\t') ++p;
    if (*p == '1') bond = 1;
    else if ((p[0] == 't' || p[0] == 'T') && (p[1] == 'r' || p[1] == 'R')) bond = 1;

    int uid = resolve(target);
    if (uid <= 0) { LOGI("[join] 找不到目标：%s", target); return 1; }

    void* inst = get_char(uid);
    if (!inst) { LOGI("[join] uid=%d 实例不存在（还没被游戏创建）", uid); return 1; }

    int revived = revive(inst);
    int joined = add_team(inst);
    unstranger(inst);
    fief_ready(inst);
    if (bond) make_bond(inst);

    LOGI("[join] %s → uid=%d，复活=%d，本次入队=%d，结缘=%d", text, uid, revived, joined, bond);
    return 1;
}

/* ---------------------------------------------------------------- hook */
static void (*orig_console_invoke_str)(void* self, void* command) = nullptr;

static void hook_console_invoke_str(void* self, void* command)
{
    char buf[256];
    buf[0] = 0;
    if (command) str_to_buf(command, buf, sizeof(buf));
    if (join_handle(buf)) return;                    /* 吞掉：不打印任何提示 */
    if (orig_console_invoke_str) orig_console_invoke_str(self, command);
}

/* v97：Enter 键走的是 `InvokeCommandInternal()`，它自己读 `textInput.text`。
 * 手机端不引 TMP 头，运行期用 il2cpp_class_get_method_from_name 取 `TMP_InputField.get_text`。 */
static void* (*A_class_get_method)(void*, const char*, int) = nullptr;
static void* g_tmp_get_text = nullptr;

static void* ui_input_text(void* consoleUI)
{
    if ((uintptr_t)consoleUI < 0x10000) return nullptr;
    void* field = *(void**)((char*)consoleUI + OFF_CONSOLE_TEXTINPUT);
    if ((uintptr_t)field < 0x10000) return nullptr;

    if (!g_tmp_get_text)
    {
        if (!A_class_get_method && g_i2cpp)
            A_class_get_method = (void* (*)(void*, const char*, int))dlsym(g_i2cpp, "il2cpp_class_get_method_from_name");
        if (A_class_get_method)
        {
            void* k = find_class("TMPro", "TMP_InputField");
            void* mi = k ? A_class_get_method(k, "get_text", 0) : nullptr;
            if (mi) g_tmp_get_text = *(void**)mi;              /* MethodInfo 首字段 = methodPointer */
        }
        if (!g_tmp_get_text) { LOGE("[join] 取不到 TMP_InputField.get_text，内部入口只放行"); return nullptr; }
    }
    void* s = ((void* (*)(void*, void*))g_tmp_get_text)(field, nullptr);
    return ((uintptr_t)s > 0x10000) ? s : nullptr;
}

static void (*orig_console_internal)(void* self) = nullptr;

static void hook_console_internal(void* self)
{
    char buf[256];
    buf[0] = 0;
    void* s = ui_input_text(self);
    if (s) str_to_buf(s, buf, sizeof(buf));
    if (join_handle(buf)) return;                    /* 吞掉：不打印任何提示 */
    if (orig_console_internal) orig_console_internal(self);
}

void join_install_hooks(void)
{
    if (hook_install("控制台.join", (void*)(g_base + RVA_CONSOLE_INVOKE_STR),
                     (void*)hook_console_invoke_str, (void**)&orig_console_invoke_str))
        LOGI("控制台: join 指令 @ %p（静默，命中即吞）", (void*)(g_base + RVA_CONSOLE_INVOKE_STR));
    else
        LOGE("控制台: join 指令 hook 失败 @ %p", (void*)(g_base + RVA_CONSOLE_INVOKE_STR));

    if (hook_install("控制台.join(内部)", (void*)(g_base + RVA_CONSOLE_INTERNAL),
                     (void*)hook_console_internal, (void**)&orig_console_internal))
        LOGI("控制台: join 指令(InvokeCommandInternal) @ %p", (void*)(g_base + RVA_CONSOLE_INTERNAL));
    else
        LOGE("控制台: join(内部) hook 失败 @ %p", (void*)(g_base + RVA_CONSOLE_INTERNAL));
}
