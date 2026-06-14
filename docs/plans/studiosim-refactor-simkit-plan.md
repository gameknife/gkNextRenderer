---
title: "StudioSim 重构 + 公共仿真层（Sim Kit）抽取与开发计划"
category: plan
status: 草案
owner: engine
created: 2026-06-13
last_updated: 2026-06-13
---

# StudioSim 重构 + 公共仿真层（Sim Kit）抽取与开发计划

> **状态**：规划草案（已与需求方确认三项关键取向，见 §0.2），待后续 agent 落地。
> **目标读者**：负责实现本重构的后续 AI agent / 开发者。
> **涉及 target**：`AirportSim`、`StudioSim`、`NextGameplay`（公共层落点）；间接受影响：所有链接 `NextGameplay` 的目标（`CharacterDemo` / `gkNextRenderer` / `gkNextUnitTests` 等）。
> **前置必读**：
> - `src/Application/Game/AirportSim/`（**重构后的目标架构样板**：分层、`IAgentVisual`、`AgentSystem` rig 池、`AirportSimUI`、薄 `GameInstance`）。
> - `src/Application/Game/StudioSim/`（**被重构对象**：1925 行单体 `StudioSimGameInstance`、box 视觉、丰富经营玩法）。
> - `docs/plans/airport-sim-mvp-plan.md`、`docs/plans/studiosim-mvp-plan.md`、`docs/plans/studiosim-gameproject-iteration-plan.md`、`docs/plans/studiosim-production-model-refinement.md`（两个 app 的设计史与玩法语义来源）。
> - `docs/designs/scad-rig-design.md` + `AGENT_GUIDE/ScadRig.md`（ScadRig 已实施，使用手册）。
> - `AGENT_GUIDE/CharacterDemo.md`、`AGENT_GUIDE/SCADLoader.md`、`AGENTS.md`（构建/验证/分层纪律）。
> **本文写作前已核实的真实代码**：两个 app 全部 `.h/.cpp`；`NextGameplay::FNavGrid`/`FPathFollower`/`FRigInstance`/`FRigAnimator`；`Assets::FRigAsset`/`Assets::FScadRigLoader::LoadRig`；`NextGameInstanceBase` 生命周期；`NextAI::FAIService`/`NextAI::GetAIService`；`src/CMakeLists.txt` 的 target 注册与 `NextGameplay` 依赖（仅依赖 `gkNextEngine`）。下文 API 引用均为已存在符号，新符号一律标注 **【新增】**。

---

## 0. 摘要与决策

### 0.1 一句话目标

把 AirportSim 已经验证良好的「LLM 驱动角色生态」基础设施沉淀为 `NextGameplay` 下的公共仿真层 **Sim Kit**，让 AirportSim 与 StudioSim 共用同一套 *SCAD 锚点解析 / 角色池与寻路 / ScadRig 视觉*，并据此把 StudioSim 从 1925 行单体彻底重写为与 AirportSim 同构的分层架构，角色表现从直立 box 升级为 ScadRig 骨骼角色。

### 0.2 三项已拍板决策（需求方确认）

| # | 议题 | 决策 |
| --- | --- | --- |
| 1 | **StudioSim 重构力度** | **彻底重写，对齐 AirportSim 分层**（Layer0 确定性 / Layer1 LLM / 独立 UI 类 / 薄 GameInstance），保留现有经营玩法语义。 |
| 2 | **公共层落点** | **`src/Gameplay/Sim/`**（编入 `NextGameplay` 静态库）。**`NextGameplay` 提升为更高层的 gameplay 模块**，允许链接其它 module（`ScadLoader`/`NextAI`），不再受「只依赖 `gkNextEngine`」约束（见 §2.2）。 |
| 3 | **首批纳入公共层的范围** | **核心三件套**：① SCAD→POI 锚点解析；② 角色池（NavGrid + PathFollower + 移动/分离）；③ `ISimVisual` + ScadRig/Box 视觉 + rig 池注入。调度器/时钟/相机/overlay/感知列为 Round 2（§3.4、§10）。 |

### 0.3 成功标准

1. `./gnb build AirportSim` 与 `./gnb build StudioSim` 均通过；`./gnb build gkNextRenderer gkNextUnitTests` 通过（确认 `NextGameplay` API 未破坏其它消费者）。
2. `gnb shot --target AirportSim` 截图与重构前**视觉等价**（行为保持式适配，§4）。
3. `gnb shot --target StudioSim` 显示员工为 **ScadRig 角色**（按职位换色、有 Idle/Walk/Sit/Work 动作），办公室经营流程（晨会立项→生产→事件→聚集决策→复盘结算）功能不退化。
4. 公共三件套在 `gkNextUnitTests` 有最小覆盖（锚点解析 / 角色池 spawn-move-despawn / 视觉 hint 切换），与现有 `Test_ScadRig` 等同级。

---

## 1. 背景与动机

### 1.1 两个 app 现状对比

| 维度 | AirportSim（样板） | StudioSim（待重构） |
| --- | --- | --- |
| 架构 | 分层清晰：Layer0 确定性（`JourneySystem`/`TimeSystem`/`FlightBoard`/`QueueSystem`/`AgentSystem`），Layer1 LLM（`DecisionScheduler`/`PerceptionSystem`） | 系统已拆分（`EmployeeSystem`/`GoalSystem`/`EventSystem`/`GatheringSystem`/`ProductionSystem`/`DecisionScheduler`），但编排+UI+会议运行时+飘字全塞进 1925 行 `StudioSimGameInstance.cpp` |
| 角色视觉 | `IAgentVisual` 接口 → `GeometryVisual`（box）/ `ScadRigVisual`（骨骼），`AgentSystem` 内含 rig 池注入 | 直接 `FProcModel::CreateBox` 建 box 节点，无视觉接口、无 ScadRig |
| UI | 独立 `AirportSimUI` 类（1089 行，含世界 overlay/调试面板/气泡） | 全部 `DrawXxxHud`/`DrawXxxModal`/`DrawWorldOverlay` 内联在 GameInstance |
| 时钟 | 独立 `TimeSystem`（游戏分钟 + 日夜光照 + 班次窗口） | `FWorldState.gameClockMinutes` 内联推进，无日夜 |
| 相机 | 观察相机：总览 + 锁定跟踪 + 点选 agent + 滚轮缩放 | 固定 `OverrideRenderCamera`，无跟踪/点选 |
| LLM 调度 | `DecisionScheduler`（单在途 + worker 入队 + 主线程 apply + 轮询游标 + 决策日志 + fallback） | `DecisionScheduler`（同纪律的早期版，无日志/游标/感知联动） |
| 入口 | `CreateGameInstance` 直接 new；不依赖 QuickJS | 额外 `Modules::NextQuickJS::Install(... entryScript="assets/scripts/studiosim_entry.js")`，而该脚本是空的 `export {};`（见 §5.6） |
| 代码量 | ~5697 行 / 24 文件，单文件最大 1089（UI） | ~5429 行 / 19 文件，单文件最大 1925（GameInstance） |
| 玩法 | 纯观察盒（无经营） | 经营：晨会立项（题材/类型/规模）→ 四仪表生产状态机 → 玩家事件冲击 → 聚集群体决策 → 上线复盘/评分/营收结算 |

