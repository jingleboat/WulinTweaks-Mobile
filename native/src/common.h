/*
 * WulinTweaks · 手机端公共定义
 * =============================
 * 1) 日志 / 已解析地址 / 运行时开关（面板改的就是这些）
 * 2) 全部 RVA 与结构体字段偏移（绑定国际版 V1.2.0820B131，换版本必须重新标定）
 * 3) il2cpp C API 指针（来自 libil2cpp.so 的导出符号，不写死偏移）
 */
#pragma once

#include <stdint.h>
#include <stddef.h>
#include <android/log.h>

/* ------------------------------------------------------------------ 版本锚点 */
#define GAME_VERSION "V1.2.0820B131"
#define MOD_VER "v108"         /* v108：times 压制去掉 R 图整族豁免（修苗彩蝶循环）*/
#define PKG_NAME "com.xd.dxlzz.global.gp"
#define LOG_PATH "/sdcard/Android/data/" PKG_NAME "/files/wulin.log"
/* 易物「已拿走」台账（避免同一件池子物件被反复复制） */
#define LEDGER_PATH "/sdcard/Android/data/" PKG_NAME "/files/trade-taken.txt"

/* ------------------------------------------------------------------ 日志 */
void glog(const char* fmt, ...);
#define LOGI(...) glog(__VA_ARGS__)
#define LOGE(...) glog("E: " __VA_ARGS__)

/* ------------------------------------------------------------------ 全局 */
extern uintptr_t g_base;   /* libil2cpp.so 加载基址 */
extern void* g_i2cpp;      /* libil2cpp.so 句柄（dlsym 用） */

/* 运行时开关：1 = 开。面板点一下就变，hook 里实时读。
 * 易物只有两个开关：「确认搬空」跟随 F_TRADE_WIN（v83 起不再是独立开关）。 */
extern volatile int F_TRADE_WIN;    /* 易物作价恒占优（含确认搬空、空手也能成交） */
extern volatile int F_TRADE_POOLS;  /* 易物显示池子 */
extern volatile int F_MAP;          /* 大地图解锁 */
extern volatile int F_CAB;          /* 马车地图解锁 */
extern volatile int F_STORY;        /* 剧情/队伍条件放宽 */
extern volatile int F_WATER;        /* 水面可走（进水池 + 触发入水状态） */
extern volatile int F_FLAT;         /* 剧情分支展开（Branch Flattener） */

/* 剧情分支展开：在已加载的图对象上套用补丁（node 传图上任意节点即可，内部按 graph 去重）。
 * 定义在 flatten.cpp。 */
void flatten_try(void* any_node);
/* 该条件节点是否已被「分支展开」接管（= 恒真）。传 HavePartnerCondition / IsJoinedTeamCondition 实例。 */
int flatten_cond_always_true(void* cond);
/* v87：该**门**（HoldNode）是否已被接管 —— 由 hook_hold_check 整门判真，与 PC 的
 * BranchFlatten.IsGateForced 同一语义（故意不看开关）。 */
int flatten_gate_forced(void* gate);
/* 只读体检：打印某 uid 的角色模板内容（姓/名/陌生时姓名/UName）与实例是否存在。 */
void relax_name_probe(int uid);
/* 提前打开某 uid 的「熟悉后姓名」窗口（由分支展开在 patch 图时调用）。 */
void relax_name_open(int uid, void* graph);
/* 剧情播放起止（由 StoryScenarioNode.OnActive / Continue 钩子驱动）。 */
void story_play_begin(void* playNode);
void story_play_end(void);
void story_play_end_now(void);   /* v80：剧情收尾时立刻还原（StoryCloseNode.OnResult 驱动） */

/* ================================================================== RVA */
/* ---- il2cpp 通用 ---- */
#define RVA_FIND_OBJECTS_ALL 0x4AD117C  /* Resources.FindObjectsOfTypeAll(Type) -> Object[] —— 用来枚举 CharacterPoolData 模板 */
#define RVA_FIND_OBJECT_TYPE 0x4AD9BF0   /* Object.FindObjectOfType(Type) —— 真货，参数必须是 System.Type 对象 */
#define RVA_SINGLETON_GET 0x3A2C090      /* MonoSingleton<object>.get_Instance —— 参数按 MethodInfo 传（传元数据槽里的
                                          * 泛型方法用法槽，就能拿到对应单例）。resolve_save 一直用它，别改成别的。 */
