# CharacterDemo + NextGameplay 代码结构梳理

本文梳理 **CharacterDemo** 小游戏（`src/Application/Game/CharacterDemo/`）及其依赖的**共享游戏层 NextGameplay**（`src/Engine/NextGameplay/`）的代码结构。两者是一套"引擎共享层 + 一个示例消费者"的组合，理解时需要一起看，所以合并成一篇。

> 与 `AGENT_GUIDE/Brotato3D.md`、`AGENT_GUIDE/MagicaLego.md` 同属"游戏代码梳理"系列。Brotato3D 是**自包含**的上帝类游戏；CharacterDemo 则相反——它把可复用的角色/AI 能力沉到 `NextGameplay` 引擎库里，自己只做**编排 + 输入 + 调试 UI**。

---

## 1. 一句话定位

- **NextGameplay**（引擎库 `NextGameplay.lib`）：与具体游戏无关的**角色 & AI 原语**——角色门面、4 个 ECS 组件、A* 导航网格、寻路跟随、骨骼模型查找工具、反射注册。**无任何 CharacterDemo 依赖**，理论上可被任意游戏复用。
- **CharacterDemo**（可执行）：NextGameplay 目前**唯一的消费者**，是一个"第三人称/第一人称角色 + 一个 AI 敌人 bot"的演示。负责输入、相机、投射物、调试 UI，以及把 NextGameplay 的能力拼起来。

依赖与构建（`src/CMakeLists.txt`）：

```
CharacterDemo (exe) ──links──► NextGameplay (lib) ──► gkNextEngine (lib)
gkNextUnitTests (exe) ─links──► NextGameplay        （Test_GameplayComponents.cpp）
```

> **改 NextGameplay 必须同时验证两个目标**：`CharacterDemo` 和 `gkNextUnitTests`（后者覆盖组件反射/重置/动画映射）。目前没有别的消费者，但它是**公共引擎层**，改 API 要按"会有第二个游戏来用"的心态对待。

---

## 2. 分层与文件地图

### 2.1 NextGameplay（共享层，`src/Engine/NextGameplay/`）

| 文件 | 角色 | 关键内容 |
| --- | --- | --- |
| `Gameplay/GameplayTypes.h` | **枚举 + 名字 helper** | `ECharacterAnimState/ECharacterMovementMode/ECharacterControlSource/EAIAgentState/EBehaviorTreeStatus/EBehaviorDebugState` + 各自的 `GetXxxName()` inline 转换 |
| `Gameplay/GameplayMath.hpp` | 数学工具 | `NormalizeHorizontalOrZero`、`AdvanceYawToward`（朝目标方向限速转 yaw） |
| `Character/CharacterActor.{h,cpp}` | **角色门面（facade）** | 组合控制器 + 3 个组件 + 骨骼模型，提供 `Initialize/SyncTransform/UpdateAnimation/...` 高层 API |
| `Components/CharacterControlComponent.{h,cpp}` | ECS 组件：控制意图 | move/look intent、desiredSpeed、sprint、jump（一帧的输入快照） |
| `Components/CharacterGameplayComponent.{h,cpp}` | ECS 组件：角色运行态 | 第一人称/Foot IK/移动模式/眼高 + 骨骼根、SkinnedMeshComponent 列表、`PlayAnimation`/`ConfigureFootPlacementIK` |
| `Components/CharacterAnimationComponent.{h,cpp}` | ECS 组件：动画状态机 | 把"速度/朝向/policy"映射到 12 种 `ECharacterAnimState` 并驱动 clip 切换 |
| `Components/AIAgentComponent.{h,cpp}` | ECS 组件：AI 黑板 | 状态机当前/目标状态、巡逻点、行为树各节点调试态、`FPathFollower` |
| `AI/NavGrid.{h,cpp}` | **导航网格 + A***  | 用场景 BVH 射线采样可走性，A* 寻路 + 路径平滑 + 可达性掩码；支持脏区域增量重建 |
| `AI/PathFollower.h` | 路点跟随（header-only） | `GetMoveDirection`/`NeedsRepath`/`SetPath` |
| `Utilities/SceneNodeUtils.{hpp,cpp}` | 场景节点工具 | 查找 append 进来的骨骼根、收集 SkinnedMeshComponent、递归设可见性/射线可见性/关物理 |
| `Reflection/GameplayReflectionRegistry.{h,cpp}` | 反射注册 | `RegisterGameplayReflection()` 一次性把 4 个组件注册进 entt::meta |