> 史实背景：AirportSim 本身就是「照搬 StudioSim 模式」起步的（`OfficeMap`→`AirportMap`、`DecisionScheduler` 同源、气泡 UI 同源，见 `AirportSim-MVP-Plan.md` 前置说明与各文件注释），随后被打磨成更干净的分层并接入 ScadRig。本计划是把这层「事实上的共享」正式沉淀为代码层，并让 StudioSim 反向受益。

### 1.2 重复基础设施盘点（抽取依据）

| 能力 | AirportSim 实现 | StudioSim 实现 | 是否首批抽取 |
| --- | --- | --- | --- |
| SCAD 具名节点 → POI 锚点表 | `AirportMap`（含 `frontDir`、claim/release、4 联座 slot） | `OfficeMap`（含 `roleTag`、`workable`、claim） | ✅ 件套一（§3.1） |
| 角色池 + NavGrid + PathFollower + 移动/分离 | `AgentSystem`（pool 复用 + 脚本走点 `MoveAlong`） | `EmployeeSystem`（固定人数 + repath） | ✅ 件套二（§3.2） |
| 视觉接口 + box + ScadRig + rig 池注入 | `IAgentVisual`/`GeometryVisual`/`ScadRigVisual` + `AgentSystem::InjectAssets` rig 段 | 仅 box | ✅ 件套三（§3.3） |
| `EMood` / `FDecisionResult` / `MinutesToHHMM` / `EAnimHint` | `AirportSimTypes.h` | `StudioSimTypes.h` | ⛔ Round 2（§3.4、§10） |
| 串行 LLM 决策调度纪律 | `DecisionScheduler`（进阶版） | `DecisionScheduler`（早期版） | ⛔ Round 2（StudioSim 重写时先 app 内对齐，§5.3/§10） |
| 世界时钟 / 时间倍速 / 日夜 | `TimeSystem` | `FWorldState` 内联 | ⛔ Round 2 |
| 观察相机（总览+跟踪+点选+缩放） | GameInstance 内 | 无 | ⛔ Round 2 |
| 世界 overlay（投影/气泡/名牌/飘字） | `AirportSimUI` | GameInstance 内联 | ⛔ Round 2 |

### 1.3 目标与非目标

**目标**
- 在 `src/Gameplay/Sim/` 落地核心三件套，**纯增量**（新文件，不动 `NextGameplay` 既有 API），先让 AirportSim 行为保持式接入验证，再支撑 StudioSim 重写。
- StudioSim 彻底重写为分层架构 + ScadRig 角色，保留全部经营玩法语义。

**非目标（本轮不做）**
- 不抽取调度器/时钟/相机/overlay/感知（Round 2）。
- 不改 ScadRig 引擎机制、不改渲染管线、不改 `.scad` 资产布局（仅允许微调锚点）。
- 不改 AirportSim 的玩法与数值（仅做底座替换的行为等价适配）。
- 不新增蒙皮/IK/动画混合树。

---

## 2. 目标架构总览

### 2.1 分层图

```
┌─────────────────────────────────────────────────────────────────┐
│ Application/Game/AirportSim        Application/Game/StudioSim     │
│  ├ *GameInstance（薄编排）          ├ *GameInstance（薄编排，重写）│
│  ├ *UI（世界 overlay + 面板）        ├ *UI（重写，从单体抽出）      │
│  ├ Layer0：Journey/Time/Flight/Queue ├ Layer0：Office/Production/Day │
│  ├ Layer1：Decision/Perception       ├ Layer1：Decision/Goal/Event/  │
│  │                                   │         Gathering/Perception  │
│  └ 域类型/Config/roster              └ 域类型/Config/roster          │
│            │  通过接口/数据消费 ▼            │                        │
├─────────────────────────────────────────────────────────────────┤
│  NextGameplay（提升为高层 gameplay 模块）── src/Gameplay/Sim/        │
│   ① FAnchorMap     SCAD 具名节点 → 通用 POI 锚点表 + claim/seat      │
│   ② FCharacterPool 角色池 + NavGrid + PathFollower + 移动/分离       │
│   ③ ISimVisual / FGeometryVisual / FScadRigVisual / FRigPool        │
│   （Round 2）FDecisionScheduler / FWorldClock / 相机 / overlay …     │
│  （既有）src/Gameplay/AI（NavGrid/PathFollower）、Rig（RigInstance）  │
│      │ 现在可直接 link ▼（DAG 无环：两 module 仅依赖 gkNextEngine）  │
├──────────────┬──────────────────────────────┬─────────────────────┤
│  ScadLoader  │  NextAI                       │  gkNextEngine        │
│  FScadRig-   │  FAIService / GenerateText-   │  Assets::Scene/Node/ │
│  Loader::    │  Async（公共层可直接调用）     │  Model/FRigAsset/    │
│  LoadRig     │                               │  Camera/Env …        │
└──────────────┴──────────────────────────────┴─────────────────────┘
```

### 2.2 依赖策略（已放宽：NextGameplay 提升为高层模块）

**决策更新**：`NextGameplay` 不再限制为「只依赖 `gkNextEngine`」，而是**提升为更高层的 gameplay 模块，允许链接 `ScadLoader` 与 `NextAI`**。依赖图保持无环——`ScadLoader` 与 `NextAI`（`GK_MODULE_TARGETS`）目前都只依赖 `gkNextEngine`（见 `src/CMakeLists.txt` L485–490 module 分支），所以新增边 `NextGameplay → {ScadLoader, NextAI}` 不产生环：

```
NextGameplay ──► ScadLoader ──► gkNextEngine
       │     └──► NextAI    ──► gkNextEngine
       └────────────────────► gkNextEngine
```

由此放开两条原约束：

1. **可依赖 `ScadLoader`**：件套三的 ScadRig **加载**（`Assets::FScadRigLoader::LoadRig`）可下沉进公共层——`FCharacterPool`/`FRigPool` 直接吃 rig 路径并自行 `LoadRig`，app 不必再手动加载+传 `Assets::FRigAsset`（仍保留「传入预加载 asset」的重载以便共享/测试，见 §3.3）。
2. **可依赖 `NextAI`**：Round 2 的串行 LLM 决策调度可作为**具体类** `Sim::FDecisionScheduler` 直接持有 `NextAI::FAIService*`，**不再需要** §10.1 原先设计的「抽象 pump + 回调注入」绕行方案（该绕行方案作废，§10.1 改为具体类草案）。