/* XNode：按端口名取 NodePort（走 Dictionary 的托管 API，不依赖 keys/values 序列化数组）。
 * v66 实测：ports 字典的 keys/values 两个 List 在运行期可能是空的，
 * 直接按平行数组下标读书不到端口 —— 必须用这两个方法。 */
#define RVA_NODE_GET_OUTPUTPORT 0x4BDA820  /* Node.GetOutputPort(string) -> NodePort */
#define RVA_NODE_GET_PORT       0x4BDA70C  /* Node.GetPort(string)       -> NodePort */
/* NpcTalkingBubble（角色头顶/对话框气泡）
 *   Init(ActorViewElemenetAdapator, NpcTalkingContent)  —— 气泡创建时调，拿到「谁在说话 + 说什么」
 *   nameText +0x48 (TextMeshProUGUI)  talkingText +0x50
 * NpcTalkingContent: characterInstance +0x10  content +0x18  nameOverride +0x20
 * 实测（v72 日志）：对话框名字既不来自 get_FullName 的返回值，也不等于 m_givenName 的 set_text，
 * 所以只能在气泡 Init 之后直接把 nameText 改成「熟悉后姓名」。 */
#define RVA_BUBBLE_INIT 0x2429F60
#define OFF_BUBBLE_NAMETEXT 0x48
#define OFF_TC_CHARACTER    0x10
#define OFF_TC_NAMEOVERRIDE 0x20
/* TalkMessageNode（scenario 里的「对话」节点）—— 对话框中说话人姓名的真正产出点
 *   NpcID 0x50  NpcName 0x58  showName 0x64  showNpcName 0x68  showImage 0x70  text 0x80
 * GetNpcNameRuntime() 返回运行时显示的说话人姓名；SlgTalkMessageNode 是 SLG 版同名方法。 */
#define RVA_TMN_NPCNAME_RUNTIME     0x3D865D0
#define RVA_SLGTMN_NPCNAME_RUNTIME  0x26B117C
#define OFF_TMN_NPCID 0x50
/* StoryScenarioNode（剧情图里的「播放剧情」节点）—— 剧情真正播放的唯一可靠起止点
 *   OnActive()              开始播这段 scenario
 *   Continue(string port)   播完，走向下一个节点
 * 用来把「显示熟悉后姓名」收窄到播放期间（而不是被 Hold 重判一直续命）。 */
#define RVA_SSN_ONACTIVE  0x3DA0730
#define RVA_SSN_CONTINUE  0x3DA0798
#define RVA_SCN_ONRESULT        0x3DAC5AC  /* StoryCloseNode.OnResult —— 剧情真正收尾（v80 用它立刻还原姓名） */
#define OFF_SCN_GRAPHNAME       0x60       /* StoryCloseNode.GraphName（string） */
#define RVA_GET_NAME 0x4AD8B9C           /* Object.get_name() */
#define RVA_GO_CHILD_OBJ 0x4AD34C8       /* Component.get_transform()（拿子节点 Transform 时也用它） */
#define RVA_COMP_GET_GO 0x4AD3504        /* Component.get_gameObject() */
#define RVA_GO_GET_COMPONENT 0x4AD35B8   /* GameObject.GetComponent(Type) */
#define RVA_COMP_GET_TRANSFORM 0x4AD34C8 /* Component.get_transform() */
#define RVA_T_SET_LAST 0x4AE2294         /* Transform.SetAsLastSibling() */
#define RVA_CANVAS_SCALE 0x4B79BAC       /* Canvas.get_scaleFactor() */

/* ---- 易物 TradingWithNpcManager ---- */
#define RVA_TR_START 0x23BFFC0           /* static void StartTrading(GameCharacterInstance) */
#define RVA_TR_CANCEL 0x23C038C          /* static void CancelTrading() */
#define RVA_TR_CONFIRM 0x23C0684         /* static void ConfirmTrading() */
#define RVA_TR_VALUE_OFFSET 0x23C0AD8    /* static long ValueOffset() */
#define RVA_TR_GET_NPC 0x23BFD98         /* static GameCharacterInstance get_npc() */
#define RVA_TR_PLAYER_ZONE 0x23BFE50     /* static GameItemPack get_playerTradingZone() */
#define RVA_TR_NPC_ZONE 0x23BFF08        /* static GameItemPack get_npcTradingZone() */

