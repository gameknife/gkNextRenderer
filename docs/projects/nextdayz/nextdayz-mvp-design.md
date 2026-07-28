---
title: "NextDayz 类 DayZ 生存射击 MVP 设计与开发步骤"
category: project
status: 已实现
owner: NextDayz
created: 2026-07-21
last_updated: 2026-07-21
---

# NextDayz — 类 DayZ 生存射击 MVP 设计与开发步骤

> 本文是把 `assets/scad/proc/coldwar/riverland_1km.scad`（冷战开放地图）与 ScadRig（可自由组合的刚体骨骼角色/动作）当作既有基础设施，在其上搭建一个**最小可玩的类 DayZ 第一人称生存射击 demo**（target 名 `NextDayz`）的设计方案与分阶段开发步骤。文档面向后续接手的 AGENT，所有系统都给出落地文件、复用 API 与验收标准。
>
> MVP 与复杂 3C 均已实现。蹲姿、四方向三步态、分层瞄准/后坐力和 Loot 动作的现行架构
> 以 [NextDayz 复杂 3C 与 ScadRig 分层动画设计](nextdayz-3c-scadrig-design.md) 为准。

## 0. MVP 范围界定

**要做（本 MVP）：**

- 最简 3C：物理角色控制器 + 第一人称相机（可切第三人称）；WASD 行走、Shift 奔跑、Space 跳、鼠标转向。
- 枪械：装备主/副武器、切枪、开火射击子弹（hitscan）、瞄准（ADS）、换弹。
- 拾取：在地图里搜刮武器 / 弹药 / 服装 / 杂物；靠近 + 准星指向 → `[E]` 拾取。
- 背包：极简列表；可装备武器、可装备服装。
- Time of Day：昼夜循环（太阳方位/强度随游戏时间变化），可选一档"阴天"天候。
- 极简 UI：准星、当前武器/弹量、时钟、交互提示、背包面板。

**不做（本 MVP 明确排除，别顺手加）：**

- ❌ AI 敌人 / NPC / 僵尸。
- ❌ 角色状态系统（血量、饥饿、口渴、体温、受伤、出血）。物品的"消耗"只做拾取入包，不做使用后果。
- ❌ 联机 / 存档持久化 / 基地建造 / 载具驾驶。
- ❌ 复杂 UI（物品拖拽、装备格子图、地图 M）。列表即可。

## 1. 设计支柱

1. **地图即内容**：不新建关卡。直接跑 `riverland_1km.scad`，它已含可行走地形、河/桥、8 个 POI 与散布的武器/弹药/物资道具。拾取物**从地图既有节点派生**，不另写刷新点表（详见 §5.4）。
2. **物理零配置**：场景构建时引擎自动为网格节点建静态碰撞体（§2.3），地形/建筑/道具加载即可行走、可挡子弹，无需手写碰撞。
3. **复用 Gameplay 层**：3C 用 `NextCharacterController`，相机用 `OverrideRenderCamera`，角色视觉用 ScadRig（`FRigInstance`/`FRigAnimator`），昼夜用 AirportSim 同款 `TimeSystem` 思路。新代码只写"游戏规则"，不重造引擎能力。
4. **第一人称优先**：DayZ 手感以 FPS + ADS 为主，第三人称仅作切换观察。

## 2. 复用的基础设施（已核对事实）

> 下列都是本文动手前已在代码中核实的事实，标注了文件位置，AGENT 可直接依赖。

### 2.1 地图与运行时加载

- 地图：[assets/scad/proc/coldwar/riverland_1km.scad](assets/scad/proc/coldwar/riverland_1km.scad)。1km²、`176×176` 地形 cell（`<180²` 故地形是**单个** MeshShape，可行走/寻路）；主公路东西横贯、河 + 混凝土桥、8 个 POI（西部村庄 / 桥西加油站 / 桥东碉堡 / 东北军事基地 / 东南小镇 / 河畔工厂 / 西山通信站 / 北坡坠机点 / 西南湖畔营地）。
- `.scad` **可运行时直接加载**：注册加载器后请求即可。AirportSim 就是这么做的（[AirportSimGameInstance.cpp:72-94](src/Application/Game/AirportSim/AirportSimGameInstance.cpp:72)）：

  ```cpp
  Modules::Scad::Register();                 // OnInit 里注册 .scad 场景加载器
  GetEngine().RequestLoadScene({.filename = "assets/scad/proc/coldwar/riverland_1km.scad"});
  ```

### 2.2 场景节点命名（拾取的基础）