### 2.2 CharacterDemo（应用层，`src/Application/Game/CharacterDemo/`）

| 文件 | 角色 | 关键内容 |
| --- | --- | --- |
| `CharacterDemoGameInstance.{hpp,cpp}` | 入口 + 编排 | `OnTick` 主循环、输入、相机、投射物、模型异步加载、NavGrid 重建、HUD |
| `CharacterDemoAIController.{hpp,cpp}` | **AI 行为树** | Patrol/Chase/Evade/Attack 状态决策 + 行为树执行；通过 `FCallbacks` 回调与 GameInstance 解耦 |
| `CharacterDemoAIDebugUI.{hpp,cpp}` | AI 调试 UI | 行为树覆盖层、NavGrid 覆盖层（纯 ImGui，吃一个 `FContext` 快照） |
| `CharacterDemoConfig.hpp` | 数值配置 | Player/Camera/Projectile/Animation/AI 五组可调参数（**纯 C++ 结构体，非 JSON**） |

> 注意 CharacterDemo 的数值是**硬编码结构体**（`CharacterDemoConfig`），不像 Brotato3D 走 JSON。要调手感直接改 `CharacterDemoConfig.hpp` 默认值，或运行时用 HUD 上的 slider。

---

## 3. NextGameplay 核心：门面 + 组件的分工

这是理解这套代码最关键的一点。`CharacterActor` 是一个 **facade（门面）**，它本身不是 ECS 组件，而是"把一个角色需要的东西打包"的普通类：

```
CharacterActor (facade, 普通类)
 ├── controller            : NextCharacterController（引擎物理胶囊）
 ├── actorRoot             : 场景根节点，挂着下面三个组件
 ├── gameplay  →  CharacterGameplayComponent   (ECS, 反射: 第一人称/IK/移动模式/眼高 + 骨骼引用)
 ├── animation →  CharacterAnimationComponent  (ECS, 反射: 动画状态机)
 ├── control   →  CharacterControlComponent    (ECS, 反射: 每帧控制意图)
 └── skinnedRoot / skinnedMeshComps : append 进来的骨骼模型
```

**为什么既有 facade 又有组件？**
- **组件**走 ECS + entt::meta 反射：编辑器 PropertyPanel 能显示/编辑它们，其它系统能通过场景查询拿到。
- **facade** 提供热路径上的便捷直接访问（`actor.controller.GetPosition()`、`actor.SyncTransform(...)`），不用每次 `GetComponent<>`。

**已知的状态镜像（重要，改动时小心）**：`CharacterActor` 持有 `skinnedRoot / primarySkinnedMeshComp / skinnedMeshComps / appendRootName / modelLoaded / modelLoadRequested`，**同时** `CharacterGameplayComponent` 也持有同名字段；`CharacterActor` 的 setter（`SetModelLoaded` 等）会把值**同步写进 gameplay 组件**。这是 facade/组件并存导致的真实重复——见 §7。

### 3.1 控制意图（intent）模式

输入不直接驱动角色，而是先写进 `CharacterControlComponent` 的"意图"，再由 controller 消费：

```
玩家输入/AI决策 → control->SetMoveIntent/LookIntent/DesiredSpeed/Sprinting/JumpRequested
              → controller.Update(moveIntent, desiredSpeed, ConsumeJumpRequested(), dt)
              → 每帧末 control->ClearFrameState()
```

`jumpRequested` 用 `Consume*()` 语义（取走即清零），和 Brotato3D 的波次事件是同一种"一次性事件"惯例。

### 3.2 动画状态机（CharacterAnimationComponent）

`UpdateAnimation(gameplayComponent, input, footIK, debugDraw)` 把 `FCharacterAnimationUpdateInput`（速度、参考前/右向、指令移动方向、policy 等）映射到 12 种 `ECharacterAnimState`，再切 clip。`ECharacterAnimationPolicy` 决定玩家相机相对 / 玩家移动对齐 / AI 各状态用哪套动画选择逻辑。clip 名字由 `MapAnimationNames()` 从实际加载到的动画里按候选列表挑（带 fallback 链），所以**换骨骼模型不需要改代码**，只要动画名能命中候选。