/* ---- 背包 / 物品 ---- */
#define RVA_PACK_GET_TOTAL 0x227F70C     /* long GameItemPack.GetTotalValue(float) */
#define RVA_PACK_ADD_MERGE 0x227DD64     /* bool AddItem(GameItemInstance,bool) —— 可堆叠物品会合并进同类堆 */
#define RVA_PACK_ADD_INDEP 0x227DF18     /* bool AddItemAsIndependent(GameItemInstance) —— 只 contents.Add，绝不合并 */
#define RVA_PACK_REMOVE 0x2273A98        /* bool RemoveItem(GameItemInstance,int) */
#define RVA_ITEM_CTOR_ID 0x227C714       /* GameItemInstance..ctor(int id,int stack,int craftLevel) */
#define RVA_ITEM_CTOR_KEY 0x227C514      /* GameItemInstance..ctor(string key,int stack,int craftLevel) */
#define RVA_ITEM_ISVALID 0x227C328       /* bool get_IsValid() */
#define RVA_ITEM_STACKABLE 0x227C4CC     /* bool get_IsStackable() = Templete.IsStackable && modifyData==null */
#define RVA_ITEM_CHANGESTACK 0x227C86C   /* bool ChangeStack(int amount) —— m_stack += amount，无回调 */

/* ---- 角色 / 池子 ---- */
#define RVA_CHAR_BACKPACK 0x223C098      /* GameItemPack get_BackPack() */
#define RVA_CHAR_DROP_PACK 0x223C0A0     /* GameItemPack get_DropOnDeadBackPack() */
#define RVA_CHAR_STEAL_PACK 0x223C0A8    /* GameItemPack get_StealBackPack() */
#define RVA_CHAR_TEMPLETE 0x223AE7C      /* CharacterPoolData get_Templete() */
#define RVA_CHAR_TPL_NAME 0x223AE60      /* string get_TempleteName() */
#define RVA_CHAR_INSTANCE_ID 0x223AE50   /* int get_InstanceId() */
#define RVA_CHAR_TPL_UID 0x223AE58       /* int get_TempleteUid() */

/* ---- 队伍背包 ---- */
#define RVA_TEAM_INVENTORY 0x22B240C     /* PlayerTeamManager.get_TeamInventory() */

/* ---- 易物 UI 刷新 ---- */
#define RVA_SHOWER_SETDIRTY 0x2425EDC    /* ItemPackShower.SetDirty() */

/* ---- 暗取（潜行偷窃） ---- */
#define RVA_STEAL_UI_INIT 0x247E268      /* StealPanelUI.Init()：面板里 ShowPack 的就是 npc.backPack */
#define OFF_SM_SELECT_ROLE 0x68          /* StealManager.SelectRole */
#define OFF_ROLE_DATA 0x20               /* Role.gameCharacterInstance */

/* ---- 大地图 / 马车 ---- */
#define RVA_MAP_IS_ENABLE 0x2578030      /* UIWorldMapManager.IsMapEnable(int) */
#define RVA_CAB_IS_PASS 0x2420448        /* UICabMapManager.IsPassIcon(int) */
#define RVA_HASHSET_CONTAINS 0x38C4830   /* HashSet<T>.Contains(容器,id,类型信息) */
#define RVA_INIT_ICON 0x257680C          /* UIWorldMapManager.initIcon() */
#define RVA_CAB_UPDATE_ICON 0x2419540    /* UICabMapManager.UpdateIconShow() */
#define RVA_T_CHILD_COUNT 0x4AE21E0      /* Transform.get_childCount() */
#define RVA_T_GET_CHILD 0x4AE2848        /* Transform.GetChild(int) */
#define RVA_GET_OR_CREATE_MAPDATA 0x22E7F1C /* SaveObjectMapManager.GetOrCreateMapData(int) */
#define VA_CLASS_CELL_UIMAP 0x4E87E68    /* UIWorldMapManager 的 Il2CppClass 元数据槽 */

