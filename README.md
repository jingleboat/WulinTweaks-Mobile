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
| **剧情分支展开** | 把「某角色是否在队」的互补门对（`Negate=0/1`）由 **if/else 改成顺序播放**：先播在队侧的内容，再播不在队侧的内容。**通用规则**，不限定某张图（跳过嵌套门对 / 无出口 / 两侧都不是单节点剧情的情形） |
| **剧情期间显真名** | 剧情对话期间把出场角色从「陌生时姓名」（`@冷峻严肃的中年人`）换成本地拼的真名。只作用于当前正在播的那张图；剧情收尾、换图、读档后还原 |
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
  wulin_main.cpp   入口、条件 hook、姓名替换（播放窗口）、离队容错、地图解锁、日志
  flatten.cpp      剧情分支展开（通用规则：门对 → 独占内容区 → 出口改指 → 门判真）
  trade.cpp        易物三件套 + 池子
  water.cpp        水面可走
  ui.cpp           游戏内面板（顶部居中）
  hook.cpp         arm64 inline hook 框架（自建跳板，含内部跳转绝对化）
  probe.cpp        诊断（#if WULIN_PROBE）
  common.h         全部 RVA 与结构体字段偏移（绑定 V1.2.0820B131）
  glyphs.h         面板文字字库（由 gen_glyphs.py 生成，别手改）
  stub.s           注入引导桩
native/build.ps1   NDK 构建脚本
mk_apk.py          APK 重打包
gen_glyphs.py      生成面板文字字库 glyphs.h
tools_hookaudit.py 审计 hook 目标前 4 条指令（内部分支/字面量/条件跳转是否安全）
```

> 2026-10-23 清理：早期逆向阶段的一次性脚本（`sv_*` / `dis_*` / `find_*` / `fix_*` /
> `patch_v*` / 存档扫描等，共 156 个文件）已删除，只保留构建与长期可复用的工具。
> 需要那批脚本时从 git 历史里取：`git log --diff-filter=D --name-only`。

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

只列功能节点，中间的调试版本见 git 历史。

| 版本 | 内容 |
|---|---|
| v50 | 伙伴离队容错（按 ID） |
| v54 | 离队容错补 `mode=1` Index 越界 |
| v55 | `ContinueArrange` 看门狗阈值 16 → 256（修释法寺剧情播完卡死） |
| v56–v57 | 面板改屏幕顶部居中 / 贴顶 |
| v58 | 主界面「鸣谢」按钮文字改为 mod |
| v63 | 跳板内部跳转绝对化（修 `TMP.get_text` 钩子导致的读档栈溢出） |
| v69 | 剧情分支展开首次跑通（只针对 `R10049-2`，靠 `conditionPort` 认门） |
| v70 | 修 `team_ids` 读 il2cpp 数组漏 `+0x20`；展开改为不写 `partnerID/Negate` |
| v77 | 姓名落点确定为角色实例 `m_surName / m_givenName` |
| v78 | 性能收口：类指针缓存、展开缓存扩到 16 槽、日志分类计数 |
| v79 | 姓名只在「剧情真正播放期间」生效（`OnActive` / `Continue` 界定窗口） |
| **v80** | 分支展开改**通用规则**（不再限定某张图，A/B 类都展开）；姓名窗口改事件驱动（`Continue` 只续期 30s、新增 `StoryCloseNode.OnResult` 收尾信号）；**改名提到 `OnActive` 原函数之前**，修首句闪陌生名 |

---

## 已知限制

- 放宽只作用在 `Negate=0` 一侧，同一图里 `需要伙伴(X)` 与 `需要伙伴(X, Negate=1)` 可能同时成立 ——
  这是有意保留（流程内放宽），容错补丁只兜底「动作落空」，不解决走错分支。
- 剧情分支展开：**B 类（两侧都是剧情）会被串起来都播**。若某张图两侧本来是互斥内容，
  可出现"同一情节演两遍"或动作节点副作用叠加；手机端没有黑名单配置，遇到只能在面板第 8 行整体关掉。
- 剧情期间的姓名窗口没有「剧情收尾」以外的结束信号，`StoryCloseNode` 若不走，名字会多留最多 30 秒。
- RVA 与字段偏移绑定 **V1.2.0820B131**，换游戏版本必须重新标定。