**CMake 改动（必做，M1/M5 触发 `--reconfigure`）**：把 `NextGameplay` 加入既有的 module 链接清单即可（沿用现成惯例，`src/CMakeLists.txt` L643–660）：

```cmake
if ( TARGET ScadLoader )
    foreach(t NextGameplay gkNextRenderer ... StudioSim AirportSim gkNextUnitTests) # ← 加 NextGameplay
        if ( TARGET ${t} ) target_link_libraries(${t} PRIVATE ScadLoader) endif()
    endforeach()
endif()
if ( TARGET NextAI )
    foreach(t NextGameplay gkNextEditor ... StudioSim AirportSim gkNextUnitTests)   # ← 加 NextGameplay
        if ( TARGET ${t} ) target_link_libraries(${t} PRIVATE NextAI) endif()
    endforeach()
endif()
```

> 静态库 + PRIVATE：`NextGameplay` 的 PRIVATE static 依赖会自动并入每个最终可执行文件的链接闭包，故 `CharacterDemo`/`gkNextRenderer` 等消费者无需再各自显式 link（现有显式 link 行保留亦无害）。**Android**：module 源码被直接编进单一 SHARED target（见 CMake 顶部注释），一切符号同库内，天然可用。

**权衡（已知并接受）**：提升 `NextGameplay` 后，它的所有消费者（`CharacterDemo`/`gkNextRenderer`/`gkNextVisualTest`/`gkNextUnitTests` …）的链接闭包都会带上 `ScadLoader`（含其 manifold/freetype/earcut 可选依赖）与 `NextAI`。这些都是静态库、未用符号会被裁剪，体积/构建代价可控；换来的是公共层能力完整、API 更简洁（无 pump 绕行、rig 自加载）。如担心个别轻量 target（如纯渲染 demo）不该背上 LLM 依赖，可改为「只让 `NextGameplay` 自身 link，消费者按需」——但鉴于均为静态库，本计划默认全量传递，简洁优先。

首批（核心三件套）**实际只强依赖 `ScadLoader`**（件套三 rig 自加载）；`NextAI` 的链接边可一并加上以备 Round 2，亦可推迟到抽取调度器时再加。

### 2.3 命名空间与目录布局

- 命名空间：`NextGameplay::Sim`（与既有 `NextGameplay`/`NextGameplay::Rig` 同族）。
- 目录：`src/Gameplay/Sim/`，源码被 `src/cmake/SourceFiles.cmake` 的 `file(GLOB_RECURSE src_files_nextgameplay "Gameplay/*.cpp" …)` 自动收录（glob 已覆盖子目录，新增 `.cpp` 需 `--reconfigure` 一次让 glob 重扫）。**链接关系需改 CMake**：把 `NextGameplay` 加进 `ScadLoader`/`NextAI` 的 module 链接清单（§2.2 代码块）。
- 文件命名沿用 Gameplay 层风格（`RigInstance.h`/`NavGrid.h` 无 F 前缀类、`FRigInstance`/`FNavGrid` 类型用 F）。本计划新类型用 `F`/`I` 前缀，文件名见各件套清单。

---

## 3. 公共层首批：核心三件套（`src/Gameplay/Sim/`）

> 设计原则：**先共性、后差异**。公共层只承载两个 app 的*交集*；域特有字段（旅客旅程态、员工任务态、座位语义、职位标签）留在 app 侧，通过「核心结构 + app 侧扩展/侧表」组合，而非塞进公共结构。

### 3.1 件套一：SCAD→POI 锚点解析 `FAnchorMap`

**现状差异**

| | `AirportMap`（`AirportSim/AirportMap.*`） | `OfficeMap`（`StudioSim/OfficeMap.*`） |
| --- | --- | --- |
| POI 字段 | name/category/worldPos/**frontDir**/nodeId/occupiedBy(-1)/**seatOccupied[4]** | name/category/**roleTag**/worldPos/nodeId/**workable**/occupiedBy(0) |
| 解析 | 节点名→category 前缀；从节点世界旋转恢复 `frontDir`（scad 局部 -y → 引擎 +z） | 节点名→category 前缀；desk 解析职位标签 |
| 操作 | `PointsOfCategory`/`FindByName(Mutable)`/`ClaimFree`/`Release`/`ClaimSeat`/`ReleaseSeat`/`ServicePoint`/`SeatPosition` | `PointsOfCategory`/`FindByName`/`SetWorkable`/`ResetWorkable` |

**统一设计**

```cpp
// src/Gameplay/Sim/AnchorMap.h   namespace NextGameplay::Sim
struct FAnchorPoi                         // 【新增】通用锚点（交集字段）
{
    std::string name;                     // 节点名 = scad module 名
    std::string category;                 // 名字前缀（'_NN' 之前），如 "checkin"/"desk"
    glm::vec3   worldPos{0.0f};
    glm::vec3   frontDir{0.0f, 0.0f, 1.0f}; // 由节点世界旋转恢复（统一实现，office 也能用）
    uint32_t    nodeId = 0;
    int         occupiedBy = -1;          // 统一用 -1 表示空（StudioSim 旧用 0，迁移时改判定）
    int         seatOccupied[4] = {-1,-1,-1,-1}; // 多 slot POI（wait/沙发）；不用则全 -1
    bool        enabled = true;           // = StudioSim workable（断电/宕机置 false）
};

struct FAnchorParseConfig                 // 【新增】解析配置（app 注入命名约定）
{
    char categorySeparator = '_';         // category = 名字到第一个分隔符+数字前
    bool recoverFrontDir = true;          // 是否从世界旋转恢复 frontDir
    // 可选：category 白名单（空 = 接受所有具名 user-module 节点）
    std::vector<std::string> acceptCategories;
};

class FAnchorMap                          // 【新增】= AirportMap ∪ OfficeMap 的交集能力
{
public:
    void BuildFromScene(Assets::Scene& scene, const FAnchorParseConfig& cfg);
    void Clear();
    const std::vector<FAnchorPoi>& Points() const;
    std::vector<FAnchorPoi>&       PointsMutable();
    std::vector<const FAnchorPoi*> PointsOfCategory(const std::string& category) const;
    const FAnchorPoi* FindByName(const std::string& name) const;
    FAnchorPoi*       FindByNameMutable(const std::string& name);
    FAnchorPoi* ClaimFree(const std::string& category, int agentId); // occupiedBy<0 的首个
    void        Release(const std::string& name, int agentId);
    int  ClaimSeat(const std::string& poiName, int agentId, glm::vec3& outPos);
    void ReleaseSeat(const std::string& poiName, int slot, int agentId);
    void SetEnabled(const std::string& category, bool enabled);      // = SetWorkable
    void ResetEnabled();
    static glm::vec3 ServicePoint(const FAnchorPoi& poi, float frontOffset);
    static glm::vec3 SeatPosition(const FAnchorPoi& poi, int slot, float spacing, float frontOffset);
};
```

**域特有数据的处理（关键决策）**