/* ---- 剧情条件 ---- */
#define RVA_STORY_JOINED 0x3D936EC       /* IsJoinedTeamCondition.CheckAllConnectNode */
#define RVA_STORY_PARTNER 0x3D923FC      /* HavePartnerCondition.CheckAllConnectNode */
#define RVA_STORY_BASE 0x3D7E2A0         /* ConditionNode.CheckAllConnectNode（基类实现） */
#define RVA_STORY_CONTINUE 0x2311FE4     /* StoryManager.ContinueArrange(Node,string) —— 递归看门狗挂这里 */
#define RVA_GET_CHARACTER 0x2263114      /* GameCharacterManager.GetCharacter(int) —— 从 uid 拿实例 */
#define RVA_HOLD_CHECK 0x3D9DEFC         /* HoldNode.CheckCondition() —— 「Hold 只放行一次」挂这里 */

/* ---- 伙伴邀请/离队（v50）：离队容错
 * mod 放宽「某角色在队」后，某些图的对话在角色**不在队**时也会播；紧接着的
 * ChangePartnerNode(remove) 要把他踢出队，人本来不在 → 动作落空 → 后续
 * 「在队/不在队」两条流程判断可能都进不去，剧情卡死（EJ-004 大梁捞朱 实测）。
 * 对策：remove 一个不在队的角色时当作已成功，直接跳过原版调用。
 * 原版 OnResult 只是「调静态委托 OnResultAction」的空壳，跳过 = 什么都不做。 */
#define RVA_CHANGE_PARTNER 0x3DA7878     /* ChangePartnerNode.OnResult() */

/* ---- 诊断（v51）：进场区域条件
 * EnterAreaCondition 的判定是 (TotalMapID == 当前地图id) != Negate，但"当前地图id"
 * 取自某个单例的 +0x94，静态分析定不到具体类。加日志把它和槽位里的 TotalMapID 一起打出来。 */
#define RVA_ENTER_AREA_CC 0x268DAD0      /* DllStoryConditionDelegate.EnterAreaConditionCheckAllConnectNode */
#define RVA_PTM_CUR_MAPID 0x22B2430      /* PlayerTeamManager.get_CurMapId() */
#define OFF_EA_TOTALMAP 0x50             /* EnterAreaCondition.TotalMap (bool) */
#define OFF_EA_TOTALMAPID 0x54           /* EnterAreaCondition.TotalMapID (int) */
#define OFF_EA_NEGATE 0x58               /* EnterAreaCondition.Negate (bool) */
#define OFF_EA_AREASTR 0x68              /* EnterAreaCondition.areaString (string*) */
/* ---- 主界面「鸣谢」按钮改字（v58）
 * 手机版把「鸣谢」位当成了 mod 入口，所以把按钮文字直接显示成 mod。
 * 文本来自 config_assets_all.bundle 的本地化表，改数据要重打包 bundle（还牵扯 m_Crc），
 * 所以改在 Text.set_text 上做替换：只匹配长度 2 的「鳴謝/鸣谢」，命中就换成 "mod"。
 * Il2CppString 布局：+0x10 = 长度(int)，+0x14 = UTF-16 字符数组。 */
#define RVA_TEXT_GET 0x4B9A414
/* TMPro.TMP_Text（主界面按钮实测用的是 TMP，不是 UGUI Text）*/
#define RVA_TMP_TEXT_GET 0x4A422D4   /* TMPro.TMP_Text.get_text */
#define RVA_TMP_TEXT_SET 0x4A423BC   /* TMPro.TMP_Text.set_text */
#define RVA_TEXT_SET 0x4B9A41C           /* UnityEngine.UI.Text.set_text(string) */
#define OFF_IL2STR_LEN 0x10
#define OFF_IL2STR_CHARS 0x14
/* ---- 剧情期间显示「熟悉后姓名」（v61）
 * 放宽之后剧情触发并不算"互动过一次"，所以那些角色头顶/对话框里仍是陌生名（@冷峻严肃的中年人），
 * 光看立绘分不清是谁的彩蛋。这里在「放宽命中」后的窗口期内，把 FullName 换成 RealFullName
 * （陌生状态下 RealFullName 会从模板 CharacterPoolData 拼 SurName + GivenName）。
 *
 *   GameCharacterInstance: +0x98 = CharacterPoolData*  +0xA0 = m_templeteUid
 *                          +0xA8 = m_surName          +0xB0 = m_givenName
 *   CharacterPoolData:     +0x20 = NameAsStranger     +0x28 = SurName  +0x30 = GivenName
 */
