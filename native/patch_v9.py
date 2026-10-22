# -*- coding: utf-8 -*-
"""v9：诊断不再跑在线程里（托管方法在非托管线程里不可靠，之前静默失败），
改成在主线程 IsBlock 回调里数够次数后内联执行；同时把 MapEntity 也从
PathFindingManager.InitPathfindingData 这个已知可靠的入口再抓一份。"""
p = 'src/water.cpp'
s = open(p, encoding='utf-8').read()

# 1) 抓 MapEntity 的第二来源
if 'RVA_PFM_INIT_PATH' not in s:
    s = s.replace('#define RVA_MM_ISSWIM 0x2395AB8   /* bool MapManager.IsSwimGrid(Vector3Int) */',
                  '#define RVA_MM_ISSWIM 0x2395AB8   /* bool MapManager.IsSwimGrid(Vector3Int) */\n'
                  '#define RVA_PFM_INIT_PATH 0x21C3038 /* List<Vector3Int> PathFindingManager.InitPathfindingData(MapEntity) */', 1)

if 'g_me_alt' not in s:
    s = s.replace('static void* g_me = nullptr;', 'static void* g_me = nullptr;\nstatic void* g_me_alt = nullptr; /* InitPathfindingData 传入的实体（已初始化完的那种） */', 1)

# map_load 优先用 g_me_alt
s = s.replace('''    void* me = ((fn2i)(g_base + RVA_MAP_GETINFO))(mgr, mid, nullptr);
    if (!me) return;
    g_me = me;''',
              '''    void* me = g_me_alt;
    if (!me) me = ((fn2i)(g_base + RVA_MAP_GETINFO))(mgr, mid, nullptr);
    if (!me) return;
    g_me = me;''')

# 2) 诊断原因要打日志，别静默返回
s = s.replace('''static void diag_run(void) {
    if (g_diag_done || !g_me || !g_cells) return;
    void* sw = *(void**)((char*)g_me + OFF_ME_SWIM);
    if (!sw) return;''',
              '''static int g_diag_tries = 0;

static void diag_run(void) {
    if (g_diag_done) return;
    if (!g_me || !g_cells) { if (g_diag_tries++ < 3) LOGI("[water:diag] 等待地图(entity=%p cache=%p)", g_me, g_cells); return; }
    void* sw = *(void**)((char*)g_me + OFF_ME_SWIM);
    if (!sw) { if (g_diag_tries++ < 6) LOGI("[water:diag] mSwimGrid 为空(entity=%p)", g_me); return; }''')

s = s.replace('''    if (!any) return; /* 没铺好，等下一轮 */''',
              '''    if (!any) { if (g_diag_tries++ < 12) LOGI("[water:diag] 水面位图还是空的(grid=%p)，继续等", sw); return; }''')

# 3) 线程换成 IsBlock 内联触发
s = s.replace('''    pthread_t t;
    if (pthread_create(&t, nullptr, diag_thread, nullptr) == 0) pthread_detach(t);

''', '')

s = s.replace('''    if (hook_install("MapManager.IsSwimGrid", (void*)(g_base + RVA_MM_ISSWIM),''',
              '''    if (hook_install("PFM.InitPathfindingData", (void*)(g_base + RVA_PFM_INIT_PATH),
                     (void*)hook_pfm_init, (void**)&orig_pfm_init))
        LOGI("[water] 已挂 PFM.InitPathfindingData（取 MapEntity）");
    else
        LOGE("[water] 挂 InitPathfindingData 失败");

    if (hook_install("MapManager.IsSwimGrid", (void*)(g_base + RVA_MM_ISSWIM),''')

# 4) hook_pfm_init + 计数器（放在 hook_isswim 之前）
s = s.replace('''static bool hook_isswim(void* self, uint64_t xy, uint32_t z) {''',
              '''static void* (*orig_pfm_init)(void*, void*) = nullptr;
static void* hook_pfm_init(void* self, void* mapEntity) {
    if (mapEntity) g_me_alt = mapEntity;
    void* r = orig_pfm_init ? orig_pfm_init(self, mapEntity) : nullptr;
    g_diag_done = 0;
    g_diag_tries = 0;
    return r;
}

static bool hook_isswim(void* self, uint64_t xy, uint32_t z) {''')

# 在 hook_isblock 里数次数触发诊断
s = s.replace('''    int mid = *(int*)((char*)self + OFF_MM_CURMAP);
    if (mid != g_cur_map || !g_cells) map_load(self);
    int x = (int)(uint32_t)(xy & 0xffffffffu);''',
              '''    int mid = *(int*)((char*)self + OFF_MM_CURMAP);
    if (mid != g_cur_map || !g_cells) map_load(self);
    if (!g_diag_done && ++g_block_calls > 400) diag_run();
    int x = (int)(uint32_t)(xy & 0xffffffffu);''')

s = s.replace('static int g_diag_tries = 0;', 'static int g_diag_tries = 0;\nstatic int g_block_calls = 0;', 1)
s = s.replace('    g_diag_done = 0;\n    g_diag_tries = 0;\n    return r;', '    g_diag_done = 0;\n    g_diag_tries = 0;\n    g_block_calls = 0;\n    return r;')

open(p, 'w', encoding='utf-8').write(s)
print('v9 patched; diag_thread removed:', 'diag_thread' not in s)
