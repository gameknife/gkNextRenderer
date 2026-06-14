---
title: "StudioSim —— LLM 驱动的游戏工作室办公室模拟（MVP 开发计划）"
category: plan
status: 已完成
owner: engine
created: 2026-06-07
last_updated: 2026-06-07
---

# StudioSim —— LLM 驱动的游戏工作室办公室模拟（MVP 开发计划）

> **状态**：规划草案（待评审）
> **目标读者**：负责实现本原型的后续 AI agent / 开发者
> **代号**：`StudioSim`（target 名，可改；备选 `CrunchTime` / `OfficeLLM`）
> **前置必读**：[`AGENT_GUIDE/CharacterDemo.md`](../../AGENT_GUIDE/CharacterDemo.md)（最接近的实现模板）、[`AGENT_GUIDE/SCADLoader.md`](../../AGENT_GUIDE/SCADLoader.md)、[`AGENT_GUIDE/QuickJSBindings.md`](../../AGENT_GUIDE/QuickJSBindings.md)、[`AGENTS.md`](../../AGENTS.md)
> **本文写作前已调研的真实引擎设施**：`NextAI::FAIService` / `FAgentLoop`、`NextGameplay::FNavGrid` / `FPathFollower` / `CharacterActor`、SCAD loader 的 `sceneNode.name` 语义节点、`Assets::Scene` 节点查询 API、`NextGameInstanceBase` 生命周期钩子。下文所有 API 引用均为代码中已存在的符号。

---

## 1. 愿景与 MVP 边界

### 1.1 一句话定位

用引擎现有的**本地 LLM**（`localllm` / llama-server）+ **SCAD 程序化场景**，搭一个游戏工作室办公室：**每天开始，玩家先定下今天的团队目标**（从 LLM 给的 3 个选项里选，或自由输入），然后一群用简单几何体表示的"打工人"各按职位**围绕这个目标**展开一天的工作、由 LLM 驱动决策、相互交互；玩家可注入随机事件（竞品发新作 / 断电 / 版本服务器宕机）冲击目标，LLM 员工会做出反应并改变后续走向。

### 1.2 MVP 做什么 / 不做什么

垂直切片优先：先把**"定今日目标 → 目标分解到人 → 决策调度 → LLM 决策 → NPC 行动 → 事件冲击目标 → 链式影响"**这条闭环跑通，再加广度。

| 维度 | MVP 内（In Scope） | MVP 外（Out of Scope，留作扩展） |
| --- | --- | --- |
| **每日目标** | 晨会：LLM 给 3 个候选目标，玩家三选一或**自由输入**自定义；目标分解成各职位今日重点；贯穿全天驱动；18:00 结算 | 多天连续演化、KPI/晋升、目标依赖链 |
| 场景 | 1 个 `office.scad`：开放工位区 + 1 会议室 + 1 茶水间 + 1 洽谈室 + 走道；语义命名点位自动成为锚点 | 多楼层、可破坏环境、动态家具重排 |
| 员工 | 5–8 个，简单几何体（盒/胶囊）+ 颜色区分职位，头顶气泡显示状态/对话 | 骨骼动画、表情、KayKit 角色模型 |
| 移动 | `FNavGrid` A* 在语义点位间寻路 + `FPathFollower` 跟随 | 人群避让、排队、坐下动作 IK |
| 决策 | LLM 在"决策时刻"输出**单个 JSON 动作**（去哪/找谁/做什么/情绪），且**对齐今日目标**；串行调度 + 异步 | 多步工具调用 agent、长期记忆向量库 |
| 日程 | 角色卡 + **目标分解出的个人重点任务**为主线；脚本化默认日程作为确定性 fallback | 复杂排班、加班结算 |
| 事件 | 玩家从面板注入 3 类事件，**冲击今日目标**、改全局世界状态，影响后续决策 | 事件编辑器、持久化、分支剧情树 |
| 交互 | 两员工在同一点位相遇 → 一次轻量对话；会议室聚集 | 多人群聊涌现、关系图谱 |
| 时间 | 一天的**阶段机**：晨会→工作（加速时钟，可暂停/调速）→结算 | 跨天存档、连续多日演化 |
| 平台 | Windows 桌面（与 localllm 同机） | Android/iOS（移动端无本地 LLM） |

**MVP 的成功标准（Demo 验收）**：在 Windows 机上启动 `StudioSim` → **晨会**：LLM 给 3 个今日目标（如"赶在竞品前发布可玩 demo"），玩家三选一或自己输入 → 目标**分解到各职位**写进员工 → 6 名员工围绕目标展开一天（去工位写代码/开评审会/协作）→ 玩家点"竞品发布新游戏"**冲击目标** → 数秒内多名员工气泡出现焦虑/讨论、部分人聚到会议室重排计划 → 18:00 给出**目标达成总结**。**全程离线；LLM 不可用时用 fallback 目标库 + 静态分解 + 脚本日程仍能完整演示。**

---

## 2. 核心玩法循环

```
  ┌══════════════════ 一天的阶段机（EDayPhase）══════════════════┐
  ║                                                              ║
  ║  ① Briefing 晨会（时钟暂停）                                  ║
  ║     LLM 给 3 个今日目标 ──► 玩家三选一 / 自由输入自定义        ║
  ║     ──► 目标分解：拆成每个职位今天的重点任务                   ║
  ║                          │                                   ║
  ║                          ▼                                   ║
  ║  ② Working 工作（加速时钟推进 09:00→18:00）                   ║
  ║     ┌────────────┐  到决策时刻  ┌────────────────────────┐   ║
  ║     │  世界状态   │ ───────────► │  决策调度器（串行预算）   │   ║
  ║     │ 今日目标    │              │  挑 1 个员工 → 组 prompt  │   ║
  ║     │ 当日事件    │ ◄──┐         └───────────┬────────────┘   ║
  ║     │ 全局氛围    │    │ 改变                 ▼                ║
  ║     └────────────┘    │      ┌────────────────────────┐      ║
  ║         ▲             │      │   本地 LLM（异步）       │      ║
  ║         │ 玩家注入事件 │      │  输出 1 个 JSON 动作      │      ║
  ║    ┌────┴────────┐    │      │  （须对齐今日目标）       │      ║
  ║    │ 玩家事件面板 │    │      └───────────┬────────────┘      ║
  ║    │ 竞品/断电/宕机│   │ 失败→脚本日程     ▼                  ║
  ║    │（冲击目标）  │    │      ┌────────────────────────┐      ║
  ║    └─────────────┘    └──────│ NPC 执行：寻路/对话/工作 │      ║
  ║                              │ FNavGrid + 气泡 + 状态   │      ║
  ║                              └────────────────────────┘      ║
  ║                          │                                   ║
  ║                          ▼ 18:00                             ║
  ║  ③ Review 结算：汇总今天行动 ──► LLM 给目标达成总结 + 评分     ║
  ║                                                              ║
  └══════════════════════════════════════════════════════════════┘
```