#define RVA_GCI_FULLNAME 0x223B75C        /* GameCharacterInstance.get_FullName —— 姓+名，无称号 */
#define RVA_GCI_FULLNAME_TITLE 0x223B828  /* get_FullNameWithTitle = Title 空 ? FullName : Concat(Title,"·",FullName) */
#define RVA_GCI_REALFULLNAME 0x223B920    /* get_RealFullName —— 熟悉 ? FullNameWithTitle : GetLocFullName(模板)【无称号】 */
#define RVA_GCI_REALFULLNAME_TITLE 0x223B998 /* get_RealFullNameWithTitle —— 熟悉 ? FullNameWithTitle
                                              * : GetLocFullNameWithTitle(模板) = Concat(GetLocTitle,"·",GetLocFullName)
                                              * 也就是原版「称号·姓名」（总捕头·洛天雄）。v84 起名字统一走这条。 */
#define RVA_GCI_TITLE 0x223B22C           /* get_Title —— 实例称号（内部走 GetLocTitle 本地化） */
#define RVA_GCI_TITLE_TPL 0x258D3B0       /* static GetLocTitle(CharacterPoolData) —— 称号本地化（读 PersonalTitle） */
#define RVA_GCI_REALFULLNAME_TPL 0x258D0CC   /* static GetLocFullName(CharacterPoolData) —— 姓名，无称号 */
#define RVA_GCI_REALFULLNAME_TITLE_TPL 0x258D4A8 /* static GetLocFullNameWithTitle(CharacterPoolData) —— 称号·姓名 */
#define OFF_GCI_TEMPLATE 0x98
#define OFF_GCI_TPL_UID 0xA0
#define OFF_GCI_SURNAME 0xA8
#define OFF_GCI_GIVENNAME 0xB0
#define OFF_CPD_UID 0x10                 /* BaseDataStorageClass.Uid（= 角色 uid） */
#define OFF_CPD_UNAME 0x18               /* BaseDataStorageClass.UName */
#define OFF_CPD_NAME 0x18                /* CharacterPoolData.name —— 角色正式全名（未结识时也是真名） */
#define OFF_CPD_NAME_STRANGER 0x20
#define OFF_CPD_SURNAME 0x28
#define OFF_CPD_GIVENNAME 0x30
#define OFF_CPD_PERSONAL_TITLE 0x38       /* CharacterPoolData.PersonalTitle（本地化 key） */

/* ChangePartnerNode 字段（dump: WuLin.StateMachine.ChangePartnerNode） */
#define OFF_CP_MODE 0x60                 /* SelectionMode: ID=0 / 队中位置=1 */
#define OFF_CP_INDEX 0x64                /* mode==1 时的队伍位置 */
#define OFF_CP_PARTNER_ID 0x70           /* mode==0 时的 NPC ID */
#define OFF_CP_STATE 0x74                /* PartnerIO: add=0 / remove=1 */

/* 队伍名单访问链（反汇编 PlayerTeamManager.get_TeamSize 0x22B2354 得到） */
#define OFF_PTM_CONTAINER 0x60           /* PlayerTeamManager.dataContainer */
#define OFF_SOC_DATA 0x20                /* SaveObjectContainer<T>.data -> SaveObjectPlayerTeam */
#define OFF_SOPT_LIST 0x90               /* SaveObjectPlayerTeam.playerTeam (List<int>) */
#define OFF_LIST_ITEMS 0x10              /* List<T>._items（指向 il2cpp 数组对象） */
#define OFF_ARRAY_DATA 0x20              /* il2cpp 数组的元素起始偏移：klass 0x00 | monitor 0x08 | bounds 0x10 | max_length 0x18 | 元素 0x20 */
#define OFF_LIST_SIZE 0x18               /* List<T>._size */

/* ---- 剧情里的「地图图标」条件（v48）
 * 这两个条件节点内部查的是大地图/马车的「可见集合」，而手机端的解锁是在
 * HashSet<int>.Contains 上全局拦截的 —— 于是剧情条件也被一起放宽。
 * 这两处 hook 的作用：判定期间临时关掉解锁拦截，让它们读原版数据。 */