- scad 里**每一次 user module 调用都会生成一个同名场景节点**（`node.name == 模块名`），见 [FScadEvaluator.Detail.h:889-925](src/Modules/ScadLoader/FScadEvaluator.Detail.h:889) 与 [FScadLoader.cpp:254](src/Modules/ScadLoader/FScadLoader.cpp:254)。
- 因此地图里的 `cw_wpn_ak(seed=1)`、`cw_item_can(...)` 等会成为名为 `cw_wpn_ak`、`cw_item_can` 的独立节点，可通过 `scene.Nodes()` 遍历，每个节点有：`node->GetName()`、`node->WorldTranslation()`、`node->GetInstanceId()`、`node->GetComponent<RenderComponent>()`。
- 已有工具 `NextGameplay::Sim::FAnchorMap`（[AnchorMap.h](src/Gameplay/Sim/AnchorMap.h)）按名字前缀 + 分隔符 `_` 归类节点，可直接借鉴其遍历/归类写法给拾取系统。

### 2.3 物理自动从场景生成

- 场景构建时，引擎对**每个可 raycast、索引数 `< 65535*3` 的网格节点自动创建静态 MeshShape 碰撞体**，见 [Scene.Build.cpp:292-348](src/Engine/Assets/Core/Scene.Build.cpp:292)。地形（约 6.2 万三角形，单 mesh）、建筑、道具因此都自带碰撞。
- 结论：玩家胶囊能踩在地形上、被墙/车挡住、子弹能打中它们——**无需游戏侧手写任何碰撞体**。

### 2.4 角色控制器（3C 的"C"）

- `NextCharacterController`（[NextCharacterController.h](src/Gameplay/Character/NextCharacterController.h)，Jolt 后端）：
  - `Create(NextPhysics*, FCharacterControllerSettings)`；`settings` 含 `height/radius/maxSlopeAngle/maxStepHeight/mass/initialPosition`（[NextPhysics.hpp:58](src/Engine/Runtime/Subsystems/NextPhysics.hpp:58)）。
  - `Update(const glm::vec3& inputDir, float speed, bool jump, float dt)`——每帧物理前调；**走/跑就是传不同的 `speed`**。
  - `GetPosition() / GetLinearVelocity() / IsOnGround()`。
- 更高层 facade `CharacterActor`（[CharacterActor.h](src/Gameplay/Character/CharacterActor.h)）把 controller + 控制/动画组件 + 蒙皮视觉打包，但它偏向 **Mannequin 蒙皮**路径。本项目角色视觉走 ScadRig（§5.2），所以**直接用 `NextCharacterController` + 自建 ScadRig 视觉**更干净，不必套 `CharacterActor` 的蒙皮镜像逻辑。

### 2.5 相机覆盖（3C 的"Camera"）

- 游戏用 `OverrideRenderCamera(Assets::Camera& out)` 接管渲染相机，设置 `out.ModelView = glm::lookAt(eye, target, up)` 与 `out.FieldOfView` 即可。CharacterDemo 已有可用的 FPS/TPS 实现（[CharacterDemoGameInstance.cpp](src/Application/Game/CharacterDemo/CharacterDemoGameInstance.cpp) 的 `OverrideRenderCamera`），可整段借鉴：FPS 用眼高 + `forward`；TPS 用 `target - forward*distance`。

### 2.6 射击 / 交互射线

- 引擎级 hitscan：`GetEngine().RayCast(origin, dir, callback)`（[Engine.hpp:159-161](src/Engine/Runtime/Engine.hpp:159)），回调收 `Assets::RayCastResult{ HitPoint, Normal, T, InstanceId, MaterialId, Hit }`（[UniformBuffer.hpp:93](src/Engine/Assets/GPU/UniformBuffer.hpp:93)）。子弹命中、拾取准星命中都用它。
- 可选实体子弹（曳光/抛物）：`CreateBoxBody` + `AddForceToBody`，CharacterDemo 的 `FireProjectile/SpawnProjectile` 是范例。MVP 用 hitscan 做命中判定，视觉曳光可选。

### 2.7 玩家角色视觉（ScadRig）

- 现成角色资产：[assets/scad/characters/next_ra_soldier.scad](assets/scad/characters/next_ra_soldier.scad)，已含 `anim_idle` / `anim_walk` / `anim_fire` clip 与 `bone_root(loadout)`。
- 运行时管线（见 [AGENT_GUIDE/ScadRig.md](AGENT_GUIDE/ScadRig.md)）：
  `FScadRigLoader::LoadRig(.scad) → Assets::FRigAsset` →（`BeforeSceneRebuild` 注入 part model/material）→ `FRigInstance::Instantiate(scene, asset, desc, outBones)` → `FRigAnimator::Bind(...)`，每帧 `Play("idle"/"walk"/"fire", fade)` + `Update(dt)` + 写世界节点 TRS。