一句话：**先定今日目标并分解到人 → 时钟推进 → 调度器按预算挑员工 → LLM 给一个对齐目标的动作 → NPC 用 NavGrid 执行 → 玩家事件冲击目标改变情境 → 天黑结算目标达成度。**

---

## 3. 系统架构总览

分层（自底向上），尽量**复用引擎，不重造轮子**：

```
┌──────────────────────────────────────────────────────────────────┐
│ StudioSimGameInstance  (NextGameInstanceBase 子类，编排层)          │
│  OnInit / BeforeSceneRebuild / OnSceneLoaded / OnTick / OnRenderUI │
└───────────────┬──────────────────────────────────────────────────┘
     ┌──────┬────┴──────┬────────────┬────────────┬───────────┬──────────┐
     ▼      ▼           ▼            ▼            ▼           ▼          ▼
 ┌──────┐┌──────┐┌──────────┐┌──────────────┐┌─────────┐┌─────────┐┌────────┐
 │Office││Employ││GoalSystem││DecisionSched ││EventSys ││ World   ││StudioUI│
 │Map   ││eeSys ││今日目标   ││LLM 调度器    ││玩家事件 ││ State   ││HUD/气泡│
 │语义点││几何体││晨会/分解/ ││串行+预算     ││冲击目标 ││时钟/阶段││目标面板│
 │位解析││+移动 ││结算      ││             ││         ││/事件    ││事件面板│
 └──┬───┘└──┬───┘└────┬─────┘└──────┬───────┘└────┬────┘└─────────┘└────────┘
    │       │         │            │             │
    │       ▼         └────────────┼─────────────┘
    │  NextGameplay                ▼
    │  FNavGrid /          NextAI::FAIService
    │  FPathFollower       GenerateTextAsync / (扩展: FAgentLoop)
    ▼       ▼                      ▼
 ┌──────────────────────────────────────────────────────────────────┐
 │ gkNextEngine  (Scene/Node, 程序化几何, 物理, 渲染, 反射)           │
 └──────────────────────────────────────────────────────────────────┘
```

**实现语言决策：C++。** 理由：核心依赖（`FNavGrid`、`FAIService`、`FAgentLoop`）都是 C++ 引擎层，而 QuickJS **目前没有 AIService 绑定**（见 `QuickJSBindings.md` 的 module surface）。`CharacterDemo` 正是"C++ GameInstance + 复用 NextGameplay"的范例，直接照搬其分层。
> 扩展方向：若后续希望策划用 TS 调玩法数值/日程，可单独给 QuickJS 加 `AI.GenerateAsync()` 绑定，但不在 MVP 内。

---

## 4. 复用的引擎设施清单（真实 API 映射）

> 这一节是实现 agent 的"零件库"。每一项都已在代码中验证存在，给出文件位置。

### 4.1 本地 LLM —— `NextAI::FAIService`

- 获取：`engine.GetAIService()` → `NextAI::FAIService*`（见 [Engine.hpp:112](../../src/Engine/Runtime/Engine.hpp)）。
- 切到本地：`SwitchProvider(NextAI::EAIProviderType::LocalLlama)`；配置在 [`assets/configs/ai_config.json`](../../assets/configs/ai_config.json) 的 `localllm` 段（`endpoint: 127.0.0.1:8765`，`autoDiscoverPid: true` 会读 `external/llm/run/server.pid`）。
- 调用（MVP 主路径）：`GenerateTextAsync(prompt, callback)` —— **异步**，回调在内部线程触发；NPC 决策、晨会生成目标、目标分解、结算都走这个，避免阻塞渲染线程。同步版 `GenerateText(prompt)` 仅供单测/工具脚本。
- 多轮 + 工具（扩展路径）：`Chat(FChatRequest)` / `ChatStream(...)`，`SupportsTools()`。见 [AIService.hpp](../../src/Modules/NextAI/AIService.hpp)、[AIChat.hpp](../../src/Modules/NextAI/AI/AIChat.hpp)。
- 启动本地 server：`gnb llm serve`（详见 AGENTS.md "Local LLM"），当前模型 `gemma-4-E4B-it (Q4_K_M)`。

### 4.2 多步 Agent 循环（扩展用）—— `NextAI::FAgentLoop`