`AirportSim` 的 POI 几乎与 `FAnchorPoi` 等价（seat 已在交集里），可直接用 `Sim::FAnchorPoi` 替换 `AirportSim::FPointOfInterest`。`StudioSim` 多一个 `roleTag`（desk 的职位标签）——**推荐**：StudioSim 不把 `roleTag` 塞回公共结构，而是在 `OfficeMap`（app 侧薄包装，持有 `Sim::FAnchorMap`）里维护 `name → ERole` 的侧表，从 `category`/节点名按命名约定解析（如 `desk_engineer_01`）。这样公共结构不被任一 app 的领域概念污染。

> 备选（不推荐）：给 `FAnchorPoi` 加 `uint32_t userTag`/`void* userData`。会让公共层语义模糊，且 `void*` 生命周期难管。除非后续出现第三个 app 也需要标签，否则用 app 侧侧表。

**文件清单**
- 新增：`src/Gameplay/Sim/AnchorMap.h`、`AnchorMap.cpp`。
- AirportSim：删 `AirportMap.*`，`AirportMap` 改为对 `Sim::FAnchorMap` 的 `using`/薄壳（保留 `ServicePoint`/`SeatPosition` 等数值入口或转调）。
- StudioSim（重写期）：`OfficeMap` 持有 `Sim::FAnchorMap` + roleTag 侧表。

### 3.2 件套二：角色池 `FCharacterPool`

**现状差异**

| | `AgentSystem` | `EmployeeSystem` |
| --- | --- | --- |
| 池 | 固定大小池（员工固定槽 + 旅客循环复用），spawn/despawn | 固定人数（开局生成，不复用） |
| 资源注入 | `InjectAssets`（box 模型/材质 **+ rig 池**）；`OnSceneLoaded` 建 NavGrid + 池节点（藏地下 `kParkedPos`） | `InjectAssets`（box 模型/材质）；`OnSceneLoaded` 建 NavGrid + 员工节点 |
| 移动 | A* `MoveTo` + 脚本走点 `MoveAlong` + `Arrived`；`Tick` 积分 + **分离力** + 写 transform | `RepathTo` + `Tick` 沿 follower 移动（含中庭轻推） |
| 视觉 | 每 agent 持 `IAgentVisual`（box / rig） | 直接操作 `emp.node`（box） |

**统一设计**

把「与领域无关的运动学核心」抽成 `FSimCharacter`，把「池 + NavGrid + mover + 分离 + rig 池注入」抽成 `FCharacterPool`；app 的 `FAgent`/`FEmployee` **组合**一个 `FSimCharacter`（或继承），领域字段留在 app 结构。

```cpp
// src/Gameplay/Sim/SimCharacter.h   namespace NextGameplay::Sim
struct FSimCharacter                      // 【新增】运动学+视觉核心（域无关）
{
    int   id = -1;
    bool  active = false;
    glm::vec3 position{0.0f};
    float yaw = 0.0f;
    float speed = 1.8f;
    NextGameplay::FPathFollower follower;
    bool  moving = false;
    glm::vec3 moveTarget{0.0f};
    std::vector<glm::vec3> scriptWaypoints; // 脚本走点（绕过 NavGrid，如安检单向流）
    EAnimHint anim = EAnimHint::Idle;
    std::unique_ptr<ISimVisual> visual;     // 件套三
};

// src/Gameplay/Sim/CharacterPool.h
struct FCharacterPoolConfig               // 【新增】数值（各 app 用自己的 Config 填）
{
    int   poolCapacity = 32;
    float navCellSize = 0.45f;
    float agentRadius = 0.28f;
    float separationRadius = 0.6f;
    float separationStrength = 1.2f;
    float groundY = 0.15f;
    glm::vec3 parkedPos{0.0f, -100.0f, 0.0f};
    // 视觉：box 回退 + 可选 ScadRig。公共层现可直接 link ScadLoader，故二选一：
    bool useRig = false;
    std::string rigPath;                          // 非空 → 池内部 FScadRigLoader::LoadRig 自加载
    const Assets::FRigAsset* rigAsset = nullptr;  // 或传入预加载 asset（共享/测试用，优先级高于 rigPath）
    glm::vec3 boxHalfMin{-0.25f, 0.0f, -0.25f};
    glm::vec3 boxHalfMax{ 0.25f, 1.6f, 0.25f};
};

class FCharacterPool                      // 【新增】= AgentSystem ∩ EmployeeSystem 的运动学/视觉骨架
{
public:
    void Configure(const FCharacterPoolConfig& cfg);
    // BeforeSceneRebuild：注入 box 模型/材质 + （useRig 时）每槽 rig part 模型与材质
    void InjectAssets(std::vector<Assets::Model>& models, std::vector<Assets::FMaterial>& materials);
    // OnSceneLoaded：建 NavGrid + 创建池节点（藏 parkedPos）；视觉按 useRig 选 box/rig
    void OnSceneLoaded(Assets::Scene& scene);
    void Clear();

    // 槽位管理：返回核心引用；app 把领域结构与 slot 绑定（或令 FAgent 继承 FSimCharacter）
    FSimCharacter* Acquire(int slot, const glm::vec3& pos, const glm::vec3& tintColor);
    void           Release(FSimCharacter& c);

    bool MoveTo(FSimCharacter& c, const glm::vec3& target);   // NavGrid A*
    void MoveAlong(FSimCharacter& c, std::vector<glm::vec3> waypoints);
    bool Arrived(const FSimCharacter& c) const;

    // 对一组角色积分移动 + 分离力 + 写节点 transform + 推进各自 visual 动画
    void Tick(float deltaSeconds, Assets::Scene& scene, std::span<FSimCharacter> characters);

    bool NavReady() const;
    const NextGameplay::FNavGrid& NavGrid() const;
};
```

**迁移取向（关键决策）**
- AirportSim 的 `FAgent`、StudioSim 的 `FEmployee` 改为 **持有/继承 `Sim::FSimCharacter`**：领域字段（旅客 `pstate`/`flightIdx`/`groupId`…，员工 `todayTask`/`mood`/`gatheringId`…）留在 app 结构；运动学/视觉字段下沉到核心。
- `AgentSystem`/`EmployeeSystem` 保留为**领域系统**（spawn 策略、roster、状态机驱动目标），把「建 NavGrid / 积分移动 / 分离 / 写 transform / rig 池注入」委托给 `FCharacterPool`。
- 分离力、`MoveAlong` 脚本走点等 AirportSim 已有、StudioSim 没有的能力，下沉后 StudioSim 免费获得（员工避免叠在 box 上、会议走位更自然）。

**文件清单**
- 新增：`src/Gameplay/Sim/SimCharacter.h`、`CharacterPool.h`、`CharacterPool.cpp`。
- AirportSim：`AgentSystem` 瘦身，移动/NavGrid/rig 段转调 `FCharacterPool`；`FAgent` 组合 `FSimCharacter`。
- StudioSim（重写期）：`EmployeeSystem` 同样转调；`FEmployee` 组合 `FSimCharacter`。