### 3.3 AI：NavGrid + PathFollower + 行为树

- `FNavGrid`：`Build()` 用场景 CPU BVH 朝下射线采样每个格子的地面高度/可走性/头顶净空；`FindPath()` 是 A* + `SmoothPath()` 视线拉直；`RebuildDirtyRegion()` 支持场景变动后只重算脏矩形。
- `FPathFollower`：拿到 A* 路点后做"到点切下一点 + 需要时重寻路"。
- **行为树在应用层**（`CharacterDemoAIController`）：`DetermineDesiredState` 决定 Patrol/Chase/Evade/Attack，`RunBehaviorTree` 执行；各节点状态回写进 `AIAgentComponent` 的 `behaviorXxxStatus_` 供调试 UI 显示。

---

## 4. CharacterDemo 数据流（`OnTick` 等）

```
OnInit          探测可选资产(KayKit+Mannequin) → KeepCPUMeshData=true → 请求加载场景
BeforeSceneRebuild  注入胶囊占位模型 + 投射物模型/材质（只做一次, sceneHelpersInjected_ 守门）
OnSceneLoaded   建玩家 controller → CharacterActor.Initialize → 占位可视体
                → InitAIBot（建 AI actor + 绑定 AIController 回调 + 收集巡逻点）
                → NavGrid.Build → 异步 append 两个 Mannequin_Medium.glb（玩家 + AI, 见下）
OnTick          1. 若骨骼模型已加载完 → TryInit{,AIBot}CharacterModel（解析骨骼根 + 装动画 + 移除占位体）
                2. RefreshNavGridFromSceneDirtyRegion（场景变了就增量重建导航网格）
                3. 读输入 → 组合 moveDir → SetControlIntent → controller.Update
                4. UpdateCharacterFacingYaw → UpdateCharacterNode（SyncTransform）
                5. UpdateAnimationState（玩家）→ UpdateAIBot（AIController.Update 跑行为树）
OnSceneUnloaded ResetCharacterState（销毁 actor/AI/导航）
```

**双 Mannequin 异步 append（容易看懵的点）**：`OnSceneLoaded` 里对同一个 `Mannequin_Medium.glb` 用 `.append=true` 请求加载**两次**——这是故意的，玩家和 AI bot 各要一个骨骼实例。靠 `FindAppendedCharacterRoot(scene, baseName, ordinal)` 的 `ordinal`（玩家用基名 `Mannequin_Medium`，AI 找 `Mannequin_Medium_1`）区分两个实例。模型是异步加载的，所以 `OnTick` 每帧轮询 `modelLoadRequested && !modelLoaded` 直到骨骼根出现才接管。

**AIController 解耦**：`CharacterDemoAIController` 不直接依赖 `CharacterDemoGameInstance`，而是通过 `FCallbacks`（`getPlayerEyePosition`、`hasLineOfSightToPlayer`、`spawnProjectile`、`updateAnimationState`、`updateNode` 等 std::function）回调宿主。要把这套 AI 搬到别的游戏，只需重新实现这些回调。

---

## 5. 约定速查

- **命名/风格**：同引擎规范（PascalCase 类型/函数、camelCase 变量、尾下划线私有成员）。NextGameplay 类型用 `F`/`E` 前缀（`FNavGrid`、`EAIAgentState`）。
- **枚举 → 字符串**：统一用 `NextGameplay::GetXxxName()`（在 `GameplayTypes.h`）。**不要在消费端再写一遍 switch**——本次整理已经把 CharacterDemo 里几处重复的 switch 折叠回这些共享 helper（见 §7）。
- **坐标/朝向**：玩法在 XZ 平面，yaw = `atan2(dir.x, dir.z)`。转向限速统一用 `NextGameplay::AdvanceYawToward`。
- **控制意图**：永远 `SetControlIntent → controller.Update → ConsumeJumpRequested`，别让输入直接改 transform。
- **反射**：`OnInit` 里调一次 `NextGameplay::RegisterGameplayReflection()`（内部有幂等守卫）。新增反射组件就在 `GameplayReflectionRegistry.cpp` 里加一行注册。
- **可选资产**：CharacterDemo 依赖 KayKit + Mannequin 资产包，缺失时 `OnInit` 弹框提示跑 `scripts/fetch-paks.{sh,bat} --optional` 而不是崩溃。