- 单角色可参考 ScadLibrary 的 `CharacterDesigner`/`FRigPreview`（[src/Application/Editor/ScadLibrary/CharacterDesigner.cpp](src/Application/Editor/ScadLibrary/CharacterDesigner.cpp)）——它就是"注入 → 实例化 → 播 clip"的单实例样板。染色/换装经 `FRigInstanceDesc::partMaterialIds`（[RigInstance.h:24](src/Gameplay/Rig/RigInstance.h:24)）。
- run 动画可用 `walk` clip 提速（`SetPlaySpeed(1.6f)`）；aim 姿态复用 `anim_fire` 或另作 `anim_aim`。

### 2.8 Time of Day

- 环境参数在 `scene.GetEnvSettings()`（`Assets::EnvironmentSetting`，[Model.hpp:28](src/Engine/Assets/Core/Model.hpp:28)）：`SunRotation`(0..1→π 扫方位)、`SunIntensity`、`SkyIntensity`、`HasSun`、`SkyIdx`；`SunDirection()` 由 `SunRotation` 推导。
- 现成昼夜范式：[AirportSim/TimeSystem.cpp](src/Application/Game/AirportSim/TimeSystem.cpp) —— 累计游戏分钟，`hour→SunRotation`、黎明/黄昏 smoothstep 调 `daylight`，据此调 `SunIntensity/SkyIntensity/HasSun`。可几乎照搬。

### 2.9 UI / 输入 / 生命周期

- 游戏基类 `NextGameInstanceBase`（[GameInstance.hpp](src/Engine/Runtime/GameInstance.hpp)）提供全部钩子：`OnInit / OnTick(dt) / OnDestroy`、`BeforeSceneRebuild / OnSceneLoaded / OnSceneUnloaded`、`OverrideRenderCamera`、`OnRenderUI`、`OnKey / OnCursorPosition / OnMouseButton / OnScroll`、`ConfigureCVars`、`RegisterAgentQueries`（给 `gnb validate` 暴露 `game.*` 查询）。
- HUD 直接用 ImGui（`OnRenderUI()` 内 `ImGui::Text/SliderFloat/...`），CharacterDemo 的 `OnRenderUI` 是范例。
- 拾取后隐藏道具：`renderComp->SetVisible(false)` + 物理体 `SetBodyActive(bodyId,false)`（节点物理体由 `node->GetComponent<PhysicsComponent>()` 取）。

## 3. 总体架构与文件布局

新建 target `src/Application/Game/NextDayz/`：

```text
src/Application/Game/NextDayz/
├── CMakeLists.txt
├── NextDayzMain.cpp              // CreateGameInstance 工厂
├── NextDayzGameInstance.hpp/.cpp // 生命周期编排、输入路由、相机覆盖、Tick 调度、HUD 汇总
├── NextDayzConfig.hpp            // 所有可调数值（速度/FOV/灵敏度/时标/拾取半径…）
├── Player/
│   ├── PlayerController.hpp/.cpp // 角色控制器 + 相机 yaw/pitch + 移动/跳/跑 + 视角切换
│   └── PlayerRigVisual.hpp/.cpp  // ScadRig 加载/注入/实例化/动画（跟随控制器）
├── Weapons/
│   ├── WeaponDefs.hpp            // 武器/弹药静态数据表（constexpr）
│   └── WeaponSystem.hpp/.cpp     // 装备/切枪/开火(raycast)/换弹/ADS/视图模型
├── Inventory/
│   ├── Inventory.hpp/.cpp        // 物品列表 + 装备状态（武器槽/服装槽）
│   └── LootSystem.hpp/.cpp       // 从场景节点扫拾取物 + 准星/邻近交互 + 拾取入包 + 隐藏节点
├── World/
│   └── TimeSystem.hpp/.cpp       // 昼夜循环（改编自 AirportSim）
└── UI/
    └── NextDayzHUD.cpp           // 准星/弹量/时钟/交互提示/背包面板（ImGui）
```

**分层原则**：`NextDayzGameInstance` 只做编排（收输入→更新各系统→驱动相机/HUD）。业务状态归各系统所有，别把武器/背包状态塞进 GameInstance 成员堆里。所有魔法数字进 `NextDayzConfig.hpp`（对齐 [CharacterDemoConfig.hpp](src/Application/Game/CharacterDemo/CharacterDemoConfig.hpp) 的写法）。

**CMake 注册**（对齐 AirportSim）：

```cmake
# src/Application/Game/NextDayz/CMakeLists.txt
file(GLOB_RECURSE nextDayzSources CONFIGURE_DEPENDS "*.cpp" "*.hpp" "*.h")
add_executable(NextDayz ${nextDayzSources} ${GK_DESKTOP_MAIN_SOURCE})
gk_configure_application(NextDayz MODULES ${GK_STANDARD_RUNTIME_MODULES} ScadLoader)
target_link_libraries(NextDayz PRIVATE NextGameplay)
```

再到 [src/Application/Game/CMakeLists.txt](src/Application/Game/CMakeLists.txt) 追加 `add_subdirectory(NextDayz)`。

## 4. 坐标与出生点约定