### 3.3 件套三：视觉层 `ISimVisual` + `FGeometryVisual` + `FScadRigVisual` + rig 池注入

这是「让 StudioSim 用上 Scad character」的核心载体，几乎可从 AirportSim **原样上移**。

```cpp
// src/Gameplay/Sim/SimVisual.h   namespace NextGameplay::Sim
enum class EAnimHint { Idle, Walk, Sit, Work };   // 【新增】= AirportSim EAgentAnimHint

class ISimVisual                                   // 【新增】= IAgentVisual
{
public:
    virtual ~ISimVisual() = default;
    virtual void SetWorldTransform(const glm::vec3& pos, float yaw) = 0;
    virtual void SetAnimHint(EAnimHint hint) = 0;
    virtual void SetVisible(bool visible) = 0;
    virtual void SetMoveSpeed(float metersPerSecond) {}  // rig 走路速度匹配；box 忽略
    virtual void Tick(float deltaSeconds) {}             // rig 动画推进；box 忽略
};

class FGeometryVisual final : public ISimVisual { /* = GeometryVisual：单 box 节点 */ };

// src/Gameplay/Sim/ScadRigVisual.h —— 消费 Assets::FRigAsset + NextGameplay::Rig
class FScadRigVisual final : public ISimVisual      // 【新增】= AirportSim::ScadRigVisual 上移
{
public:
    FScadRigVisual(Assets::Scene& scene, const Assets::FRigAsset& asset,
                   const NextGameplay::FRigInstanceDesc& desc, int poolSlot,
                   const FRigVisualParams& params);   // baseWalkSpeed/sizeJitter/parkedPos
    // SetWorldTransform/SetAnimHint/SetMoveSpeed/SetVisible/Tick：照搬现实现
};
```

**rig 加载 + 池注入**：现在公共层可链接 `ScadLoader`，因此把 `AgentSystem::InjectAssets` 里的 rig 段（`FScadRigLoader::LoadRig` 加载 + 每池槽 part 模型 + 每 part section 材质 + tint 材质，见 `AgentSystem.cpp` L77 加载 / L84–L182 注入）**整体上移**到 `FCharacterPool` 内部或独立 `FRigPool` 助手——app 只在 Config 里给 `rigPath`，无需再手动加载。注意 ScadRig 当前是「GPU-driven primitive buffer 按已注入 model 三角数定容」，所以**每池槽必须保留独立 part model**（见 `AgentSystem.h` 注释与 `rigSlotPartModelIds_`），这条约束随代码上移，不要在抽取时擅自合并模型（§8 风险）。

**clip 映射**：`Idle/Walk/Sit/Work` → `asset.FindClip("idle"/"walk"/"sit"/"work")`，缺失回退 `idle`（照搬 `ScadRigVisual.cpp` 的 `ClipForHint`）。`agent_basic.scad` 已含这四个 clip（AirportSim 在用）。

**文件清单**
- 新增：`src/Gameplay/Sim/SimVisual.h`/`.cpp`（`ISimVisual`/`EAnimHint`/`FGeometryVisual`）、`ScadRigVisual.h`/`.cpp`（`FScadRigVisual`/`FRigVisualParams`）。
- AirportSim：删 `ScadRigVisual.*` 与 `AgentSystem.h` 内 `IAgentVisual`/`GeometryVisual`，改用 `Sim::` 版（或 `using IAgentVisual = Sim::ISimVisual` 过渡）。
- StudioSim（重写期）：直接用 `Sim::FScadRigVisual` + 职位 tint。

### 3.4 首批不纳入的部分（Round 2 候选，本轮仅做设计预留）

| 能力 | 为什么不进首批 | 本轮如何对待 |
| --- | --- | --- |
| 串行 LLM 决策调度 | **依赖已不再是障碍**（NextGameplay 现可 link NextAI）；仅因首批范围限定为「核心三件套」而暂缓，且两版 prompt/apply 差异大 | StudioSim 重写时**在 app 内对齐 AirportSim 进阶版**；Round 2 直接抽成具体类 `Sim::FDecisionScheduler`（持 `FAIService*`，§10.1）。**如想提前**：依赖已通，可低成本并入首批 |
| 世界时钟 / 时间倍速 / 日夜 | 与玩法耦合（航班窗口 vs 工作日阶段） | StudioSim 重写沿用自有 `FWorldState` 时钟，结构对齐 `TimeSystem` |
| 观察相机 | 与各 app 取景强相关 | StudioSim 重写时**新增**总览+跟踪+点选，照 AirportSim 写法（app 侧） |
| 世界 overlay / 气泡 / 飘字 | 投影工具可共享，但与各 app UI 排版耦合 | 抽成 `*UI` 类内方法；Round 2 再提公共 `FWorldOverlay` |
| 感知系统 | StudioSim 现把触发散在各系统 | 重写时**新增** `PerceptionSystem`，照 AirportSim 形态（app 侧） |

> 为降低 Round 2 成本：StudioSim 重写时这些 app 侧实现**有意保持与 AirportSim 同形**（同函数名/同字段/同纪律），让将来「二次上移」是机械抽取而非重新设计。

---

## 4. AirportSim 适配（先行验证公共层）

**原则：行为保持式（behavior-preserving）**。这一步不改玩法、不改数值、不改 UI，只把底座换成 `Sim::`，用截图等价验证抽取无副作用。先在最干净的 AirportSim 上跑通，再去碰 StudioSim。

逐项改动：
1. `AirportMap` → `Sim::FAnchorMap`（+ 数值入口转调）。校验 `frontDir` 恢复、`ClaimSeat` 行为一致。
2. `FAgent` 组合 `Sim::FSimCharacter`；`AgentSystem` 的 NavGrid/移动/分离/rig 注入转调 `Sim::FCharacterPool`；spawn/despawn 策略与 roster 逻辑保留在 `AgentSystem`。
3. `IAgentVisual`/`GeometryVisual`/`ScadRigVisual` → `Sim::ISimVisual`/`FGeometryVisual`/`FScadRigVisual`（`EAgentAnimHint` → `Sim::EAnimHint`，或 app 内 `using` 过渡）。
4. `AirportSimConfig` 的相关常量填进 `FCharacterPoolConfig`/`FAnchorParseConfig`/`FRigVisualParams`。

验证（每步后）：
- `./gnb build AirportSim`（仅该 target）。
- `gnb shot --target AirportSim`（隐藏 UI）与 `gnb shot --target AirportSim --ui`，对比重构前后 `agent_validation.jpg`：角色数量/分布/动作/职业配色一致；安检单向流、坐席、气泡正常。
- `./gnb build gkNextRenderer gkNextUnitTests` 确认未破坏 `NextGameplay` 其它消费者。

---

## 5. StudioSim 彻底重写（对齐 AirportSim 分层）

