#pragma once
/* 游戏内 mod 面板（默认展开、可折叠；不用快捷键） */

void ui_install();   /* 装每帧 tick 的 hook（在 install_tweaks 里调） */
int  ui_hit_test_screen(float sx, float sy);  /* 屏幕坐标是否落在面板上（挡点击用） */

/* v125：开关记忆 —— 状态存 <files>/wulin_mod.cfg。
 * load 必须在装各模块 hook 之前调用，这样模块一上来读到的就是上次留下的状态。 */
void ui_settings_load(void);
void ui_settings_save(void);