#define RVA_CITY_VISIBLE_CHECK 0x3D9A1E0 /* WorldMapCityVisibleCondition.CheckAllConnectNode（判断大地图图标显示） */
#define RVA_CAB_PASS_COND 0x3D88408      /* CabPassIconCondition.CheckAllConnectNode（判断马车图标是否通过） */

/* ---- Addressables / AssetBundle ---- */
#define RVA_ABRO_GET_CRC 0x49E6270       /* AssetBundleRequestOptions.get_Crc() */
#define RVA_AB_LOAD_FROM_FILE_ASYNC 0x4A8B4A8 /* AssetBundle.LoadFromFileAsync_Internal(string,uint,ulong) */

/* ---- DynamicModifier 懒加载 ---- */
#define RVA_DM_GET_MODIFIER 0x221B014        /* DynamicModifierHelper.GetModifier(string) */
#define RVA_MOD_LOAD_ASSET_FROM_MOD 0x22ABA34 /* ModManager.LoadAssetFromMod(Type,string) */

/* ---- 每帧 tick（面板用） ---- */
#define RVA_MTSC_UPDATE 0x217EFD8        /* MainThreadSynchronizationContext.Update() */

/* ---- UGUI 构建（面板） ---- */
#define RVA_GO_CTOR_NAME 0x4AD4F2C       /* GameObject..ctor(string) */
#define RVA_GO_ADD_COMP 0x4AD47EC        /* GameObject.AddComponent(Type) */
#define RVA_GO_GET_TRANSFORM 0x4AD49C8   /* GameObject.get_transform() */
#define RVA_TR_SETPARENT 0x4AE0364       /* Transform.SetParent(Transform,bool) */
#define RVA_RT_ANCHOR_MIN 0x4ADE29C      /* RectTransform.set_anchorMin(Vector2) */
#define RVA_RT_ANCHOR_MAX 0x4ADE3B8      /* set_anchorMax(Vector2) */
#define RVA_RT_ANCHORED_POS 0x4ADE4D4    /* set_anchoredPosition(Vector2) */
#define RVA_RT_SIZE_DELTA 0x4ADE5F0      /* set_sizeDelta(Vector2) */
#define RVA_RT_PIVOT 0x4ADE70C           /* set_pivot(Vector2) */
#define RVA_TEX_CTOR 0x4ABB954           /* Texture2D..ctor(int,int,TextureFormat,int,bool) */
#define RVA_TEX_LOAD_RAW 0x4ABC270       /* LoadRawTextureData(IntPtr,int) */
#define RVA_TEX_APPLY 0x4ABC57C          /* Apply() */
#define RVA_TEX_SET_FILTER 0x4AB94F0     /* Texture.set_filterMode(FilterMode) */
#define RVA_TEX_SET_WRAP 0x4AB92F0       /* Texture.set_wrapMode(TextureWrapMode) */
#define RVA_RAWIMG_SET_TEX 0x4B8E0FC     /* RawImage.set_texture(Texture) */
#define RVA_RAWIMG_SET_UVRECT 0x4B8E1C4  /* RawImage.set_uvRect(Rect) */
#define RVA_GRAPHIC_SET_RAYCAST 0x4B69C1C/* Graphic.set_raycastTarget(bool) */
#define RVA_CANVAS_SET_ORDER 0x4B79FBC   /* Canvas.set_sortingOrder(int) */
#define RVA_CANVAS_SET_MODE 0x4B79A90    /* Canvas.set_renderMode(int) */
#define RVA_DONTDESTROY 0x4AD99A4        /* Object.DontDestroyOnLoad(Object) */
#define RVA_INPUT_MOUSE_DOWN 0x4B259A4   /* Input.GetMouseButtonDown(int) */
#define RVA_INPUT_MOUSE_POS 0x4B25C74    /* Input.get_mousePosition() -> Vector3 */
#define RVA_SCREEN_HEIGHT 0x4AA2DE4      /* Screen.get_height() */
#define RVA_SCREEN_WIDTH 0x4AA2DBC       /* Screen.get_width() */