### 5.1 目标文件结构（旧 → 新）

| 关注点 | 旧（现状） | 新（重写后，对齐 AirportSim） |
| --- | --- | --- |
| 编排 | `StudioSimGameInstance.cpp` 1925 行（含一切） | `StudioSimGameInstance.{hpp,cpp}` 薄编排（≈ AirportSim 的 381 行级别）：OnInit/BeforeSceneRebuild/OnSceneLoaded/OnTick(tick 各系统)/OnRenderUI(转 UI)/相机/输入 |
| UI | 内联 `DrawStatusHud`/`DrawProgressHud`/`DrawEmployeeHud`/`DrawEventHud`/各 Modal/`DrawWorldOverlay`/飘字 | **新增 `StudioSimUI.{h,cpp}`**（对照 `AirportSimUI`）：世界 overlay + HUD + 立项/目标/聚集/复盘 Modal + 飘字粒子 |
| 数值 | 散落 + `studio_sim.json` 卡片 | **新增 `StudioSimConfig.hpp`**（对照 `AirportSimConfig`）：员工 roster、时钟、生产、聚集触发等常量；可保留 json 卡片覆盖 |
| 时钟 | `FWorldState` 内联推进 | `DayClock`/`WorldClock`（app 侧，结构对照 `TimeSystem`：游戏分钟 + 倍速 + pause + dayIndex；日夜可选） |
| 相机 | 固定 | 观察相机（总览 + 锁定员工跟踪 + 点选 + 滚轮，照 AirportSim） |
| 角色池/视觉 | `EmployeeSystem` 直建 box | `EmployeeSystem` 转调 `Sim::FCharacterPool` + `Sim::FScadRigVisual`（§5.5） |
| 锚点 | `OfficeMap` 自解析 | `OfficeMap` 持 `Sim::FAnchorMap` + roleTag 侧表 |
| 感知 | 散在系统 | **新增 `PerceptionSystem`**（照 AirportSim） |

### 5.2 Layer 0 —— 确定性层（保证「工作室永远在正确运转」）

- `OfficeMap`：薄包 `Sim::FAnchorMap`；desk 的 `ERole` 侧表。
- `EmployeeSystem`：roster/卡片加载 + 脚本日程 `DaySchedule`（LLM 不可用时的兜底目标）+ 委托 `FCharacterPool` 做移动/视觉。
- `ProductionSystem`：四仪表（tech/design/art/polish）确定性生产状态机（Planning→Production→Polish→Done + bug 循环），**保留现有逻辑**，仅把员工「在工位 WORK」判定改用 `FSimCharacter`/`FAnchorMap`。
- `DayClock`：工作日阶段（Briefing/Working/Review）+ 游戏时钟（仅 Working 推进）+ 倍速。

### 5.3 Layer 1 —— LLM 层（经营叙事的来源）

- `DecisionScheduler`：**对齐 AirportSim 进阶版**（单在途 + worker 入队 + 主线程 apply + 轮询游标 + 决策日志 + fallback）。prompt 构造/结果 apply 保留 StudioSim 语义（WORK/REST/TALK/MEETING/IDLE + targetPoi/targetEmployee/dialogue/mood/duration）。**有意写成与 AirportSim 同形**以便 Round 2 抽取（§10.1）。
- `GoalSystem`：晨会三候选目标 → 玩家选择/自定义 → 分解到各职位 `todayTask` → Active → 复盘 Summarize。**保留**（已是独立异步纪律系统）。
- `EventSystem`：玩家注入事件（断电/宕机…）→ 改全局氛围 + 全员清目标重决策。**保留**。
- `GatheringSystem`：聚集（会议/茶水间）→ 进度感知多人对白 + 群体决策（采纳改派 `todayTask`）。**保留**。
- `PerceptionSystem`：**新增**，把现在散在 GameInstance/各系统的「事件冲击→插队重决策」「长时间无产出→触发聚集」等触发条件收敛为统一感知 tick（照 AirportSim `PerceptionSystem`）。

### 5.4 UI 层 —— `StudioSimUI` 抽取

把 `StudioSimGameInstance.cpp` 中以下成员整体迁出到 `StudioSimUI`，GameInstance 仅持有 `StudioSimUI ui_` 并在 `OnRenderUI` 转调（对照 AirportSim：`ui_.Draw(viewProjection, …)`）：
- 世界 overlay：`DrawWorldOverlay`（员工头顶气泡/名牌/mood 图标）、`CollectProductionVisualEvents`/`TickFloatingText`（飘字粒子）。
- HUD：`DrawStatusHud`/`DrawProgressHud`/`DrawEmployeeHud`/`DrawEventHud`。
- Modal：`DrawProjectPitchModal`/`DrawGoalChoiceModal`/`DrawGatheringDecisionModal`/`DrawReviewModal`。
- 会议运行时 `FMeetingRuntime`、飘字 `FFloatingTextParticle`、立项选择缓存等 UI 态随之迁入（或拆到 UI 与 system 各取所需）。
- 复用 AirportSim 的 `ProjectWorld`（世界→屏幕投影）等工具——Round 2 提为公共 `FWorldOverlay`，本轮先在 `StudioSimUI` 内放一份同形实现。

### 5.5 ScadRig 角色（让 StudioSim 用上 Scad character）

- 复用同一 `assets/scad/characters/agent_basic.scad` + **按职位 tint 换色**：StudioSim 6 职位（Engineer/Artist/Designer/PM/QA/Boss）各给一个 `glm::vec3` 配色（`FEmployeeCardDef.color` 已有），映射到 rig 的 tint 材质，与 AirportSim「职业配色」机制完全一致。
- 动作：办公室主要用 `Idle/Walk` + 工位 `Work` + 会议/茶水间坐下 `Sit`；四个 clip `agent_basic.scad` 已具备。员工到工位且 `ProductionSystem` 标记 WORK → `SetAnimHint(Work)`；会议/茶水间就座 → `Sit`。
- 开关与回退：照 AirportSim `Config::kUseScadRigVisual` + rig 加载失败回退 `FGeometryVisual`（StudioSimConfig 加同名常量）。
- 可选后续（非本轮）：为不同职位做 `.scad` 外观变体（如美术戴帽、Boss 西装），换 `kAgentRigPath` 即可，机制已支持。

### 5.6 清理项

- **去掉空的 QuickJS 入口**：`StudioSimGameInstance.cpp` L24/L686 安装 `Modules::NextQuickJS` 且入口脚本 `assets/scripts/studiosim_entry.js` 内容仅 `export {};`（无脚本逻辑）。重写时**移除该安装调用**，并把 `src/CMakeLists.txt` 中 StudioSim 的 `if(GK_WITH_NEXT_QUICKJS)` 门禁与 `gk_quickjs_program` 列表项去掉，使 StudioSim 与 AirportSim 一样无条件构建、不链接 QuickJS。**落地前确认**：全仓再无其它代码依赖 StudioSim 的 QuickJS 安装（已 grep：StudioSim 仅此两处引用 QuickJS）。
- **抽 `StudioSimConfig.hpp`**：把散落常量与 `studio_sim.json` 默认卡片归拢，对照 `AirportSimConfig` 的组织方式。
- **统一 `occupiedBy` 语义**：旧 `OfficeMap` 用 `0` 表示空、`Sim::FAnchorPoi` 用 `-1`，迁移时改判定避免 off-by-one。