- `FAgentLoop::Run(seed, tools, provider, options, sink, mainThread, cancelFlag)` → `FAgentResult`（`finalContent` / `transcript`）。见 [AgentLoop.hpp](../../src/Modules/NextAI/AI/AgentLoop.hpp)。
- 工具实现 `IAITool`（`RequiresMainThread()` → 改场景的工具会被 marshal 回主线程），注册进 `FToolRegistry`。见 [IAITool.hpp](../../src/Modules/NextAI/AI/IAITool.hpp)。
- `FAgentLoop::ParseFallbackToolCalls(content)`：从 ```json fence / 裸对象里**容错解析** JSON —— **MVP 解析 LLM 决策/目标 JSON 时直接复用这套思路**。
- **MVP 取舍**：先**不**用 FAgentLoop / 工具调用（每步多次往返、慢、贵 token）。用 4.1 的 `GenerateTextAsync` + 强约束 prompt + 容错 JSON 解析。FAgentLoop 留给"员工自主用工具查信息/改场景"的进阶版。

### 4.3 SCAD 语义点位 —— loader 的 `sceneNode.name`

- SCAD 的**每个 user module 调用实例 → 一个逻辑 `Node`，节点名 = module 名**（见 SCADLoader.md §"几何→模型"；实现 [FScadLoader.cpp](../../src/Modules/ScadLoader/FScadLoader.cpp) 的 `sceneNode.name`）。
- 因此：在 `office.scad` 里把功能点位写成**命名约定化的 module 调用**，加载后即可在场景里按名字找到带世界坐标的锚点。详见 §6。
- 验证 SCAD：`gnb shot --target StudioSim --scene assets/scad/office.scad`（隐藏窗口、自动截图、自动退出）。

### 4.4 场景 / 节点查询 —— `Assets::Scene` & `Assets::Node`

- `scene.Nodes()` → `std::vector<std::shared_ptr<Node>>&`：遍历全部节点，**按名字前缀**筛出 POI（见 [Scene.hpp:77](../../src/Engine/Assets/Core/Scene.hpp)）。
- `scene.GetNode(name)` / `GetNodeById(id)` / `FindNodeIdWithComponent(type)`；`GetNodeBounds(nodeId, center, radius)` 拿世界包围球。
- `Node`：`GetName()`、`WorldTranslation()` / `WorldTransform()`、`SetTranslation/Rotation/Scale`、`RecalcTransform()`、`AddComponent<T>()`、`GetComponentByTypeName()`。见 [Node.h](../../src/Engine/Assets/Core/Node.h)。
- 运行时建节点：`Node::CreateNode(name, t, r, s)`。

### 4.5 导航 —— `NextGameplay::FNavGrid` + `FPathFollower`

- `FNavGrid::Build(bvh, FNavGridSettings)`：用场景 CPU BVH 朝下射线采样可走性（地面高度/坡度/净空）。`FindPath(from, to, refHeight)` → A* + 平滑路径。`BuildReachabilityMask(from, refH)`、`RebuildDirtyRegion(...)`。见 [NavGrid.h](../../src/Gameplay/AI/NavGrid.h)。
- `FPathFollower`：`SetPath` / `GetMoveDirection` / `NeedsRepath`（header-only，[PathFollower.h](../../src/Gameplay/AI/PathFollower.h)）。
- CPU BVH 来自 `Assets::CPU::FCPUAccelerationStructure`（CharacterDemo 在 `OnSceneLoaded` 里 `KeepCPUMeshData=true` 后 Build——照抄）。

### 4.6 简单几何体表示员工 —— 程序化模型

- `Assets::FProcModel::CreateBox(...)` 生成程序化盒子；`Assets::SceneBuilder::AddLambertianMaterial(materials, glm::vec3 color)` 加材质返回 matId。**范例**：[CharacterDemoGameInstance.cpp:177-188](../../src/Application/Game/CharacterDemo/CharacterDemoGameInstance.cpp)（注入胶囊占位 + 着色）。
- 员工 = 一个根 `Node`（移动） + 一个 box/capsule 可视子节点；职位用材质颜色区分；头顶气泡用 ImGui 世界→屏幕投影画文字（见 §12 / `OnRenderUI`）。

### 4.7 GameInstance 生命周期 —— `NextGameInstanceBase`

钩子（照搬 CharacterDemo 的 override 集）：`OnInit` / `OnTick(double dt)` / `OnDestroy` / `ApplyDefaultCVars` / `BeforeSceneRebuild(nodes, models, materials, ...)` / `OnSceneLoaded` / `OnSceneUnloaded` / `OnRenderUI` / `OverrideRenderCamera` / `OnKey/OnCursorPosition/OnMouseButton/OnScroll` / App debug shortcuts。见 [CharacterDemoGameInstance.hpp](../../src/Application/Game/CharacterDemo/CharacterDemoGameInstance.hpp)。

### 4.8 反射（可选）—— `REFLECT_COMPONENT`

若把 `EmployeeComponent` 做成 ECS 反射组件，可在编辑器 PropertyPanel 调参/调试。见 [ReflectionSystem.md](../../AGENT_GUIDE/ReflectionSystem.md)。MVP 可先用纯 C++ 结构体（像 `CharacterDemoConfig`），不强制反射。

---

## 5. 关键技术约束与对策 ★★（最重要的一节）

### 5.1 头号约束：本地 LLM 是**单实例、串行**的

一个 llama-server 进程，**同一时刻只能跑一个推理**；一次几十 token 的 JSON 决策在桌面 GPU 上约 **0.3–2s**。若 8 个员工每帧都问 LLM，会瞬间卡死。

**对策（决策调度器，§9 详述）：**
1. **离散决策，不是每帧决策**：每个员工只在"决策时刻"问 LLM（动作做完、被事件打断、或日程到点），平时按已决定的动作用 NavGrid 自走。
2. **串行队列 + 每秒预算**：全局一个决策队列，**同一时刻最多 1 个在途 LLM 请求**；每真实秒最多发起 `N`（默认 1–2）个新决策，给玩家事件留余量。
3. **异步**：用 `GenerateTextAsync`，回调里解析结果并写回（回调可能在工作线程 → 用线程安全队列把结果交回主线程 `OnTick` 应用，**禁止在回调里直接改 Scene/Node**）。
4. **软实时时钟**：模拟时钟与真实时钟解耦（§5.2）；员工"思考中"时头顶显示 `…` 气泡，遮盖延迟。
5. **目标层的 LLM 调用是 O(1)/天**：晨会生成目标、目标分解、结算各只调一次，且发生在时钟暂停的 Briefing/Review 阶段，不与逐员工预算抢资源（§10）。

### 5.2 模拟时钟 / 真实时钟解耦

- 游戏内一天（09:00–18:00）压缩到几分钟真实时间（默认 `1 真实秒 = 2 游戏分钟`，可调）。
- 时钟只在 **Working 阶段**推进，且只决定"日程到点 / 决策时刻"的触发；**LLM 延迟不冻结渲染**，员工照常用上一个动作移动。
- Briefing/Review 阶段暂停时钟，等玩家操作 / 看结算。可暂停（玩家观察）、可加速（跳过无聊段）。

### 5.3 结构化输出 + 确定性 fallback（Demo 永不卡死）

- LLM 被要求**只输出 JSON**（决策 §8 / 目标 §10 各有 schema）。解析用 `FAgentLoop::ParseFallbackToolCalls` 同款容错（剥 ```json fence / 找首个 `{...}` 或 `[...]`）。
- **任一失败**（server 未起、超时、JSON 解析失败、字段非法、目标 POI 不存在）→ 退回确定性 fallback：
  - 员工决策失败 → 该员工走**脚本化默认日程**（§7 状态机）。
  - 晨会目标生成失败 → 用 `studio_sim.json` 的**预置目标库**随机给 3 个。
  - 目标分解失败 → 用"职位×目标类别 → 默认任务"的**静态映射表**。
  - 结算失败 → 用**启发式评分**（统计员工在目标相关 POI/任务上的耗时）。
- 先实现确定性层（M3 脚本日程、目标 fallback 库）、再叠 LLM 层，保证**任何时候拔掉 LLM 仍能演示**。

### 5.4 其它约束

- **Token / 上下文**：每次决策 prompt 控制在 ~300–600 token（角色卡精简 + 今日目标 + 个人重点 + 相邻同事 + 当日事件摘要），别把整段历史塞进去。
- **平台**：本地 LLM 仅桌面；MVP 只做 Windows。
- **LOC 预算**：引擎目标 <50k 首方 LOC，本原型作为 Application 子项目应精简（参考 Brotato3D/CharacterDemo 体量），复用优先。