- scad 建模是 **Z-up**，引擎运行时是 **Y-up**；scad 场景在加载时经 `ScadToWorld*` 转换，游戏侧拿到的节点世界坐标已是引擎 Y-up。地面高度体现在世界 **Y**。
- 出生点策略（避免卡地里或悬空）：选一个 POI 的世界 XZ（如加油站/村庄），从高处（Y=200）向下 `RayCast` 命中地形得到地面 Y，`initialPosition = {x, groundY + 1.0, z}` 再交给控制器（Jolt 会自然贴地）。**不要**硬写 Y=0。
- 建议默认出生：桥西加油站附近（地图中部、平坦、旁边就有物资），便于第一时间验证拾取。具体世界坐标在 M0 用一次向下 raycast 打印出来固化到 `NextDayzConfig.hpp`。

## 5. 系统详设

### 5.1 3C：控制器 / 相机 / 移动 / 输入

**PlayerController 状态**：`yaw/pitch`（鼠标累积，pitch 夹 ±85°）、`firstPerson`、`isSprinting`、`isAiming`、`eyeHeight≈1.6`。

**每帧（`OnTick`）**：

1. 由 WASD 合成相机平面前/右向量（用 `yaw`，忽略 `pitch` 的水平分量），归一化得 `inputDir`。
2. `speed = isSprinting ? RunSpeed : WalkSpeed`（ADS 时再乘 `AimMoveScale≈0.5`）。奔跑与瞄准互斥（sprint 时强制退出 ADS）。
3. `controller.Update(inputDir, speed, jumpPressedThisFrame, dt)`。
4. 记录移动状态供动画/HUD（idle/walk/run）。

**相机（`OverrideRenderCamera`）**：

- forward 由 `yaw/pitch` 球面展开；up 由 right×forward。
- FPS：`eye = controllerPos + (0,eyeHeight,0)`；`FOV = isAiming ? AimFov(50) : BaseFov(75)`（ADS 平滑 lerp）。
- TPS（`V` 切换）：`target = pos+(0,camHeight,0)`；`eye = target - forward*camDistance`；滚轮调距离。

**输入映射（`OnKey/OnMouseButton/OnScroll/OnCursorPosition`）**：

| 输入 | 行为 |
| --- | --- |
| W/A/S/D | 移动 |
| Shift(按住) | 奔跑 |
| Space | 跳 |
| 鼠标移动 | 转向（需鼠标捕获；进游戏即捕获，`Esc`/打开背包时释放） |
| 鼠标左键 | 开火 |
| 鼠标右键(按住) | 瞄准 ADS |
| R | 换弹 |
| 1 / 2 | 切主 / 副武器 |
| Q | 快速切上一把 |
| E | 拾取 / 交互 |
| Tab 或 I | 开/关背包 |
| V | 切第一/第三人称 |
| 鼠标滚轮 | TPS 下调相机距离 |

灵敏度、速度、FOV、相机距离全部进 `NextDayzConfig.hpp`。

### 5.2 玩家 ScadRig 视觉与动画

**PlayerRigVisual** 职责（单实例，参考 `FRigPreview` 样板）：

- `OnInit`：`FScadRigLoader::LoadRig("assets/scad/characters/next_ra_soldier.scad", asset)`。
- `BeforeSceneRebuild`：把 rig 的 part model/material **注入**传入的 `models/materials`（拿到全局 model id / material id），填 `FRigInstanceDesc`。⚠️ 生命周期教训（ScadRig.md / AirportSim）：注入产物只能在 `BeforeSceneRebuild` 建，`OnSceneUnloaded` 只清 animator/节点指针，**不能提前销毁已注入的 model/material**。
- `OnSceneLoaded`：`FRigInstance::Instantiate(scene, asset, desc, outBones)` → `animator.Bind(...)`。
- 每帧：`SetWorldTransform(controllerPos, yaw)`；按移动状态 `animator.Play("idle"|"walk"|"fire")`（run = `Play("walk")` + `SetPlaySpeed(1.6)`）；`animator.Update(dt)`。
- **FPS 隐身**：第一人称时把 rig 世界节点设为不可见（或下沉/剔除），避免自己身体糊住镜头；仅保留武器视图模型（§5.3）。第三人称显示完整 rig。

**换装（服装）视觉**：MVP 采用**附件节点开关**而非重染色——如"钢盔"= 在头骨骼下挂一个 `cw_item_helmet` 模型节点，装备时 `SetVisible(true)`。整体 rig 变体/重染色（改 `partMaterialIds`）留作 MVP 后。

### 5.3 武器系统

**数据表（`WeaponDefs.hpp`，`constexpr`）** —— 只列 MVP 需要的：