### 5.7 必须保留的经营玩法语义（验收对照）

重写是「换骨架不换玩法」。以下行为在重写后必须等价（来源：`StudioSim-MVP-Plan.md`/`-GameProject-Iteration-Plan.md`/`-Production-Model-Refinement.md` 与现有 `StudioSimTypes.h`）：
- 立项：题材×类型×规模（`EGameGenre`/`EGameTheme`/`EProjectSizeTier`）+ 卖点 highlights + comboFit。
- 生产：四仪表推进 + bug 循环 + 阶段机 + 跨天错峰。
- 目标：晨会三候选 + 玩家选择/自定义 + 分解 todayTask + 复盘 summary/评分。
- 事件：注入冲击 + 全局氛围 + 全员重决策。
- 聚集：会议/茶水间 + 进度感知群体对白 + 群体决策改派。
- 结算：质量/评分/销量/营收/利润 + 公司资金 `FCompanyState`。

---

## 6. 里程碑与落地顺序

> 每个 M 结束都要能独立构建+截图验证；遵循 `AGENTS.md` 的「只构建受影响目标」。改 `NextGameplay`（M1）波及所有消费者，故 M1 验证含 `gkNextRenderer gkNextUnitTests`。

| 里程碑 | 内容 | 验证 | 构建目标 |
| --- | --- | --- | --- |
| **M0** | 在 `src/Gameplay/Sim/` 落空骨架（头文件 + stub），确认 glob 收录、`NextGameplay` 仍编译 | `./gnb build gkNextRenderer gkNextUnitTests` | 受影响全量（轻，stub） |
| **M1** | 件套一/二/三实现：`FAnchorMap` / `FSimCharacter`+`FCharacterPool` / `ISimVisual`+`FGeometryVisual`+`FScadRigVisual`+rig 自加载与池注入。**改 CMake**：`NextGameplay` link `ScadLoader`(必需) + `NextAI`(可选备用)（§2.2） | 单测：锚点解析、spawn-move-despawn、hint 切换；`gkNextUnitTests` 通过 | `--reconfigure` 后 `NextGameplay` + `gkNextRenderer gkNextUnitTests`（确认全消费者链接闭包 OK） |
| **M2** | AirportSim 行为保持式接入（§4） | `gnb shot --target AirportSim [--ui]` 截图与基线等价 | `AirportSim` |
| **M3** | StudioSim 拆 UI：抽 `StudioSimUI` + `StudioSimConfig`，GameInstance 瘦身（玩法不变、仍 box） | `gnb shot --target StudioSim`：HUD/Modal/overlay 与重构前一致 | `StudioSim` |
| **M4** | StudioSim 底座替换：`OfficeMap`/`EmployeeSystem` 转调 `Sim::`；`FEmployee` 组合 `FSimCharacter`（仍 box，验证移动/分离/寻路） | `gnb shot --target StudioSim`：员工移动正常、不叠堆 | `StudioSim` |
| **M5** | StudioSim 接 ScadRig（职位换色 + Idle/Walk/Sit/Work）+ 去 QuickJS 门禁（§5.6） | `gnb shot --target StudioSim`：员工为骨骼角色、配色/动作正确 | `StudioSim`（+ 改 CMake 后 `--reconfigure` 一次） |
| **M6** | StudioSim 分层补齐：`DecisionScheduler` 对齐进阶版 + 新增 `PerceptionSystem` + 观察相机 + `DayClock` 结构化 | 完整经营流程跑通（§5.7 验收清单）；LLM 不可用时 fallback | `StudioSim` |
| **M7** | 收尾：清死代码、补 `AGENT_GUIDE/SimKit.md` 使用手册、Round 2 抽取项标 TODO | 全量 `./gnb build --reconfigure` 通过；两 app 截图终验 | 全量 |

---

## 7. 公共层 API 速查（头文件签名草案）

> 仅签名，落地以代码为准；类型见 §3。完整列在此便于下游 agent 一眼对照。

```cpp
namespace NextGameplay::Sim
{
    // —— AnchorMap.h ——
    struct FAnchorPoi { /* name category worldPos frontDir nodeId occupiedBy seatOccupied[4] enabled */ };
    struct FAnchorParseConfig { char categorySeparator; bool recoverFrontDir; std::vector<std::string> acceptCategories; };
    class  FAnchorMap { BuildFromScene/Clear/Points/PointsOfCategory/FindByName(Mutable)/
                        ClaimFree/Release/ClaimSeat/ReleaseSeat/SetEnabled/ResetEnabled/ServicePoint/SeatPosition };

    // —— SimVisual.h ——
    enum class EAnimHint { Idle, Walk, Sit, Work };
    class  ISimVisual { SetWorldTransform/SetAnimHint/SetVisible/SetMoveSpeed/Tick };
    class  FGeometryVisual : ISimVisual;

    // —— ScadRigVisual.h（消费 Assets::FRigAsset + NextGameplay::Rig；加载走 ScadLoader）——
    struct FRigVisualParams { float baseWalkSpeed; float sizeJitterRange; glm::vec3 parkedPos; };
    class  FScadRigVisual : ISimVisual;

    // —— SimCharacter.h ——
    struct FSimCharacter { /* id active position yaw speed follower moving moveTarget scriptWaypoints anim visual */ };

    // —— CharacterPool.h ——
    struct FCharacterPoolConfig { /* poolCapacity nav* separation* groundY parkedPos useRig rigPath/rigAsset box* */ };
    class  FCharacterPool { Configure/InjectAssets/OnSceneLoaded/Clear/Acquire/Release/
                            MoveTo/MoveAlong/Arrived/Tick/NavReady/NavGrid };
}
```

---

## 8. 风险与缓解