---

## 6. SCAD 办公室场景规范

### 6.1 语义命名约定（点位即锚点）

在 `assets/scad/office.scad` 里，**凡是有玩法意义的点位都封装成命名 module 调用**，加载后节点名即语义标签。约定前缀：

| 前缀 | 含义 | 示例节点名 | 玩法用途 |
| --- | --- | --- | --- |
| `desk_<role>_<id>` | 工位（按职位） | `desk_engineer_01` | 该职位员工的"工作"锚点 |
| `meet_seat_<id>` | 会议室座位 | `meet_seat_03` | 开会/评审聚集点 |
| `pantry_<id>` | 茶水间点位 | `pantry_01` | 休息/闲聊/补状态 |
| `lounge_<id>` | 洽谈室点位 | `lounge_01` | 1v1 谈话 |
| `nav_<id>` | 走道路点（可选） | `nav_07` | 引导 A* 的中转点 |
| `door_<id>` | 门 | `door_main` | 入口/动线节点 |
| `zone_<name>` | 区域中心（可选） | `zone_openspace` | 区域归属/相机焦点 |

> 约定写进 `office.scad` 顶部注释。解析时 `OfficeMap` 只认这些前缀，其余几何（墙/地/装饰）忽略为静态环境。

### 6.2 示例 SCAD 片段

```openscad
// office.scad —— 命名约定见 docs/plans/studiosim-mvp-plan.md §6
$fn = 24;

module desk_proto()  { color("LightGray") cube([1.2, 0.75, 0.6], center=true); }
module meet_proto()  { color("SteelBlue") cylinder(h=0.5, r=0.25); }
module pantry_proto(){ color("Khaki") cube([0.8,0.9,0.8], center=true); }
module lounge_proto(){ color("Plum") cube([1.0,0.5,1.0], center=true); }

// 一点位一具名 module：节点名 = 被调用的 module 名（见 §6.3）
module desk_engineer_01() desk_proto();
module desk_engineer_02() desk_proto();
module desk_artist_01()   desk_proto();
module desk_designer_01() desk_proto();
module desk_pm_01()       desk_proto();
module meet_seat_01()     meet_proto();
module meet_seat_02()     meet_proto();
module pantry_01()        pantry_proto();
module lounge_01()        lounge_proto();

translate([-4, 0, -2]) desk_engineer_01();
translate([-4, 0,  0]) desk_engineer_02();
translate([-2, 0, -2]) desk_artist_01();
translate([ 0, 0, -2]) desk_designer_01();
translate([ 2, 0, -2]) desk_pm_01();
translate([ 6, 0,  4]) meet_seat_01();
translate([ 7, 0,  4]) meet_seat_02();
translate([-6, 0,  5]) pantry_01();
translate([ 6, 0, -5]) lounge_01();
```

### 6.3 实现注意（SCAD → 锚点）

- **节点名来自被调用的 user module 名**。要让节点叫 `desk_engineer_01`，最稳的写法是**一点位一具名 module**（如上）。实现 M1 时**先用一个最小 scad 验证 `scene.Nodes()` 里拿到的实际名字**（`gnb run StudioSim --load-scene ...` 后打印所有节点名），再定最终命名风格。
- 锚点世界坐标：`node->WorldTranslation()`（确保 `OnSceneLoaded` 里已 `RecalcTransform`）。
- 已知 SCAD 限制（见 SCADLoader.md）：`mirror`/一般仿射会被分解到 TRS，极端 shear 可能失真——办公室是规则盒子布局，不受影响。

---

## 7. 数据模型（C++ struct 草图）

> 放 `src/Application/Game/StudioSim/StudioSimTypes.h`。先用普通结构体，必要时再升级为反射组件。

```cpp
namespace StudioSim
{
    enum class ERole { Engineer, Artist, Designer, ProducerPM, QA, Boss };
    enum class EAction { Idle, GotoWork, Working, GotoTalk, Talking, GotoPantry, Resting, React, Meeting };
    enum class EMood   { Calm, Focused, Stressed, Excited, Bored, Panicked };
    enum class EDayPhase { Briefing, Working, Review };   // 一天的阶段机

    // 从 SCAD 解析出的功能点位
    struct FPointOfInterest
    {
        std::string name;        // 节点名，如 "desk_engineer_01"
        std::string category;    // "desk"/"meet"/"pantry"/"lounge"/"nav"/"door"
        std::string roleTag;     // desk 专属职位标签，可空
        glm::vec3   worldPos{};
        uint32_t    nodeId = 0;
        bool        workable = true;  // 断电/宕机等会置 false
        uint32_t    occupiedBy = 0;   // 占用的员工序号，0=空
    };

    // 今日团队目标（玩法核心，见 §10）
    struct FDailyGoal
    {
        std::string title;        // "赶在竞品前发布可玩 demo"
        std::string description;  // 给 LLM 的情境扩展
        std::string source;       // "llm_option" | "player_custom" | "fallback"
        std::vector<ERole> focusRoles;            // 重点参与职位
        std::map<ERole, std::string> roleTasks;   // 目标分解：职位 → 今日重点(一句话)
        float progress = 0.0f;    // 结算评分 0..1（Review 阶段写）
        bool  set = false;        // 是否已确定
    };

    // 角色卡（静态人设，来自 JSON 配置）
    struct FEmployeeCard
    {
        std::string id;          // "alice"
        std::string displayName; // "Alice"
        ERole       role = ERole::Engineer;
        std::string personality; // "话痨、乐观、爱摸鱼"
        std::string homeDeskPoi; // "desk_engineer_01"
        glm::vec3   color{};     // 几何体颜色
    };

    // 运行态
    struct FEmployeeRuntime
    {
        FEmployeeCard card;
        std::shared_ptr<Assets::Node> rootNode;   // 移动根
        EAction  action = EAction::Idle;
        EMood    mood   = EMood::Calm;
        std::string currentPoi;       // 当前所在点位
        std::string targetPoi;        // 寻路目标
        std::string todayTask;        // 来自目标分解的个人重点（喂进决策 prompt）
        std::string bubbleText;       // 头顶气泡
        float    actionTimer = 0.0f;  // 当前动作剩余（游戏分钟）
        bool     decisionPending = false; // 是否有在途 LLM 请求
        NextGameplay::FPathFollower follower;
        std::vector<std::string> shortMemory; // 最近 N 条经历摘要（喂回 prompt）
    };

    struct FWorldEvent
    {
        std::string id;          // "competitor_launch"
        std::string title;       // "竞争对手发布了新游戏"
        std::string description; // 喂给 LLM 的情境文本
        EMood       moodBias = EMood::Stressed; // 倾向影响
        double      gameTimeRaised = 0.0;
    };

    struct FWorldState
    {
        EDayPhase phase = EDayPhase::Briefing;
        int       dayIndex = 0;
        double    gameClockMinutes = 9 * 60; // 09:00
        float     timeScale = 2.0f;          // 1 真实秒 = N 游戏分钟
        bool      paused = false;
        FDailyGoal today;                    // 今日目标
        std::string globalMood;              // "紧张"/"日常"/"庆祝"
        std::vector<FWorldEvent> todaysEvents;
    };

    // 一次员工决策结果（调度器与 LLM 之间）
    struct FDecisionResult   // = LLM 决策 JSON 解析后的结构
    {
        EAction action = EAction::Idle;
        std::string targetPoi;       // GOTO/WORK/MEETING/REST
        std::string targetEmployee;  // TALK
        std::string dialogue;        // 头顶气泡
        EMood   mood = EMood::Calm;
        int     durationMinutes = 30;
        bool    valid = false;       // 解析+校验是否通过
    };

    // 晨会候选目标（LLM 给的一项）
    struct FGoalOption
    {
        std::string title;
        std::string description;
        std::vector<ERole> focusRoles;
    };
}
```

