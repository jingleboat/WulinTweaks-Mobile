# WulinTweaks Mobile —— 大侠立志传 手机版（国际版）改造

《大侠立志传》安卓国际版（`com.xd.dxlzz.global.gp`）的功能补丁，纯 native 注入（`libwulin.so` + `libil2cpp` stub），不依赖任何 mod 框架。
与 PC 端 [WulinTweaks](https://github.com/jingleboat/WulinTweaks) 同一套规则、两端行为对齐。

---

## ⚠️ 重要提示：一个会关掉柳月樱线的坑

> **无名渡口与富商对话，剧情条件放宽会和诸葛神机前往海蛟帮，会变为「海蛟帮贵客」，导致柳月樱事件关闭。**

机制：剧情条件放宽后，海蛟帮支线 `M5030-0` 会比你预想的更早推到底，给存档打上 `海蛟帮贵客` 标记。
而柳月樱引子图 `R10049-0` 的作废条件之一正是这条标记 —— 标记一旦存在，引子图立刻变 `Failed`，
`R10049-2`「护花使者」及之后整条线永久关闭。

**想走柳月樱线，就在九黎部落拾取社稷之印之前，先把大梁城城门口那场「帮忙解围」做掉。**

已经踩坑的话，存档层面可以救：

1. 从 `SaveObjectStory.save` 的 `TagString` 里删掉 `海蛟帮贵客`
2. 把 `R10049-0` 的 `State` 重置为 `0 (NotOpen)`
3. 按攻略重走：**进大梁城客栈 → 回城门口 → 选「上前帮忙解围」** → 打赢拿樱花香囊

⚠️ **只把 `State` 改成 `4` 是无效的。** `StoryManager.StoryComplated` 开头有一段掩码判断
（`0x92` = bit1|bit4|bit7），State 已经是 4 就直接 `ret`，后面「注册后续图」的逻辑不执行，
`R10049-2` 永远不会被加载。必须让它真正走完一次。

---

## 功能

| 功能 | 说明 |
|---|---|
| 剧情条件放宽 | 放宽「某角色是否在队 / 是否有伙伴」这类条件，让不同队伍组合也能触发剧情。`R{角色id}-*` 图在本命角色从未入队时仍按原版（角色入队后才开自己的故事线） |
| 伙伴离队容错 | 「伙伴邀请或离队」要踢一个不在队（或队伍没那么多位置）的角色时，视为已成功，避免剧情卡死 |
| 大地图解锁 | 解除世界地图可去城市的限制 |
| 马车地图解锁 | 解除马车可去地点的限制 |
| 水面可走 | 可进入水域（含入水状态） |
| 易物强化 | 作价恒占优 / 显示池子 / 确认搬空 |
| 游戏内面板 | 屏幕顶部居中，可逐项开关上述功能 |

主界面「鸣谢」按钮的显示文字被替换成 **mod**（手机版把这一格当 mod 入口用）。

---

## 目录

```
native/src/
  wulin_main.cpp   入口、剧情/队伍条件 hook、伙伴离队容错、地图解锁、日志
  trade.cpp        易物三件套 + 池子
  water.cpp        水面可走
  ui.cpp           游戏内面板（顶部居中）
  hook.cpp         arm64 inline hook 框架（自建跳板）
  probe.cpp        诊断
  common.h         全部 RVA 与结构体字段偏移（绑定 V1.2.0820B131）
  stub.s           注入引导桩
native/build.ps1   NDK 构建脚本
mk_apk.py          APK 重打包 / 签名
```

辅助脚本（排查用）：`sv_dec.py`（存档解密）、`sv_patch.py`（改图状态）、`sv_reset.py`（重置图）、
`sv_dumpbyname.py`（按名 dump 剧情图节点）、`sv_dis.py`（ARM64 反汇编）、`sv_probe.py`（存档搜索）。

---

## 构建

需要 Android NDK（`build.ps1` 里默认 `android-ndk-r27c`）。

```powershell
cd native
pwsh -File build.ps1            # → libwulin.so + stub.bin
```

打包（替换 APK 里的 so）：

```powershell
python -X utf8 mk_apk.py build --src <上一版.apk> --out $tmp --no-sign `
    --replace "lib/arm64-v8a/libwulin.so=<绝对路径>"
& "<build-tools>\zipalign.exe" -p -f 4 $tmp $al
$env:JAVA_HOME="<jdk>"
& "<build-tools>\apksigner.bat" sign --ks keystore\wulin.jks --ks-pass pass:<pass> `
    --ks-key-alias wulin --out <最终.apk> $al
```

> ⚠️ **`mk_apk.py sign` 自带的 `keystore/wulin.pem` + `wulin.crt` 与已装版本的 `wulin.jks` 不是同一套证书**，
> 用它签名会 `INSTALL_FAILED_UPDATE_INCOMPATIBLE`。必须走 `apksigner` + `wulin.jks`。
> 一致性校验：`apksigner verify --print-certs`，SHA-256 应固定不变。

---

## 版本

| 版本 | 内容 |
|---|---|
| v50 | 伙伴离队容错（按 ID） |
| v51–v53 | 进场区域条件诊断日志 |
| v54 | 离队容错补 `mode=1` Index 越界 |
| v55 | `ContinueArrange` 看门狗阈值 16 → 256（修释法寺剧情播完卡死） |
| v56 | 面板改屏幕顶部居中 |
| v57 | 面板贴顶（边距 100 → 12） |
| **v58** | 主界面「鸣谢」按钮文字改为 mod |

---

## 已知限制

- 放宽只作用在 `Negate=0` 一侧，同一图里 `需要伙伴(X)` 与 `需要伙伴(X, Negate=1)` 可能同时成立 ——
  这是有意保留（流程内放宽），容错补丁只兜底「动作落空」，不解决走错分支。
- RVA 与字段偏移绑定 **V1.2.0820B131**，换游戏版本必须重新标定。
