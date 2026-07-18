# CharacterDemo 与 NextGameplay 角色层

CharacterDemo 是 `src/Gameplay/` 中 CharacterActor、角色组件、NavGrid 和基础 AI 的示例消费者。`NextGameplay` 现在还包含 camera、rig 与 Sim Kit，并被 AirportSim、StudioSim、CitySolSim、Editor/Remote 等多个 target 使用；不要再把它视为 CharacterDemo 私有库或只有一个消费者。

## 当前范围

CharacterDemo 提供：

- 第一/第三人称玩家角色、物理 character controller、跑跳和镜头；
- 异步 append 两个 `Mannequin_Medium.glb`，分别作为玩家与 AI visual；
- Patrol/Chase/Evade/Attack AI、A* NavGrid、可见性和投射物；
- animation state、Foot IK、physics/graphics、behavior tree 与 NavGrid 调试 UI；
- 一个只含 camera、环境和 ground 的 `CharacterPlayground.proc`。

旧 `PrefabSceneWorkflow.md` 中的 KayKit `StarPillar`/`Scaffold`/`PlatformRamp` 搭建流程已不在当前代码中。`CharacterPlaygroundScene.cpp` 没有 prefab API 或这些实例；不要按旧相对坐标计划继续补场景。若要重新制作平台场景，应先作为新需求设计资产来源、碰撞和验收。

## NextGameplay 分层

| 目录 | 当前职责 |
| --- | --- |
| `Gameplay/Character/` | `NextCharacterController`、`CharacterActor` facade、controller debug draw |
| `Gameplay/Components/` | control intent、gameplay/model runtime、animation state 与 AI blackboard component |
| `Gameplay/AI/` | `FNavGrid`、A*、reachability/dirty-region rebuild、`FPathFollower` |
| `Gameplay/Gameplay/` | 共享枚举、名字 helper 和水平移动/转向数学 |
| `Gameplay/Reflection/` | 四个角色/AI component 的显式反射注册 |
| `Gameplay/Camera/` | model-view controller 与 focus animation；不属于 CharacterDemo 专用相机 |
| `Gameplay/Rig/` | 通用 rigid-body rig instance/animator |
| `Gameplay/Sim/` | anchor、角色池、ScadRig visual 等产品仿真复用层，见 [SimKit](SimKit.md) |

Engine 核心不能依赖 Gameplay；application/module 通过链接 `NextGameplay` 使用它。修改共享 header 前先用 CMake consumer 关系判断影响面，不能只因 CharacterDemo 编译通过就断言所有 Sim/Rig consumer 正确。

## CharacterActor 契约

`CharacterActor` 是普通 facade，不是 ECS component。它组合：

```text
NextCharacterController
actorRoot
CharacterGameplayComponent
CharacterAnimationComponent
CharacterControlComponent
skinnedRoot / SkinnedMeshComponent views
```

输入/AI 应先写 `CharacterControlComponent` intent，再调用 controller；jump 使用 `ConsumeJumpRequested()` 的一次性语义。物理位置和 facing 通过 `SyncTransform()` 写回 scene，render/animation 不能反向成为 controller 真值。

facade 当前仍镜像 `skinnedRoot`、skinned component 指针、append root name 与 model loading flags，并由 setter 同步到 gameplay component。修改这些字段时必须保持两边一致；不要直接只改一侧 public member。若以后消除镜像，应让 component 成为唯一状态源并一次性迁移所有调用点。

`RegisterGameplayReflection()` 显式注册 CharacterGameplay、CharacterAnimation、CharacterControl 与 AIAgent component，内部有幂等守卫。新增 Gameplay 反射 component 要同步 registry；可选模块注册规则仍遵循 [ReflectionSystem](ReflectionSystem.md)。

## CharacterDemo 生命周期

```text
CreateGameInstance
  → 注册 CharacterPlayground.proc
OnInit
  → 注册 Gameplay reflection
  → 检查可选 Mannequin 资产
  → KeepCPUMeshData=true
  → 加载命令行 scene 或 CharacterPlayground.proc
BeforeSceneRebuild
  → 注入占位 capsule、AI/projectile model 与材质
OnSceneLoaded
  → 创建 player/AI controller 与 facade
  → 从 CPU acceleration structure 建 NavGrid
  → append 两份 Mannequin，异步等待解析
OnTick
  → resolve skinned roots、消费输入、更新 controller/animation/AI
  → 对 scene dirty bounds 增量重建 NavGrid
OnSceneUnloaded
  → 清 controller、facade、AI 与 navigation runtime
```

append 两次相同 glTF 是有意的：玩家解析 `Mannequin_Medium`，AI 解析后续 ordinal 对应的 root。base scene mesh 必须保留 CPU copy，因为 append 会重建 scene mesh buffer。

NavGrid 从 scene CPU acceleration structure 采样 walkability/clearance，并支持 `RebuildDirtyRegion()`。行为树留在 application 的 `CharacterDemoAIController`，通过 callbacks 访问宿主的目标位置、LOS、投射物、animation 与 node 更新；不要把 demo 的 combat 状态机塞进通用 NavGrid/component。

## 配置、资产与输入

数值来自 `CharacterDemoConfig.hpp` 的 C++ struct，不是 JSON/CVar。`ConfigureCVars()` 只覆盖少量 renderer 默认值。

必需的 `assets/models/characters/Mannequin_Medium.glb` 属于可选 pak；缺失时运行：

```bash
./gnb.sh paks fetch optional
# Windows: gnb.bat paks fetch optional
```

常用输入以当前 HUD/`OnKey` 为准：WASD、Shift、Space、mouse，`V` 切视角、Tab 切移动模式、LMB 发射，F1/F2/F8/F9 控制调试视图。不要从旧指南恢复已经删除的 prefab 审核键或场景步骤。

## 验证

CharacterDemo/角色组件改动：

```bash
./gnb.sh build CharacterDemo gkNextUnitTests
./out/build/<preset>/bin/gkNextUnitTests "[Gameplay]"
./gnb.sh run CharacterDemo
```

运行应加载 playground、创建两个 controller，并最终解析两份 skinned root。至少检查 FPS/TPS、跑跳、animation、AI 四状态、投射物、NavGrid dirty rebuild 与 scene reload。

若改动的是 `Gameplay/Sim`、Rig、Camera 或公共 header，还要构建实际受影响的 AirportSim/StudioSim/Editor/Remote 等 consumer；CharacterDemo 不覆盖这些子系统。仅文档改动无需构建。