角色卡、初始日程、**预置目标库**、**目标分解静态映射**放 `assets/configs/studio_sim.json`（员工列表 + 默认日程 + 事件库 + fallback 目标/任务），可热改、便于策划调。

---

## 8. LLM 决策协议 ★

### 8.1 调用方式

MVP 用 `FAIService::GenerateTextAsync(prompt, callback)`，**单次**生成、强约束输出 JSON。不走多轮/工具（见 §4.2 取舍）。

### 8.2 员工决策 Prompt 模板（中文，Gemma 多语言友好）

```
[System]
你在模拟一家游戏工作室的办公室。你扮演一名员工，根据人设、今日团队目标、当前情境，决定接下来做一件事。
你的决策应优先推进【今日团队目标】，同时符合你的职位与性格。
只输出一个 JSON 对象，不要任何额外解释或 markdown。

[人设]
姓名：{name}（{role}）。性格：{personality}。工位：{homeDesk}。当前情绪：{mood}。

[今日团队目标]
{goal_title} —— {goal_description}
你的今日重点：{my_today_task}        // 来自目标分解(§10.3)，无则"按本职推进目标"

[此刻]
游戏时间：{HH:MM}。你在：{currentPoi}。
今天发生的大事：{events_summary}      // 玩家注入的事件摘要，无则"暂无"
你附近的同事：{nearby_list}           // 名字+职位+情绪，最多 3 个
最近经历：{short_memory}              // 最近 3 条一行摘要

[可去的点位]
{poi_menu}   // 形如: desk_* / meet_seat_* / pantry_01 / lounge_01

[输出 JSON 字段]
{
  "action": "GOTO|WORK|TALK|REST|MEETING|REACT|IDLE",
  "target_poi": "<点位名，GOTO/WORK/MEETING/REST 用；否则空字符串>",
  "target_employee": "<同事名，TALK 用；否则空字符串>",
  "dialogue": "<≤20字的一句话，会显示在你头顶；没话说就空>",
  "mood": "calm|focused|stressed|excited|bored|panicked",
  "duration_minutes": <10~60 的整数>
}
```

### 8.3 输出 JSON 校验与降级

解析（复用 `ParseFallbackToolCalls` 思路）后逐字段校验：

1. `action` 必须是枚举之一；非法 → 整体降级到脚本日程。
2. `target_poi` 必须在 `OfficeMap` 已知点位里且 `workable`；不存在/不可用 → 降级（或就近合法点位）。
3. `target_employee` 必须是现存员工；不存在 → 退化为 `IDLE`。
4. `mood` 非法 → 保持原 mood。
5. `duration_minutes` clamp 到 [10, 60]。
6. 全部失败/超时（>3s）→ **该员工本次走脚本日程**（以 `todayTask` 为主线），并把 `bubbleText` 设为状态占位。

校验通过后，主线程在 `OnTick` 里 apply：设 `targetPoi` → `FNavGrid::FindPath` → `FPathFollower::SetPath`；`dialogue` → 气泡；`mood` → 颜色微调 + 写入下次 prompt。

### 8.4 记忆（轻量）

`FEmployeeRuntime::shortMemory` 保留最近 3–5 条一行摘要（"09:30 和 Bob 在茶水间聊了竞品"），拼进下次 prompt 的 `[此刻]` 末尾。不做向量库（MVP 外）。

---

## 9. 决策调度器设计 ★

`DecisionScheduler`（`src/Application/Game/StudioSim/StudioSimDecisionScheduler.{h,cpp}`）—— 把"串行的 LLM"变成"能服务一屋子人"的核心。

### 9.1 数据流与线程模型

```
主线程 OnTick (仅 Working 阶段):
  1. 推进时钟，标记"到决策时刻"的员工 → 入决策队列(去重)
  2. 若 (在途请求数 < 1) 且 (本秒预算未用完) 且 队列非空:
        取队首员工 → 组 prompt(带今日目标+个人任务) → GenerateTextAsync(prompt, cb)
        该员工 decisionPending = true; 在途++; 本秒预算--
  3. 排空 completedResults_（线程安全队列）：
        对每个结果 → 校验/降级 → apply 到员工 → decisionPending=false; 在途--

工作线程 (GenerateTextAsync 回调):
  解析 JSON → FDecisionResult → completedResults_.push(...)   // 只入队，不碰 Scene
```

- **在途上限 = 1**（与单 server 串行匹配；若将来多 server 可调大）。**晨会/分解/结算的 LLM 调用也共用这条串行通道**，但它们发生在时钟暂停阶段，不与逐员工决策并发。
- **每秒预算**：`maxDecisionsPerRealSecond`（默认 2），用真实时间累加令牌桶，给玩家事件抢占留余量。
- **去重**：员工已有在途请求时不重复入队。
- **优先级**：被玩家事件直接波及的员工 > 日程到点的员工 > 闲逛随机决策。事件注入时把相关员工**插队**到队首（§11）。
- **线程安全交回主线程**：回调只 `push` 到 `std::mutex` 保护的 `completedResults_`；所有 Scene/Node 改动只在主线程第 3 步做。（与 `FMainThreadDispatcher` 同一种"marshal 回主线程"的纪律，但 MVP 自己用一个简单队列即可，不必引入 dispatcher。）
- **取消**：员工被卸载/场景切换/换天时，用 `decisionPending` + 一个 epoch 计数器丢弃过期回调结果。