```cpp
struct FWeaponDef {
    const char* id;            // "ak" / "mosin" / "shotgun" / "pistol" / "svd"
    const char* displayName;   // "AK-74"
    const char* modelScad;     // 视图模型用的 cw_wpn_* 资产/模块
    EAmmoType   ammo;          // Rifle545 / Rifle762 / Shotgun12 / Pistol9
    int   magSize;             // 弹匣容量
    float fireInterval;        // 射击间隔秒（RPM 换算）
    bool  fullAuto;            // ak/svd 连发；mosin/shotgun/pistol 单发
    float spreadHip, spreadAds;// 髋射/瞄准散布（弧度）
    float adsFovScale;         // ADS 目标 FOV
};
```

**WeaponSystem 运行时状态**：`slots[2]`（主/副，存 weaponId + 当前弹匣内弹数）、`activeSlot`、`lastFireTime`、`isReloading/reloadTimer`。

- **装备**：从背包把某武器装进指定槽；触发视图模型换成对应 `cw_wpn_*`。
- **切枪**（1/2/Q）：切 `activeSlot` + 播 rig `fire`/idle 过渡 + 换视图模型。
- **开火**（LMB）：
  1. 冷却/弹匣检查（空匣 → 播空仓 click，不发射）。
  2. 由相机 `eye/forward` + 散布抖动算射线方向；`GetEngine().RayCast(eye, dir, cb)`。
  3. 命中回调：记录 `HitPoint/Normal/InstanceId`；生成命中反馈（见下）。MVP **不做伤害**（无敌人/无状态），命中只做视觉。
  4. 弹匣 -1；`fireInterval` 冷却；full-auto 按住持续、单发每次抬起才可再射。
  5. 反馈（保持极简、复用现成能力）：枪口→命中点画一条**短命曳光线**（复用调试线/`DrawAdditionalPhysicsDebugOverlay` 同类 debug draw），命中点放一个 tiny impact marker 节点（短寿命）。抛物实体子弹（`CreateBoxBody`+力）作为可选替代。**不要**为此新造粒子系统。
- **换弹**（R）：`reloadTimer` 到点后，从背包该 `EAmmoType` 储备里补足弹匣（`min(magSize, 匣内 + 储备)`，扣减储备）。储备 = 背包里对应弹药 item 的 count。
- **ADS**（RMB 按住）：切 `spreadAds` + 请求 `WeaponSystem→PlayerController` 把相机 FOV lerp 到 `adsFovScale`，视图模型上抬居中。

**视图模型**：FPS 下把武器 `cw_wpn_*` 模型实例化成一个**相机视图空间节点**（每帧置于相机前方固定偏移，ADS 时 lerp 到居中略近）。TPS 下把该节点挂到 rig 右手骨骼。MVP 先做 FPS 视图空间节点即可。

### 5.4 背包与拾取

**Inventory（极简列表）**：

```cpp
enum class EItemKind { Weapon, Ammo, Clothing, Misc };
struct FItemStack { std::string id; EItemKind kind; int count; };
// 背包：std::vector<FItemStack>（可堆叠的 ammo/misc 合并 count）
// 装备：int equippedPrimary/equippedSecondary（指向背包武器）、
//       std::vector<std::string> equippedClothing;（helmet/backpack/vest）
```

MVP 不做容量/重量上限（可留一个大常量 `kMaxSlots` 防爆）。

**LootSystem —— 从地图节点派生拾取物**：

- `OnSceneLoaded` 遍历 `scene.Nodes()`，凡名字命中"地图物品→游戏物品映射表"（§6）者，登记为一条 `FLootEntry{ nodeId, worldPos, itemId, kind, count, looted=false }`。**这样拾取点始终跟随美术在地图里摆的道具**，不用另维护坐标表。
- 交互检测（每帧）：找"距玩家 `< kLootReach(≈2.2m)` 且准星射线朝向它/夹角最小"的未拾取 entry → 设为 `hoveredLoot`，HUD 显示 `[E] 拾取 {displayName}`。
- 拾取（E）：`hoveredLoot` 入背包（ammo/misc 堆叠、weapon/clothing 追加）；对应节点 `renderComp->SetVisible(false)` + 物理体 `SetBodyActive(false)`；entry `looted=true`。
- 容器类（`cw_wpn_crate` / `cw_prop_crate_ammo` / `cw_item_crate_supply`）：MVP 当作单次拾取，直接给一份对应物品（如弹药 crate → ammo ×N）。开箱掉落随机化留作 MVP 后。

**装备流程（背包面板内按钮）**：

- 武器：点"装备到主/副槽"→ `WeaponSystem` 载入该武器 + 换视图模型。
- 服装：点"穿戴"→ 记入 `equippedClothing` + 触发 §5.2 的附件节点显隐。

### 5.5 Time of Day 与天候

