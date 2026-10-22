/* 引导桩：被 il2cpp_init 的第一条指令 B 进来，dlopen("libwulin.so") 后原样返回。
 *
 * 约束：
 *   - 只用位置无关指令，不含数据段与重定位
 *   - 两条跳转（bl dlopen / b il2cpp_init+4）与「被偷走的那条原指令」
 *     由 inject_stub.py 在放置到 .so 里时回填
 */
    .text
    .globl wulin_stub
    .type wulin_stub, %function
wulin_stub:
    stp  x29, x30, [sp, #-0x30]!      /* 0  保存帧 */

    str  x0, [sp, #0x20]              /* 1  保存 il2cpp_init 的参数 */

    /* 2..6  在栈上拼出 "libwulin.so\0"，避免引用 rodata */
    movz x2, #0x696c                  /* 2  "li" */
    movk x2, #0x7762, lsl #16         /* 3  "wu" */
    movk x2, #0x6c75, lsl #32         /* 4  "ul" */
    movk x2, #0x6e69, lsl #48         /* 5  "in" */
    movz w3, #0x732e                  /* 6  ".s\0\0" */
    movk w3, #0x006f, lsl #16         /* 7  "o" —— 少了这条路径会变成 libwulin.s */

    sub  sp, sp, #0x10                /* 7 */
    stp  x2, x3, [sp]                 /* 8 */
    mov  x0, sp                       /* 9  path */
    mov  w1, #2                       /* 10 RTLD_NOW */

    .inst 0x94000000                  /* 11 bl dlopen        <- 回填 */
    add  sp, sp, #0x10                /* 12 */

    ldr  x0, [sp, #0x20]              /* 13 恢复参数 */
    ldp  x29, x30, [sp], #0x30        /* 14 恢复帧 */

    .inst 0x00000000                  /* 15 原 il2cpp_init 首指令 <- 回填 */
    .inst 0x14000000                  /* 16 b il2cpp_init+4     <- 回填 */

    .size wulin_stub, . - wulin_stub

/* 指令条数固定为 17，inject_stub.py 依据它计算回填位置
 *   11 * 4 = 44   bl
 *   15 * 4 = 60   原指令
 *   16 * 4 = 64   b
 */