### 9.2 决策时刻的触发条件

- 当前动作 `actionTimer` 归零（一件事做完）。
- 日程边界 / 个人任务节点（站会、午饭、下班）。
- 被玩家事件波及（插队）。
- 到达目标点位且需要决定"接下来"。
- 闲逛兜底：太久没决策的员工，低优先级补一次。

---

## 10. 每日目标系统 ★★（玩家定义一天，驱动全员）

`GoalSystem`（`src/Application/Game/StudioSim/StudioSimGoalSystem.{h,cpp}`）—— 把"一天"从无目的漫游升级为**围绕玩家设定的目标协作**。这是用户强调的核心：**每个员工的工作安排、后续驱动逻辑都根据今日目标展开**。

### 10.1 一天的阶段机（`EDayPhase`）

```
Briefing(晨会, 时钟暂停) ──玩家定目标──► Working(模拟一天) ──18:00──► Review(结算) ──► 下一天
```
- 时钟只在 **Working** 推进；Briefing/Review 暂停，等玩家操作。
- `dayIndex` 递增；MVP 默认演示一天，多天演化留扩展（§17）。

### 10.2 晨会：LLM 给 3 个目标 + 玩家自定义

1. 进入 Briefing：`GoalSystem` 调一次 LLM（**目标生成 prompt**），输入：团队构成（职位清单）、公司近况（昨日结算/未决事件，首日为空）。
2. 要求输出 **3 个候选目标的 JSON 数组**：
   ```json
   [
     {"title":"赶在竞品前发布可玩 demo","description":"...","focus_roles":["Engineer","Designer"]},
     {"title":"修复线上严重崩溃","description":"...","focus_roles":["Engineer","QA"]},
     {"title":"做一次新玩法头脑风暴","description":"...","focus_roles":["Designer","Artist","ProducerPM"]}
   ]
   ```
3. UI 弹出**「今日目标」面板**（见 §12）：3 张候选卡片 + 一个**自由文本输入框**（玩家自定义）+ 确认按钮。
4. 玩家选一张或输入自定义 → 写入 `FWorldState::today`（`source` 标 `llm_option`/`player_custom`）。
5. **Fallback**：LLM 不可用/解析失败 → 从 `studio_sim.json` 的预置目标库随机取 3 个（`source=fallback`）。玩家自定义路径始终可用，与 LLM 无关。

### 10.3 目标分解到角色（"每个员工的安排根据目标展开"）

- 目标确定后，`GoalSystem` 再调一次 LLM（**目标分解 prompt**）：输入今日目标 + 各员工职位，输出"每个职位今天的重点任务（一句话）"的 JSON map，写进对应 `FEmployeeRuntime::todayTask` 与 `FDailyGoal::roleTasks`。
  ```json
  {"Engineer":"把 demo 关卡的核心循环跑通","Designer":"定 demo 的引导流程","Artist":"出 demo 首屏美术","ProducerPM":"协调进度并对外口径","QA":"准备 demo 冒烟测试用例"}
  ```
- **一天一次、一次调用**（不是每人一次），符合 §5.1 的 LLM 预算。发生在时钟暂停的 Briefing 阶段。
- **Fallback**：不调/失败 → 用 `studio_sim.json` 的"职位 × 目标类别 → 默认任务"**静态映射表**。
- 之后每个员工的决策 prompt（§8.2）都带 `[今日团队目标]` + 自己的 `todayTask`，决策"优先推进目标"。

### 10.4 目标贯穿驱动（与 §8/§9 衔接）

- §8 prompt 注入今日目标 + 个人重点任务。
- §9 调度器的"日程到点"决策，现在以**目标分解出的个人任务**为日程主线（替代纯固定脚本）。
- 脚本 fallback 层（M3）在有目标时按 §10.3 的静态映射给任务；尚未实现 M5（无目标）时退回 M3 的固定日程。
- 不同目标 → 不同行为分布（"赶 demo"→ 工程师久坐工位、PM 频繁串场；"头脑风暴"→ 多人聚会议室）。这是验收 M5 的肉眼信号。

### 10.5 结算（Review，MVP 简化）

- 一天结束（18:00）进入 Review：汇总各员工今天的行动摘要 → 调一次 LLM（**结算 prompt**）→ 输出一句"目标达成情况"总结 + `0–100` 评分，写进 `FDailyGoal::progress` 与事件日志。
- **Fallback**：用启发式（统计员工在目标相关 POI/`todayTask` 上花的时间占比）给粗评分。
- Review 结果作为**明天 Briefing 的输入**（多天演化的钩子，MVP 不强制连续多天）。

### 10.6 与事件系统耦合

- 玩家随机事件（§11）被框定为**对今日目标的冲击**：prompt 里今日目标与当日事件并列，员工反应自然围绕"目标受阻 / 重排计划"。
- 例：目标=赶 demo，事件=版本服务器宕机 → 工程师恐慌聚集排查、PM 重新评估目标可行性、其他职位等待 —— 戏剧张力来自"目标 × 障碍"。

---

## 11. 玩家事件系统

`EventSystem`（`StudioSimEventSystem.{h,cpp}`）+ ImGui 面板按钮。事件的意义是**冲击当日目标**（§10.6）。

### 11.1 MVP 事件库（`studio_sim.json` 可配）

| id | 标题 | 注入后的世界影响 | 期望的 LLM 反应（叠加今日目标） |
| --- | --- | --- | --- |
| `competitor_launch` | 竞争对手发布新游戏 | `globalMood="紧张"`；moodBias=Stressed | 聚会议室讨论是否调整目标、加班语气 |
| `power_outage` | 公司断电 | 工位 `workable=false`；moodBias=Bored | 离开工位、去茶水间闲聊、抱怨目标推进受阻 |
| `build_server_down` | 版本服务器宕机 | 工程师工位"阻塞"；moodBias=Panicked | 工程师聚集排查、PM 重排目标、其他人等待 |

### 11.2 注入流程

1. 玩家点面板按钮 → `EventSystem::Raise(eventId)`。
2. 写入 `FWorldState::todaysEvents`，更新 `globalMood`、给受影响 POI 打标记（如断电置 `workable=false`）。
3. 把**受影响员工**插队进决策队列（高优先级），并清掉其当前动作（强制重决策）。
4. 后续每次组 prompt 都带 `events_summary` + 今日目标 → LLM 自然把"事件 vs 目标"纳入考量，产生**链式影响**（断电→去茶水间→遇同事→触发对话→情绪扩散）。
5. 事件在事件日志面板留一行（带游戏时间戳）。