---

## 6. 验证

```bash
gnb.bat build CharacterDemo          # Windows（macOS/Linux: ./gnb build CharacterDemo）
gnb.bat build gkNextUnitTests        # 改了 NextGameplay 必须连带构建
./out/build/windows/bin/gkNextUnitTests "[Gameplay]"   # 跑 4 个 gameplay 组件单测
./gnb run CharacterDemo              # 看到 "uploaded scene [...] to gpu" 即初始化通过
```

运行时调试键：`F8` AI 调试菜单 →（菜单开着时）`1` 行为树覆盖层 / `2` NavGrid 覆盖层；`F7` Foot IK、`F9` IK 调试、`V` 切第一/第三人称、`Tab` 切移动模式。

---

## 7. 代码健康：本次整理 + 仍可简化项

**本次已做（行为不变，已 build CharacterDemo + gkNextUnitTests + 跑通 `[Gameplay]` 单测）：**

- **把消费端重复的枚举→字符串 switch 折叠回 NextGameplay 共享 helper**（核心"去牵连"工作）：
  - CharacterDemo `GetMovementModeName()` → 直接用 `NextGameplay::GetCharacterMovementModeName()`（删掉重复 switch + 该成员函数）。
  - `CharacterDemoAIController::GetStateName()` → 委托 `NextGameplay::GetAIAgentStateName()`。
  - AIDebugUI 局部 `GetBehaviorStateLabel()` → 用 `NextGameplay::GetBehaviorDebugStateName()`（删掉局部函数）。
  - 效果：原本"定义了却没人用"的三个共享 helper 现在真正被使用，消费端少了三处重复逻辑。
- **删除 CharacterDemo 里纯转发的节点工具成员**：`SetNodeRayCastVisibilityRecursive` / `DisableNodePhysicsRecursive`（无调用点，死代码）+ `SetNodeVisibilityRecursive`（仅 1 处调用，改为直接用 `NextGameplay::` 自由函数），并删掉随之失效的 4 条 `using` 声明。

**仍存在、可作为后续优雅化目标（本次未动，避免在共享层引入风险）：**

| 现象 | 位置 | 建议 |
| --- | --- | --- |
| `CharacterActor` 与 `CharacterGameplayComponent` 状态镜像 | `CharacterActor.{h,cpp}` | `skinnedRoot/skinnedMeshComps/modelLoaded/...` 两边各存一份并手动同步。可让 facade 只存"指向组件的引用 + 转发 getter"，组件作为唯一数据源，消除 setter 里的双写 |
| `UpdateCharacterFacingYaw` 重复转向数学 | `CharacterDemoGameInstance.cpp` | 与 `NextGameplay::AdvanceYawToward` 逻辑高度重合，差别只在"用速度还是指令方向"选向。可先算 facing 方向再调共享函数 |
| `GetCharacterControlSourceName` | `GameplayTypes.h` | 枚举名 helper 矩阵里唯一仍未被使用的；保留以维持 API 对称，未来 ControlSource 上 UI 时即可用 |
| `IsGameplayReflectionInitialized()` | `GameplayReflectionRegistry.{h,cpp}` | 公共诊断查询，当前无人调用；保留为 `RegisterGameplayReflection` 的伴生 API（同 Brotato3D 保留 `PlayShopCantBuySfx` 的判断口径） |

---

## 8. 进一步阅读

- [`AGENT_GUIDE/Brotato3D.md`](Brotato3D.md) / [`AGENT_GUIDE/MagicaLego.md`](MagicaLego.md) —— 另两个 C++ 游戏的同类梳理
- [`AGENT_GUIDE/ReflectionSystem.md`](ReflectionSystem.md) —— entt::meta 反射（CharacterDemo 的组件就靠它进编辑器）
- `docs/plans/2026-05/refactor/phase-05-nextgameplay.md` —— NextGameplay 当初从游戏里抽取出来的重构计划（历史背景）
- [`AGENTS.md`](../AGENTS.md) —— 引擎全局规范、构建/命名/目录约定