- `World/TimeSystem`：改编 [AirportSim/TimeSystem.cpp](src/Application/Game/AirportSim/TimeSystem.cpp)。累计 `gameMinutes`（`timeScale` 默认 60，即 1 真实秒 = 1 游戏分钟，一整天 24 分钟；可调）；每帧 `ApplyEnvironment(scene)`：
  - `hour = fmod(gameMinutes,1440)/60`。
  - `SunRotation = clamp((hour-6)/12, 0,1)`（06:00→18:00 东升西落）。
  - 黎明 05–07 升、黄昏 17–19 落，`daylight = min(rise,set)` smoothstep。
  - `HasSun = daylight>0.02`；`SunIntensity = kDaySun*daylight`；`SkyIntensity = kDaySky*(kNightFrac+(1-kNightFrac)*daylight)`。
- **天候（极简一档）**：一个 `bool overcast` + `weatherTimer`，偶发切换。overcast 时把 `SkyIntensity/SunIntensity` 乘 `≈0.6` 并（可选）调一个雾相关 cvar。MVP 只需让"天变阴/变亮"肉眼可见即可，不做降雨粒子。
- 起始时间设 08:00，保证首屏是白天好验证。

### 5.6 极简 HUD（`OnRenderUI`）

全部用 ImGui，背景透明、无边框、不抢输入（除背包面板）：

- **准星**：屏幕中心一个小十字/点（ADS 时收细或隐藏）。
- **右下**：`{武器名}  {匣内}/{储备}`，如 `AK-74  24 / 90`；换弹时显示 `Reloading…`。
- **中下**：交互提示 `[E] 拾取 AK-74`（仅 `hoveredLoot` 存在时）。
- **右上**：时钟 `HH:MM`（+ 可选阴天/晴天小字）。
- **背包面板**（Tab/I 开）：一个普通 ImGui 窗口，列出背包 `FItemStack`（名字 ×count），每行武器/服装带"装备"按钮；顶部显示当前主/副武器与已穿戴服装。开面板时释放鼠标捕获、暂停转向。
- 不做血条/状态条（无状态系统）。

## 6. 数据表：地图物品 → 游戏物品映射

`riverland_1km.scad` 实际摆放的可拾取节点（LootSystem 据此登记）：

| 地图节点名（scad 模块） | 游戏物品 id | 种类 | MVP 处理 |
| --- | --- | --- | --- |
| `cw_wpn_ak` | `ak`（AK-74） | Weapon | 可装备，Rifle545 |
| `cw_wpn_svd` | `svd`（SVD） | Weapon | 可装备，Rifle762，半自动 |
| `cw_wpn_mosin` | `mosin` | Weapon | 可装备，Rifle762，单发栓动 |
| `cw_wpn_shotgun` | `shotgun` | Weapon | 可装备，Shotgun12 |
| `cw_wpn_pistol` | `pistol` | Weapon | 可装备，Pistol9 |
| `cw_wpn_crate` | 掉 `ak` + Rifle545 弹 | Weapon/Ammo | 容器，单次给一份 |
| `cw_prop_crate_ammo` | Rifle545 ×30 | Ammo | 弹药箱 |
| `cw_item_ammobox` | Rifle762 ×20 | Ammo | 弹药盒 |
| `cw_item_helmet` | `helmet` | Clothing | 可穿戴（头附件显隐） |
| `cw_item_backpack` | `backpack` | Clothing | 可穿戴 |
| `cw_item_medkit` | `medkit` | Misc | 仅入包（无使用后果） |
| `cw_item_can` | `food_can` | Misc | 仅入包 |
| `cw_item_jerrycan` | `fuel` | Misc | 仅入包 |
| `cw_item_radio` | `radio` | Misc | 仅入包 |
| `cw_item_crate_supply` | 综合补给（食物+绷带若干） | Misc | 容器，单次给一份 |
| `cw_item_bedroll` / `cw_item_lantern` | 对应 Misc | Misc | 仅入包 |

> 弹药数量、武器参数都是可调初值，进 `WeaponDefs.hpp` / `NextDayzConfig.hpp`，后续平衡再改。POI 到物品的分布已由地图决定（碉堡/军事基地枪弹多、小镇有手枪/医疗、坠机点有 AK+背包+医疗），天然给出"往军事点搜刮更肥"的动线。

## 7. 开发里程碑与步骤（交付给执行 AGENT）

> 每个里程碑 = 独立可编译 + 可肉眼验证的一步。构建只针对本 target：`./gnb.sh build NextDayz`（引擎层没动就不用全量）。渲染验证用 `gnb shot --target NextDayz --scene assets/scad/proc/coldwar/riverland_1km.scad [--ui]`；交互验证用 `gnb validate --script ...`。完成一个里程碑写一条 journal。

### M0 — 目标骨架 + 加载地图（半天）