---

## 12. 员工间交互 & 表现层

### 12.1 相遇对话

- 两个员工同时在同一点位（或距离 < 阈值）且都 `Idle/Resting` → 触发一次**配对对话**：
  - MVP 简化：发起方的下一次决策若 `action=TALK` 且目标在身边，就生成一句 `dialogue` 显示气泡；被搭话方下次决策的 prompt 里加入"刚才 X 对你说了：…"，形成一来一回。
  - 不做实时多轮群聊（MVP 外）。
- 会议：目标（如"头脑风暴"）或事件触发"开会"，相关员工 `action=MEETING` → 寻路到 `meet_seat_*`，聚齐后短暂停留 + 轮流冒泡。

### 12.2 表现层（`StudioSimUI` + `OnRenderUI`）

- **头顶气泡**：把 `node->WorldTranslation()` 投影到屏幕（相机矩阵），ImGui 在该屏幕坐标画 `displayName` + `bubbleText` + 情绪 emoji/颜色。决策在途显示 `…`。
- **今日目标面板**（Briefing 弹出）：3 张候选卡 + 自定义输入框 + 确认；确认后在 HUD 常驻显示当前目标标题。
- **HUD**：当前游戏时间、阶段（晨会/工作/结算）、时钟暂停/调速、`globalMood`、今日目标、在途决策数（调试用）。
- **事件面板**：3 个事件按钮 + 事件日志滚动列表（含结算总结）。
- **调试 overlay**（App debug shortcut，仿 CharacterDemo 的 F8）：画 POI 锚点、NavGrid、员工目标连线、最近一次 prompt/响应文本、各员工 `todayTask`。

---

## 13. 代码结构 / 文件骨架

参照 CharacterDemo 分层（编排 GameInstance + 若干系统文件 + 纯 C++ 配置）。新建 `src/Application/Game/StudioSim/`：

```
src/Application/Game/StudioSim/
├── StudioSimGameInstance.{hpp,cpp}     # NextGameInstanceBase 子类，编排 OnTick 主循环 + 阶段机
├── StudioSimTypes.h                    # §7 的结构体/枚举
├── StudioSimConfig.hpp                 # 可调数值（时钟/预算/阈值），纯 C++（仿 CharacterDemoConfig）
├── OfficeMap.{h,cpp}                   # SCAD 节点 → POI 表；按前缀分类；点位查询/占用/workable
├── EmployeeSystem.{h,cpp}              # 员工生成(几何体)、移动(NavGrid/PathFollower)、动作执行
├── StudioSimGoalSystem.{h,cpp}         # §10 晨会3选项+玩家自定义+目标分解+结算+EDayPhase 阶段机
├── StudioSimDecisionScheduler.{h,cpp}  # §9 调度器 + prompt 组装 + JSON 解析/校验
├── StudioSimEventSystem.{h,cpp}        # §11 事件注入 + 世界状态
├── StudioSimUI.{h,cpp}                 # §12 气泡/HUD/目标面板/事件面板/调试 overlay
└── StudioSimMain.cpp                   # 入口（注册 GameInstance 并启动，仿其它 game target）

assets/
├── scad/office.scad                    # §6 办公室场景
└── configs/studio_sim.json             # 员工角色卡 + 日程 + 事件库 + 预置目标库 + 目标分解静态映射
```

构建接入：
- 在 [`src/CMakeLists.txt`](../../src/CMakeLists.txt) 加 `StudioSim` 可执行目标，`links` → `NextGameplay` → `gkNextEngine`（仿 CharacterDemo 的 target 定义）。
- [`assets/CMakeLists.txt`](../../assets/CMakeLists.txt) 已拷贝 `scad/`、`configs/`，新增文件随之带走（必要时 `--reconfigure`）。
- 改了 `NextGameplay` 才需连带构建 `gkNextUnitTests`；本原型只**消费**不改 NextGameplay，则只 `gnb build StudioSim`。

---

## 14. 里程碑拆解（M0–M7）★★

> 每个里程碑**独立可验证**、可作为 `.spec/TODO.md` 的一个任务条目。**M3 先于 M4、M4 先于 M5**——确定性脚本层是 LLM 层的安全网，目标系统依赖 LLM 调度器就绪。

### M0 — Target 脚手架 + 空场景跑通
- 建 `StudioSim` target（CMake + `StudioSimGameInstance` 骨架，override 集照搬 CharacterDemo）。
- 加载占位场景（先用现成 `assets/scad/` 任意文件或 playground），俯视相机。
- **验证**：`gnb build StudioSim` 通过；`gnb run StudioSim` 日志出现 `uploaded scene [...] to gpu`。

### M1 — `office.scad` + 语义锚点提取（`OfficeMap`）
- 写 `office.scad`（§6 命名约定）。`OfficeMap` 遍历 `scene.Nodes()` 按前缀解析出 `FPointOfInterest` 表（类型 + `WorldTranslation`）。
- 先**打印所有节点名**确认 SCAD 命名落地（§6.3），再定最终前缀。调试 overlay 画出锚点。
- **验证**：`gnb shot --target StudioSim --scene assets/scad/office.scad`；日志打印解析到的 POI 数与名字。

### M2 — 员工实体 + NavGrid 移动（`EmployeeSystem`）
- `OnSceneLoaded`：`KeepCPUMeshData=true` → 建 CPU BVH → `FNavGrid::Build`。
- 从 `studio_sim.json` 读 5–8 个角色卡，每人生成简单几何体（`FProcModel::CreateBox` + 职位色）放到 `homeDesk` 附近。
- 实现"去某 POI"：`FindPath` + `FPathFollower` 驱动 `rootNode`；先用**随机巡游**验证移动。
- **验证**：`gnb run StudioSim` 看到员工在点位间走动、不穿墙。

### M3 — 阶段机 + 模拟时钟 + 脚本化日程（确定性 fallback 层）
- `EDayPhase` 阶段机骨架 + `FWorldState` 时钟（加速/暂停/调速）。每员工一条脚本日程（09:00 工位→12:00 茶水间→…）。
- 决策时刻：到点 → 切对应 POI → 移动 → 停留。**完全不调 LLM 也能演示一天**。
- HUD 显示时间/阶段 + 事件日志骨架。
- **验证**：`gnb run StudioSim` 跑完加速"上午"，员工按日程换位置；§5.3 的安全网。