| 风险 | 说明 | 缓解 |
| --- | --- | --- |
| **`NextGameplay` 改动波及全仓** | 公共层在共享静态库，`CharacterDemo`/`gkNextRenderer`/单测都链接它 | 首批代码层**纯增量**（只加 `Sim/` 新文件，不改既有 `NextGameplay` 头/签名）；M1 验证含 `gkNextRenderer gkNextUnitTests` |
| **提升 NextGameplay 后消费者背上 ScadLoader/NextAI** | 所有 `NextGameplay` 消费者链接闭包变大（含 manifold/freetype 等 ScadLoader 子依赖） | 均为静态库、未用符号裁剪；M1 全消费者构建确认无链接错误。若需收窄，可改为仅 `NextGameplay` 自身 link、消费者按需（§2.2 权衡） |
| **依赖图成环** | `NextGameplay → ScadLoader/NextAI` 新边 | 两 module 仅依赖 `gkNextEngine`，新边不成环（§2.2 DAG）；落地后 `--reconfigure` 全量构建确认 |
| **ScadRig 三角定容** | GPU-driven primitive buffer 按已注入 model 三角数定容，每池槽需独立 part model（`AgentSystem.h` 注释） | 抽取时原样保留 `rigSlotPartModelIds_` 逐槽独立模型，**不合并**；rig 池注入逻辑整体上移不改语义 |
| **POI payload 污染公共层** | StudioSim `roleTag` 想塞进 `FAnchorPoi` | 用 app 侧侧表（§3.1），公共结构只放交集字段 |
| **`occupiedBy` 语义不一致** | 旧 OfficeMap 空=0，新公共=−1 | 迁移时统一 −1，逐处改判定并单测 |
| **AirportSim 行为回归** | 适配引入隐性差异（分离力、frontDir、座位） | M2 严格行为保持 + 截图基线对比，每步小改小验 |
| **LLM 串行抢占** | 公共层 Round 2 才抽调度器；本轮两 app 各自单在途 | 不在首批引入跨 app 调度；StudioSim 重写沿用单实例 `parallel:1` 纪律 |
| **去 QuickJS 误删** | StudioSim 移除 QuickJS 安装 | 落地前再 grep 确认无其它依赖；改 CMake 后 `--reconfigure` 全量构建确认 |
| **单文件巨改难 review** | StudioSim 1925 行一次性重写易出错 | 按 M3→M4→M5→M6 分步：先拆 UI（玩法不变）、再换底座（仍 box）、再接 rig、最后补分层 |

---

## 9. 验证与验收

### 9.1 构建矩阵（来自 `src/CMakeLists.txt`）

- 改 `src/Gameplay/Sim/`（`NextGameplay`）：`./gnb build gkNextRenderer gkNextUnitTests`（确认 engine/gameplay API 面 + 全消费者链接闭包）+ 受影响 app。
- 改单个 app：`./gnb build AirportSim` / `./gnb build StudioSim`。
- 改了 CMake（`NextGameplay` 新增 `ScadLoader`/`NextAI` 链接边、去 QuickJS 门禁、新增 `.cpp` 未被 glob 收录）：加 `--reconfigure`。
- 终验：`./gnb build --reconfigure` 全量（确认 DAG 无环、所有 target 链接通过）。

### 9.2 视觉验证（`gnb shot`）

- `gnb shot --target AirportSim` / `--target AirportSim --ui`：M2 基线等价。
- `gnb shot --target StudioSim` / `--target StudioSim --ui`：M3 UI 等价、M5 角色升级、M6 全流程。
- 机制：渲染到 `--agent-validation-frames`（默认 90）后截图到 `out/build/<preset>/screenshots/agent_validation.jpg` 并自动退出，隐藏窗口不抢焦点。agent 直接读该图肉眼判断。

### 9.3 单元测试（`gkNextUnitTests`，对照现有 `Test_ScadRig`/`Test_ScadLoader`）

- `Sim::FAnchorMap`：从最小场景解析锚点、category 分桶、claim/release、seat slot。
- `Sim::FCharacterPool`：spawn→MoveTo→Arrived→Despawn；分离力使两 agent 不重叠；NavGrid 未就绪时直线回退。
- `Sim::FScadRigVisual`：hint→clip 映射（缺失回退 idle）、SetMoveSpeed 缩放 playSpeed。

### 9.4 Demo 验收

- AirportSim：行为与重构前一致（§4）。
- StudioSim：`./gnb run StudioSim` → 员工以 ScadRig 角色到岗、按日程/LLM 决策移动；晨会立项→生产推进（四仪表/飘字）→注入事件→聚集群体决策→上线复盘结算；LLM 不可用时全程 fallback 可演示（§5.7 全绿）。

---

## 10. Round 2 展望（本轮仅预留，不实现）

### 10.1 串行 LLM 决策调度 → `Sim::FDecisionScheduler`（具体类）

依赖已放开（`NextGameplay` 现可 link `NextAI`），**无需**原先的抽象 pump 绕行方案——直接把「单在途 + worker 入队 + 主线程 drain/apply + generation 守卫 + 轮询游标 + 决策日志 + fallback」抽成持有 `NextAI::FAIService*` 的具体基类，app 只重写「构造 prompt」与「解析+apply 结果」两个虚函数：

```cpp
// 设计草案（Round 2）：公共层直接用 NextAI
class FDecisionScheduler {           // 通用串行调度纪律
public:
    void Tick(NextAI::FAIService* ai, /*决策者范围 + 选取策略*/ ...);
    bool InFlight() const; int DecisionsMade() const; const auto& Log() const;
protected:
    virtual std::string BuildPrompt(/*某决策者上下文*/) = 0;          // app 实现
    virtual void ApplyResult(/*decisionId, 解析后的动作*/) = 0;       // app 实现
    virtual void ApplyFallback(/*ai 不可用/解析失败时*/) = 0;         // app 实现
};
```
AirportSim/StudioSim 各派生一个子类承载领域 prompt/apply。两版 `DecisionScheduler` 在 M6 已写成同形（同字段/同纪律），二次抽取近乎机械。**因依赖障碍已除，此项可视情提前并入首批**（见 §3.4）。

### 10.2 其它候选
- `Sim::FWorldClock`（游戏分钟 + 倍速 + pause + dayIndex + 可选日夜光照）。
- `Sim::FObservationCamera`（总览 + 锁定跟踪 + 点选 + 滚轮缩放）。
- `Sim::FWorldOverlay`（世界→屏幕投影 + 气泡/名牌/mood 图标/飘字粒子）。
- `Sim::FPerception`（邻居/队列/事件扫描 → 决策时刻）。
- 共享枚举/工具：`EMood`、`FDecisionResult`、`MinutesToHHMM`、mood 图标。

---

## 11. 落地速记（给执行 agent）

1. 读 §2.2 依赖策略——`NextGameplay` 已提升为高层模块，**可** link `ScadLoader`/`NextAI`；M1 别忘了那条 CMake link 改动（加 `NextGameplay` 到 module 链接清单）+ `--reconfigure`。
2. 按 §6 里程碑顺序走，**先 AirportSim 验证公共层，再动 StudioSim**。
3. 每步只构建受影响目标（§9.1），改公共层必带 `gkNextRenderer gkNextUnitTests`。
4. 用 `gnb shot` 而非手动开窗截图（§9.2）。
5. StudioSim 巨改拆成 M3(拆UI)→M4(换底座)→M5(接rig)→M6(补分层)，每步可独立验收。
6. 完成后补 `AGENT_GUIDE/SimKit.md`（公共层使用手册，对照 `AGENT_GUIDE/ScadRig.md`）。