- 新建 `src/Application/Game/NextDayz/` 全套骨架 + `CMakeLists.txt`；父 CMake 加 `add_subdirectory(NextDayz)`。
- `NextDayzMain.cpp` 实现 `CreateGameInstance`；`NextDayzGameInstance` 实现空的生命周期。
- `OnInit`：`Modules::Scad::Register()` + `RequestLoadScene(riverland_1km.scad)`。
- `ConfigureCVars`：设个默认渲染器（软件光追/SoftwareModern 更省，利于 demo 帧率）。
- **验收**：`./gnb.sh build NextDayz` 通过；`gnb shot --target NextDayz --scene assets/scad/proc/coldwar/riverland_1km.scad` 能出地图俯瞰图；日志出现 `uploaded scene [...] to gpu`。

### M1 — 3C 核心：控制器 + FPS 相机 + 移动（1–2 天）

- `PlayerController`：`OnSceneLoaded` 里向下 `RayCast` 求出生地面 Y，`controller.Create(...)`；`OnTick` 合成 `inputDir`+速度调 `controller.Update`；`OverrideRenderCamera` 出 FPS 视角；`OnKey/OnCursorPosition/OnMouseButton` 接 WASD/Shift/Space/鼠标转向 + 鼠标捕获。
- 加 `V` 切 TPS、滚轮调距。
- **验收**：`gnb shot` 出第一人称地面视角；人能在地形上走/跑/跳、被墙挡住。用 `gnb validate` 脚本推 W 若干帧断言 `engine.totalFrames` 增长且位置变化（M4 起补 `game.*` 查询）。

### M2 — 玩家 ScadRig 视觉 + 动画（1–2 天）

- `PlayerRigVisual`：`OnInit` `LoadRig(next_ra_soldier)`；`BeforeSceneRebuild` 注入 part model/material；`OnSceneLoaded` 实例化 + `animator.Bind`；每帧跟随控制器位置/yaw 并按移动状态播 idle/walk（run=walk 提速）。
- FPS 隐身 rig、TPS 显示。
- ⚠️ 严守注入生命周期顺序（§5.2），别在 `OnSceneUnloaded` 清注入产物。
- **验收**：TPS 下 `gnb shot` 看到士兵站/走动画正确；FPS 下身体不糊镜头。`gkNextUnitTests "[Rig]"` 仍绿。

### M3 — 武器系统：装备/开火/换弹/ADS/切枪（2–3 天）

- `WeaponDefs.hpp` 填 5 把武器；`WeaponSystem` 实现装备/切枪/开火(raycast)/换弹/ADS；FPS 视图模型节点跟相机。
- 初始给玩家一把主武器 + 一匣弹（便于未做拾取前先测射击）。
- 命中反馈：曳光线 + impact marker（复用 debug draw；别造粒子系统）。
- **验收**：`gnb shot --ui` 见视图模型 + 准星 + 弹量 HUD；LMB 打墙有曳光/命中点、弹量递减；R 换弹、RMB ADS 收 FOV、1/2 切枪。`gnb validate` 断言开火后 `game.ammoInMag` 递减、reload 后回满。

### M4 — 背包 + 拾取 + 装备（2–3 天）

- `Inventory` + `LootSystem`：`OnSceneLoaded` 扫映射表登记 loot；每帧算 `hoveredLoot`；E 拾取入包 + 隐藏节点；Tab/I 背包面板可装备武器/穿戴服装。
- 服装附件节点显隐（helmet/backpack）。
- `RegisterAgentQueries` 暴露 `game.inventoryCount / game.equippedWeapon / game.ammoReserve` 等，供 validate 断言。
- **验收**：走到加油站/碉堡，准星对准物资出 `[E] 拾取`；拾取后道具消失、背包 +1；面板里装备 AK → 视图模型/弹药生效；穿戴钢盔 TPS 可见。`gnb validate` 脚本：走近 loot→E→断言 `game.inventoryCount` 增、装备后 `game.equippedWeapon=="ak"`。

### M5 — Time of Day + 天候（1 天）

- `World/TimeSystem` 接入 `OnTick`，起始 08:00、`timeScale` 默认 60；HUD 右上加时钟。
- 加"阴天"一档开关（偶发或按键手动切，便于验证）。
- **验收**：连续多帧 `gnb shot`（或调大 `timeScale`）能看到太阳方位/亮度随时间推移、昼夜切换；阴天时整体变暗。

### M6 — HUD 收尾 + 配置固化 + 验证脚本（1 天）

- 准星/弹量/交互提示/时钟/背包面板统一到 `UI/NextDayzHUD`，样式极简一致。
- 数值全部收进 `NextDayzConfig.hpp`。
- 写 `assets/agentscripts/nextdayz-smoke.agentscript.json`：出生→走到最近 loot→拾取→装备→开火→换弹→切 TPS→（拉时间看昼夜）逐步 `assert`，作为回归闭环。
- **验收**：`gnb validate --script assets/agentscripts/nextdayz-smoke.agentscript.json` 全绿、退出码 0；一次完整"走路→搜刮→装备→射击→昼夜"手感串起来。

