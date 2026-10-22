# -*- coding: utf-8 -*-
"""v5：关掉探针、剧情条件放宽默认关 + 加自旋闸门，避免 Hold 节点空转栈溢出。"""
p = 'src/wulin_main.cpp'
s = open(p, encoding='utf-8').read()

# 1) 探针关掉
s = s.replace('#define WULIN_PROBE 1', '#define WULIN_PROBE 0')

# 2) 剧情条件放宽默认关
s = s.replace('volatile int F_STORY = 1;', 'volatile int F_STORY = 0;')

open(p, 'w', encoding='utf-8').write(s)
print('probe off / F_STORY=0 ->', 'WULIN_PROBE 0' in s, 'F_STORY = 0' in s)

# 3) 给两个剧情 hook 加“每秒最多放行 20 次”的闸门
old_joined = '''static bool hook_check_joined(void* self, int io, void* line) {
    if (F_STORY) {
        fn_check base = (fn_check)(g_base + RVA_STORY_BASE);
        return base(self, io, line);
    }
    return orig_check_joined ? orig_check_joined(self, io, line) : false;
}

static bool hook_check_partner(void* self, int io, void* line) {
    if (F_STORY) {
        fn_check base = (fn_check)(g_base + RVA_STORY_BASE);
        return base(self, io, line);
    }
    return orig_check_partner ? orig_check_partner(self, io, line) : false;
}'''

new_joined = '''/* 「剧情条件放宽」的自旋闸门 -------------------------------------------------
 * 直接让 IsJoinedTeam / HavePartner 恒真会把剧情 Hold 节点变成"条件随时满足"，
 * StoryManager.ContinueArrange <-> HoldNode.OnActive 互相递归，切场景时栈溢出闪退
 * （实测 crash 栈：#n 反复出现 HoldNode.OnActive / HoldNode.Continue / ContinueArrange）。
 * 这里按「同一条件对象每秒最多放行 20 次」限流：正常剧情判定照旧放行，
 * 一旦出现空转就退回原逻辑，让 Hold 节点正常等待。 */
#define GATE_SLOTS 16
typedef struct { void* p; long long t; int n; } StoryGate;
static StoryGate g_gate[GATE_SLOTS];

static long long now_ms() {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static int story_pass_once(void* self) {
    long long t = now_ms();
    for (int i = 0; i < GATE_SLOTS; ++i) {
        if (g_gate[i].p == self) {
            if (t - g_gate[i].t > 1000) { g_gate[i].t = t; g_gate[i].n = 1; return 1; }
            g_gate[i].n++;
            return g_gate[i].n <= 20;
        }
    }
    for (int i = 0; i < GATE_SLOTS; ++i) {
        if (!g_gate[i].p) { g_gate[i].p = self; g_gate[i].t = t; g_gate[i].n = 1; return 1; }
    }
    return 1;
}

static bool hook_check_joined(void* self, int io, void* line) {
    if (F_STORY && story_pass_once(self)) return true;
    return orig_check_joined ? orig_check_joined(self, io, line) : false;
}

static bool hook_check_partner(void* self, int io, void* line) {
    if (F_STORY && story_pass_once(self)) return true;
    return orig_check_partner ? orig_check_partner(self, io, line) : false;
}'''

if old_joined in s:
    s = s.replace(old_joined, new_joined)
    open(p, 'w', encoding='utf-8').write(s)
    print('story gate installed')
else:
    print('!! joined hook block not found, manual check needed')