### M4 — LLM 决策调度器（核心基础设施）
- `DecisionScheduler`（§9）：串行队列 + 每秒预算 + `GenerateTextAsync` 异步 + 线程安全回收。
- 员工决策 prompt 组装（§8.2，先不含目标）+ JSON 解析/校验（§8.3），失败**降级到 M3 脚本日程**。
- 切 `LocalLlama` provider（`gnb llm serve` 先起 server）。气泡显示 `dialogue` / `…`。
- **验证**：起本地 server 后 `gnb run StudioSim`，员工动作/对话由 LLM 驱动且有差异；停掉 server 仍能跑（走 M3）。日志可见决策耗时/在途数。

### M5 — 每日目标系统（玩家定义一天，驱动全员）★
- `GoalSystem`（§10）：Briefing 晨会调 LLM 给 **3 个目标**（fallback 预置库）+ **「今日目标」面板**（三选一 + **自由输入自定义**）。
- **目标分解**到各职位 `todayTask`（一次 LLM 调用 / fallback 静态映射）。
- 决策 prompt 注入今日目标 + 个人任务（§8.2 完整版）；员工行为随目标变化。
- 18:00 进入 Review 给**目标达成总结 + 评分**（LLM / 启发式 fallback）。
- **验证**：`gnb run StudioSim` → 晨会能看到 3 个目标并三选一或自定义 → 不同目标下员工行为分布明显不同（如"头脑风暴"多人聚会议室）→ 18:00 出总结；停掉 LLM 用 fallback 仍完整跑通。

### M6 — 玩家事件注入 + 冲击目标（`EventSystem`）
- 事件面板 3 按钮（竞品/断电/宕机）→ `Raise` → 改 `FWorldState` + 受影响 POI 标记 + 受影响员工插队重决策。
- prompt 带 `events_summary` 与今日目标并列，观察"事件 × 目标"反应。
- **验证**：注入"竞品发布"→ 数秒内多名员工情绪变 Stressed、向会议室聚集讨论是否调整目标；事件日志记录。

### M7 — 员工间交互（对话 / 会议）
- 相遇配对对话（§12.1）：一句话气泡 + 写进对方 `shortMemory` 形成来回。
- 会议聚集：目标/事件触发 `MEETING` → 聚到 `meet_seat_*`。
- **验证**：观察到至少一次"A 说一句 → B 回应"的可读对话链，且情绪在相邻员工间扩散。

**横切（贯穿各里程碑）**：确定性 fallback（M3/M5 起）、性能预算与在途上限（M4）、调试 overlay（M1 起逐步加）、`studio_sim.json` 配置驱动（M2 起）。

---

## 15. 验证与调试

- **构建**（targeted，别全量）：`gnb build StudioSim`（改了 NextGameplay 才 `gnb build StudioSim gkNextUnitTests`）。
- **运行**：`gnb run StudioSim`，看 `uploaded scene [...] to gpu`。
- **场景快验**：`gnb shot --target StudioSim --scene assets/scad/office.scad`（隐藏窗口、自动截图到 `out/build/<preset>/screenshots/agent_validation.jpg`、自动退出）——改 SCAD 布局/锚点用这个肉眼看。
- **本地 LLM**：`gnb llm serve` 起 server；`gnb llm status` 看模型；`external/llm/run/server.{log,pid}` 诊断。
- **日志过滤**：决策/目标调度打 `StudioSim:` 前缀；SCAD 解析看 `SCAD:`。
- **调试热键**：仿 CharacterDemo `F8` 开调试菜单 → POI/NavGrid/决策文本/目标分解 overlay。

---

## 16. 风险与开放问题（含待用户拍板项）

| # | 风险 / 开放问题 | 倾向 / 缓解 |
| --- | --- | --- |
| R1 | SCAD 带参 module 是否产出可识别语义节点名（§6.3） | M1 先验证；默认"一点位一具名 module" |
| R2 | 单 server 串行能否撑 6–8 人有"活着"的感觉 | 调度预算 + 离散决策 + 脚本兜底；必要时减员工数 / 提速时钟 |
| R3 | Gemma-4-E4B 中文 JSON 输出稳定性（决策/目标/分解/结算 4 种） | 强约束 prompt + 容错解析 + 校验降级；目标系列调用都有 fallback |
| R4 | 异步回调线程安全（误改 Scene） | 纪律：回调只入队，主线程 apply（§9.1） |
| R5 | 气泡世界→屏幕投影与遮挡 | MVP 不做遮挡剔除，先能显示；后续加深度测试 |
| **Q1** | **target 命名**：`StudioSim` / `CrunchTime` / `OfficeLLM`？ | 默认 `StudioSim`，待定 |
| **Q2** | **MVP 员工数 / 时钟速度默认值** | 默认 6 人、`1s=2min`，待手感调 |
| **Q3** | `EmployeeComponent` 是否做成反射 ECS 组件（编辑器可调） | MVP 先纯结构体，按需升级 |
| **Q4** | 气泡 / 目标 / 对话语言 | 默认中文（与本地模型 + 场景一致） |
| **Q5** | 目标分解每天调 LLM，还是默认走静态映射、仅可选调 LLM | 默认 LLM 一次/天 + 静态 fallback，待定 |
| **Q6** | 是否做多天演化（Review 影响 next day 的 Briefing） | MVP 先单天，预留 `dayIndex` 钩子 |

---

## 17. 超出 MVP 的后续扩展

- **多天演化**：Review 评分/未决事件喂进次日 Briefing，长出连续叙事；跨天存档。
- **FAgentLoop + 工具**：让员工自主"查项目状态""改场景物件"（用 `IAITool` + `FMainThreadDispatcher`）；让 PM 角色用工具主动给同事派活。
- **目标依赖链 / KPI**：目标拆成可勾选子任务、完成度实时驱动场景反馈。
- **长期记忆 / 关系图谱**：跨决策的人物关系、好感度，向量检索。
- **骨骼角色**：换 KayKit + Mannequin（CharacterDemo 已有异步 append 模式）+ 坐下/打字动作。
- **JS/TS 玩法层**：给 QuickJS 加 AIService 绑定，策划用 TS 配日程/事件/目标库。
- **群聊涌现**：多人实时多轮会议讨论。
- **更丰富办公室**：多区域/多楼层、可破坏环境、动态工位。

---

## 18. 如何启动实现

1. 评审本文档，拍板 §16 的 Q1–Q6。
2. 建议把 §14 的 **M0–M7** 登记进 `.spec/TODO.md`（每个里程碑一条任务，复杂者在 `.spec/specs/<id>.md` 补细节）。
3. 后续 agent 按里程碑顺序实现，每个里程碑用 §15 的命令验证后写 `.spec/journal/<id>.md`。
4. 实现中遇到的 SCAD 命名/调度参数/目标 prompt 调优等经验，更新回本文档对应小节。
```