## 8. 验证策略

- **渲染肉眼验证**：`gnb shot --target NextDayz --scene assets/scad/proc/coldwar/riverland_1km.scad --ui`（不弹窗、自动退出，读固定 `agent_validation.jpg`）。
- **交互 + 断言**：`gnb validate --script <script> [--visible]`。内建查询已够（`engine.totalFrames/frameRate`、`scene.nodeCount`、`cvar.*`），游戏侧再经 `RegisterAgentQueries` 暴露 `game.ammoInMag/ammoReserve/inventoryCount/equippedWeapon/hour/isAiming` 等。
- **单测**：动 `Gameplay/Rig` 相关跑 `gkNextUnitTests "[Rig]"`；纯游戏逻辑（背包堆叠、换弹补给、映射表）可加轻量 Catch2（可选）。
- **构建面**：只动 `src/Application/Game/NextDayz/**` → `./gnb.sh build NextDayz`；动到 `Gameplay` 共享层 → 额外 build `CharacterDemo AirportSim gkNextUnitTests` 验证未破坏其它 consumer。

## 9. 风险与开放问题

1. **ScadRig 注入生命周期**：单角色注入若顺序错会崩（AirportSim/CharacterDesigner 有前车之鉴）。M2 严格照 `FRigPreview` 样板；`OnSceneUnloaded` 只清指针。
2. **FPS 视图模型手感**：视图空间摆武器 + ADS lerp 的偏移需要试参，先粗后调；TPS 手骨骼挂载放到最后。
3. **出生点/地形高度**：务必用向下 raycast 求地面 Y（§4），别硬写；Y-up/Z-up 别搞混。
4. **拾取节点的粒度**：个别 loot 可能嵌在 POI 组节点下、或与相邻道具共节点——M4 先按名字前缀全表扫，若发现某类道具没被登记，核对该模块是否真生成独立节点（§2.2），必要时对特例补 `worldPos` 兜底。
5. **命中/曳光视觉**：明确**不新造粒子系统**，用现成 debug draw / 可选实体子弹；效果先"可见即可"，打磨留后。
6. **服装视觉**：MVP 只做 helmet/backpack 附件显隐；整体换装/重染色（改 `partMaterialIds`）是已知可行但更重，留 MVP 后。
7. **性能**：1km² 全 POI 场景较重，建议默认 `SoftwareModern` 渲染器 + 合理 FOV；`gnb shot` 首图确认帧率可接受再往下做。

## 10. 附录：关键 API 速查

| 需求 | 用法 |
| --- | --- |
| 运行时加载 .scad 地图 | `Modules::Scad::Register();` + `GetEngine().RequestLoadScene({.filename=...})` |
| 角色控制器 | `NextCharacterController::Create/Update/GetPosition/IsOnGround`（[NextCharacterController.h](src/Gameplay/Character/NextCharacterController.h)） |
| 相机接管 | `OverrideRenderCamera(Assets::Camera&)` → 设 `ModelView`(lookAt) + `FieldOfView` |
| hitscan 射击/拾取射线 | `GetEngine().RayCast(origin,dir,cb)` → `RayCastResult{HitPoint,Normal,T,InstanceId,Hit}` |
| 遍历/定位地图道具 | `scene.Nodes()` → `node->GetName()/WorldTranslation()/GetInstanceId()`；参考 `FAnchorMap` |
| 隐藏被拾取道具 | `node->GetComponent<RenderComponent>()->SetVisible(false)` + 物理体 `SetBodyActive(id,false)` |
| ScadRig 玩家视觉 | `FScadRigLoader::LoadRig` → `FRigInstance::Instantiate` → `FRigAnimator::Bind/Play/Update`（[ScadRig.md](AGENT_GUIDE/ScadRig.md)） |
| 昼夜/太阳 | `scene.GetEnvSettings()`：`SunRotation/SunIntensity/SkyIntensity/HasSun`；范式 [AirportSim/TimeSystem.cpp](src/Application/Game/AirportSim/TimeSystem.cpp) |
| HUD | `OnRenderUI()` 内 ImGui |
| 输入 | `OnKey/OnCursorPosition/OnMouseButton/OnScroll` |
| 给 validate 暴露查询 | `RegisterAgentQueries(FAgentQueryRegistry&)` → `game.<name>` |
| 生命周期钩子 | `OnInit/OnTick/OnDestroy` + `BeforeSceneRebuild/OnSceneLoaded/OnSceneUnloaded` |

---

**下一步**：从 M0 开始逐里程碑推进，每步 `./gnb.sh build NextDayz` + `gnb shot`/`gnb validate` 验证并写 journal。地图与 ScadRig 已就绪，MVP 主要是"把既有引擎能力用游戏规则串起来"，不涉及引擎改动。
