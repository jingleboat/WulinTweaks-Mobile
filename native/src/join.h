#pragma once

/* 控制台指令 `join <人物ID|人物名> [1]` —— 手机版（v96，与 PC 1.11.21 对齐）
 *
 * 入口：hook `ConsoleUI.InvokeCommand(string)`，命中就把这一条吞掉（不打印任何文字）。
 * 动作：入队 → 解除陌生（初次见面=1、好感100、清陌生名）→ 可入家园
 *       （CouldBackToFief + 队友状态 + UnlockInviting-<uid>）→ 末尾 `1` 时设为结缘。
 *       已死亡角色先复活（m_isAlive）。
 */
void join_install_hooks(void);