/* ================================================================= 偏移 */
#define OFF_PACK_CONTENTS 0x48   /* GameItemPack.contents (List<GameItemInstance>) */
#define OFF_ITEM_STACK 0x20      /* GameItemInstance.m_stack */
#define OFF_ITEM_TID 0x1C        /* GameItemInstance.m_templeteId */
#define OFF_ITEM_MODIFY 0x28     /* GameItemInstance.modifyData（非 null ⇒ IsStackable 恒 false） */
#define OFF_CPL_DROP 0x188       /* CharacterPoolData.ItemsDropOnKill (KeyWithValues[]) */
#define OFF_CPL_STEAL 0x190      /* CharacterPoolData.StealPool */
#define OFF_KWV_KEY 0x10         /* KeyWithValues.key (string) */
#define OFF_KWV_VALUES 0x18      /* KeyWithValues.values (List<int>) */
#define OFF_UI_NPC_BAG 0xA8      /* TradingWithNpcUI.npcBackPack */
#define OFF_UI_NPC_TRADE 0xB8    /* TradingWithNpcUI.npcTrading */
#define OFF_HOLD_TIMES 0x70      /* HoldNode.checkSucessTimes（addTimes 置 1 / clearTimes 置 0） */
#define OFF_NODE_GRAPH 0x18      /* XNode.Node.graph -> NodeGraph（ScriptableObject，name 即剧情图 ID） */
#define OFF_GRAPH_NODES 0x18     /* NodeGraph.nodes（List<Node>）—— v95 扫节点类名用 */
#define OFF_PARTNER_ID 0x58      /* ConditionNode(IsJoinedTeam/HavePartner).partnerID */
#define OFF_PARTNER_NEGATE 0x5C  /* 同上 Negate（取反） */
#define OFF_TEAM_STATE 0x130     /* GameCharacterInstance.m_TeamMemberState（== NULL 表示从未入队） */

/* ================================================================= il2cpp API */
typedef void* (*fn_i2c_get)(void);
typedef void** (*fn_i2c_asm)(void*, size_t*);
typedef void* (*fn_i2c_img)(void*);
typedef void* (*fn_i2c_cls)(void*, const char*, const char*);
typedef void* (*fn_i2c_cls_ty)(void*);   /* il2cpp_class_get_type -> const Il2CppType* */
typedef void* (*fn_i2c_ty_obj)(void*);   /* il2cpp_type_get_object -> Il2CppReflectionType* */
typedef void* (*fn_i2c_new)(void*);
typedef void* (*fn_i2c_str)(const char*);
typedef void* (*fn_i2c_arr)(void*, size_t);
typedef void (*fn_i2c_clsinit)(void*);
typedef void* (*fn_i2c_objcls)(void*);        /* il2cpp_object_get_class -> Il2CppClass* */
typedef const char* (*fn_i2c_clsname)(void*); /* il2cpp_class_get_name -> const char* */

extern fn_i2c_objcls A_obj_get_class;
extern fn_i2c_clsname A_class_get_name;
extern fn_i2c_get A_domain_get;
extern fn_i2c_asm A_get_assemblies;
extern fn_i2c_img A_get_image;
extern fn_i2c_cls A_class_from_name;
extern fn_i2c_cls_ty A_class_get_type;
extern fn_i2c_ty_obj A_type_get_object;
extern fn_i2c_new A_object_new;
extern fn_i2c_str A_string_new;
extern fn_i2c_arr A_array_new;
extern fn_i2c_clsinit A_class_init;

/* 按命名空间 + 类名在所有程序集里查 Il2CppClass（找不到返回 0） */
void* find_class(const char* ns, const char* name);
/* 找类的 System.Type 对象（AddComponent / FindObjectOfType 用） */
void* class_type(void* klass);
/* FindObjectOfType：传入 Il2CppClass，返回实例（可能为 0） */
void* find_object_of_type(void* klass);
/* 32 位 int 的 Il2CppClass（造 List/数组用） */
void* class_int32(void);

/* Il2CppString -> 我们自己缓冲区（ASCII 兜底 '?'）；返回 buf */
const char* str_to_buf(void* il2str, char* buf, int cap);

/* ================================================== arm64 小工具（内联 asm 用） */
typedef struct { float x, y; } V2;
typedef struct { float x, y, z; } V3;
typedef struct { float x, y, w, h; } Rect4;
