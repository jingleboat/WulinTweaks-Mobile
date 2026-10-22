#pragma once
/* 易物：作价恒占优（含确认搬空）/ 显示池子 —— v83 起只有两个开关 */

void trade_install();          /* 装 hook（在 install_tweaks 里调） */
void trade_hook_fire();        /* 供 ui 面板刷新用，无实际逻辑 */
