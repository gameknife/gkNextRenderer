---
title: "NextTotalwar 类全面战争 lowpoly RTS MVP 设计与开发计划"
category: project
status: 已实现
owner: NextTotalwar
created: 2026-07-30
last_updated: 2026-07-30
---

# NextTotalwar — 类全面战争 lowpoly 实时战略 MVP

> **实现状态（2026-07-30）**：MVP 已落地并通过定向构建、阵型/rig 单测、
> SCAD catalog、截图和选择/行军 AgentScript。现行代码导览见
> [AGENT_GUIDE/NextTotalwar.md](../../../AGENT_GUIDE/NextTotalwar.md)。

> 本文把 **kit_overhill**（低模山地零件库）、**gk_terrain**（可行走过程地形）、**ScadRig**（刚体骨骼角色 + 动作）、**FNavGrid**（A\* 寻路）当作既有基础设施，在其上设计一个 target 名为 `NextTotalwar` 的新子项目：俯视视角、lowpoly 画风、"全面战争"战场玩法的实时战略游戏。
>
> **MVP 只做到"能在 400×400 m 战场上框选部队、下达移动与阵面朝向命令、部队以阵型行军并贴合地形"，不做战斗。**
>
> 文档面向后续接手的 AGENT：所有引擎事实都标注了核实过的文件位置，所有里程碑都给出交付物、复用 API 和可执行的验收命令。

---

## 0. MVP 范围界定

**要做：**

- 一张 400×400 m 的程序地形战场：起伏平原、两侧山脊、一条河 + 桥、村庄 pad、林地；生成后**可用 ScadLibrary 过程页人工继续调整**。
- 3 个兵种（矛兵 / 剑兵 / 弓兵）的 SCAD 刚体骨骼角色 + idle / walk / march / run 动作。
- 双方各 6 支部队（regiment），每支 64 人，共 768 名士兵同屏。
- RTS 相机：平移、缩放、旋转，随地形抬升。
- 选择：左键点选 / 框选 / Shift 加选 / 双击同兵种全选；选中反馈（描边 + 地面阵型框）。
- 命令：右键点地移动；**右键按住拖拽 = 目标点 + 阵面朝向**（全战标志性操作）。
- 行军：部队级 A\* 路径 + 士兵按阵型槽位跟随 + 地形高度贴合 + 到位后重整队形。
- HUD：部队列表、选中信息、性能与**渲染预算守卫**面板。

**不做（明确排除，别顺手加）：**

- ❌ 战斗：命中、伤害、士气、溃逃、白刃缠斗、弓箭抛射。
- ❌ 敌方 AI（敌军部队只做静态摆放，可被选中/移动以便对照测试）。
- ❌ 战役地图、招募、经济、存档、联机。
- ❌ 攻城、载具、单位碰撞体与物理推挤（士兵之间不做刚体碰撞）。
- ❌ 音频、过场、结算界面。

---

## 1. 设计支柱

1. **部队是一等公民，士兵只是它的表现单元。**
   寻路、命令、选择、状态机的粒度全部是 regiment（约 12 支），不是士兵（约 768 个）。士兵没有个体寻路、没有个体避让、没有个体决策，只按阵型槽位做 seek/arrive。这一条同时决定了"看起来像全面战争"和"CPU 预算成立"。
2. **渲染实例预算是硬约束，不是优化项。**
   引擎 GPU-driven 提交路径用 15 bit 编码实例索引（§2.1），全场景可绘制 node-section 上限 **32767**。士兵数量、rig 部件数、植被密度全部要在这个预算里做加减法，且运行时有守卫面板。
3. **资产走既有 SCAD 管线，不引入新格式。**
   地图 = spec JSON → `gnb scad compose` → proc scad（之后人工维护）；兵种 = `kit_tw.scad` 组件库 + 薄 rig 文件，沿用 `kit_char` 的 pivot 标准以复用/派生动作 clip。
4. **只写游戏规则，不重造引擎能力。**
   相机走 `OverrideRenderCamera`，拾取走 `EngineHelper::GetScreenToWorldRay`，地形高度走 `TerrainComponent`，寻路走 `FNavGrid`，角色走 `FRigInstance`/`FRigAnimator`。新代码集中在阵型、命令、行军和视觉池。
5. **纯逻辑可单测。**
   阵型槽位解算、命令解析、路径推进这些不依赖渲染的部分做成纯函数/纯数据类，编进 `gkNextUnitTests`（沿用 NextRA / BrickPlayer 把 application 源文件加进测试 target 的既有做法）。

---

## 2. 已核实的引擎事实与硬预算

> 下列都是动手前已在代码中核实的事实，AGENT 可直接依赖；引用位置为当前分支实测。

### 2.1 渲染实例编码上限：32767 个 node-section（最重要）

GPU-driven 展开阶段把"可见项索引 + section 内三角形索引"压进一个 uint32：

```text
assets/shaders/Task.SoftMeshShaderExpand.comp.slang:17
    uint encBase = (item.instanceIdx & 0x7FFF) << 17;
assets/shaders/Rast.VisibilityPassSoftMeshShader.vert.slang:23
    uint instanceIdx = (prim >> 17) & 0x7FFF;   // proxy = scene.Nodes[instanceIdx - 1]
```

- `instanceIdx` = **nodeProxy 索引 + 1**（`Task.SoftMeshShaderGpuCullCompact.comp.slang:122`）。nodeProxy 是**每个可绘制节点的每个 section 一条**（`src/Engine/Assets/Core/Scene.Update.cpp:501-521`）。
- 因此**全场景可绘制 node-section 数必须 ≤ 32767**。超出后高位被 mask 截断，表现为几何错乱，**引擎当前不报警**。
- 相关次级上限：可见项缓冲每 slot 65535 条（`Scene::kMaxIndirectDrawCount`，`src/Engine/Assets/Core/Scene.hpp:36`）；单 section 三角形 ≤ 131072；单 model 最多 10 个 section（`kModelSectionStride`，`Scene.hpp:38`）。
- **本项目对策**：`Scene::GetIndirectDrawBatchCount()`（`Scene.hpp:146`）返回当前 node-section 数，HUD 常驻显示；超过 **28000** 变红并打日志。预算分配见 §5。

### 2.2 每帧 nodeProxy 全量重建

`Scene::UpdateNodesGpuDriven()`（`src/Engine/Assets/Core/Scene.Update.cpp:447-533`）在场景 dirty 时**遍历全部节点**重建 nodeProxy 数组并整体上传。RTS 每帧都有节点在动，等于每帧都走这条路径。

结论：**节点总数直接决定 CPU 常驻成本**，士兵部件数要克制（目标单兵 ≤ 6 个可绘制节点），并且不要为"每个士兵加一个地面标记节点"这类想法开口子（标记用 ImGui 投影画）。

### 2.3 地形（gk_terrain）

- 语言面与 TERR 编码见 [AGENT_GUIDE/ScadTerrain.md](../../../AGENT_GUIDE/ScadTerrain.md)；`ter_place / ter_along / ter_scatter` 贴地组合子在 `assets/scad/lib/kit_terrain.scad`。
- **cells 取 176×176**：400 m / 176 ≈ 2.27 m 一格；三角形 176×176×2 = 61,952，低于单 model 索引 65535×3 的门槛，因此地形仍是**单个 MeshShape**（可行走、可放物理道具），也低于 131072 的 section 上限。>180² 会让引擎跳过 MeshShape（[ScadAssetPlaybook](../../../AGENT_GUIDE/ScadAssetPlaybook.md) §4）。
- `TerrainComponent`（`src/Engine/Runtime/Components/TerrainComponent.hpp:58-64`）提供 `SampleHeight / SampleNormal / SampleSlopeDegrees / IsWater / WaterSurface / IsWalkable / BiomeId`，输入是**引擎世界 XZ**，与渲染网格逐三角形一致。士兵的 Y 全部由 `SampleHeight` 给，不走物理。
- 取用方式：加载完场景遍历 `scene.Nodes()` 找挂了 `Runtime::TerrainComponent` 的节点（`src/Tests/Test_TerrainWalkable.cpp:25-33` 即此写法）。
- 坐标：engine (x, z) = scad (x, −y)，world Y = scad z。

### 2.4 ScadRig 运行时链路，以及为什么不用 `FCharacterPool`

链路（[AGENT_GUIDE/ScadRig.md](../../../AGENT_GUIDE/ScadRig.md)）：

```text
FScadRigLoader::LoadRig(.scad) → Assets::FRigAsset（bones / partModels / parts / clips）
FRigInstance::Instantiate(scene, asset, desc) → 每骨骼一个 Node + 每 part 一个 RenderNode
FRigAnimator::Update(dt) → 采样 clip → 写骨骼 TRS → root 一次 RecalcTransform
```

`NextGameplay::Sim::FCharacterPool` 是 AirportSim/StudioSim/CitySolSim 的共享池，但**本项目不使用它**：它给**每个池位**复制一份 part model（`src/Gameplay/Sim/CharacterPool.cpp:89-104`）。42 个池位没问题，768 个池位会产生数千份 model 拷贝，`models_`/`offsets_` 规模和顶点内存都不可接受。

替代方案：本项目自建 `FRegimentVisualPool`（§4.6），**每兵种只注入一份 part model**，所有士兵节点共享同一 `modelId`，靠 `RenderComponent` 的**每节点材质数组**做阵营/部队换色。理论依据：容量计算是按 node-section 累加三角形的（`Scene.Update.cpp:511`），材质是逐节点的（`RenderComponent::SetMaterials`，`src/Engine/Runtime/Components/RenderComponent.hpp:38-39`），不要求 model 独占。
**但 CharacterPool 的历史注释声称"定容语义需要每池位独立拷贝"**，两者矛盾，因此 **M0 必须先用尖刀实验实测确认**，确认不了就退到"每兵种 × 每阵营一份拷贝"（拷贝数 = 兵种数 × 2，仍然可控）。

其他既有约定：clip 跨角色复用要求骨架 pivot 一致（`ch_pivot_*`，`assets/scad/lib/kit_char.scad:40-45`）；多实例去同步用 `SetPhaseOffset`，速度匹配用 `SetPlaySpeed`；面朝 +Z = SCAD 的 −Y。

### 2.5 寻路（FNavGrid）

`src/Gameplay/AI/NavGrid.h`：`Build(CPU BVH, settings)` / `FindPath` / `MaskUnwalkable` / `RebuildDirtyRegion`。

- 需要 `GOption->KeepCPUMeshData = true`（AirportSim 在 `OnInit` 里就这么做，`src/Application/Game/AirportSim/AirportSimGameInstance.cpp:86`）。
- **水域必须补语义否决**：地形河岸缓坡在几何 raycast 下是可走的，要用 `MaskUnwalkable` + `TerrainComponent::IsWater` 挡住（写法见 ScadTerrain.md）。`RebuildDirtyRegion` 后要重新应用。
- 本项目 `cellSize` 取 **2.0 m**（400 m → 200×200 = 40,000 格），`agentRadius` 取 0.4 m。部队级寻路每条命令只跑 1 次 A\*，12 支部队完全无压力。
- **桥的连通性**是既有血泪契约：桥长 ≥ 2.5×河宽，锚点取下切带外的路面高度，否则 NavGrid 在桥两端断连（ScadTerrain.md "桥的布置契约"）。地图设计必须照抄。

### 2.6 场景、输入、相机

- `.scad` 可运行时直接加载：`Modules::Scad::Register();` + `GetEngine().RequestLoadScene({.filename = "..."})`（`AirportSimGameInstance.cpp:72,94`）。
- 屏幕→世界射线：`Runtime::EngineHelper::GetScreenToWorldRay(mousePos, org, dir)`（`src/Engine/Runtime/Utilities/NextEngineHelper.cpp:92`），BrickPlayer / KongLie3D / NextRA 都是消费者。
- 相机：`NextGameInstanceBase::OverrideRenderCamera`（`src/Engine/Runtime/GameInstance.hpp:70`）。可直接参考 `NextRA::FRtsCamera`（`src/Application/Game/NextRA/Render/RtsCamera.h`）的形态：focus + distance + WASD + 滚轮。
- 框选叠加层：NextRA 用 `ImGui::GetForegroundDrawList()` 画（`NextRAGameInstance.cpp:1108`），本项目照抄这套。
- 选中描边：`Runtime::RenderOutlineFlags::selected / hovered`（`RenderComponent.hpp:19-26`），置位后由 `Scene::UpdateNodesGpuDriven` 打进 proxy（`Scene.Update.cpp:484-494`）。
- 渲染器：`r.rendererType`（0=PathTracing, 1=SoftwareTracing, 2=SoftwareModern, 3=VoxelTracing, 4=SoftwareModernNoAmbient，`src/Engine/Runtime/Config/EngineCVars.cpp:54`）。**上千个每帧移动的实例更适合光栅路径**，MVP 默认 `4`，但要在 M0 用同一场景实测 0/2/4 再定案。
- Agent 查询：`RegisterAgentQueries` 可暴露 `game.<name>`（`GameInstance.hpp:65`），供 `gnb validate` 断言。

---

## 3. 资产设计

### 3.1 组件库 `kit_tw.scad`（在 kit_overhill 之上扩展）

新建 `assets/scad/lib/kit_tw.scad`，前缀 `tw_`，遵循 [ScadAssetPlaybook](../../../AGENT_GUIDE/ScadAssetPlaybook.md) 的 kit 规范（纯零件库、零参 function 常量、含平方项的 PRNG、`boxc`/`slab` 工具、落地件底面 z=0、带朝向件 front = −y）。

**分工**：地形、植被、岩石、河桥、木屋一类**继续直接用 `kit_overhill.scad`**（`oh_nature_pine/autumn/bush/grass`、`oh_rock_boulder/cluster`、`oh_prop_bridge/fence_log/tent/campfire`、`oh_bldg_cabin/tower`），不重复造。`kit_tw` 只补战场题材缺的两类：

| 类别 | 模块 | 用途 |
|---|---|---|
| `tw_bldg_*` | `tw_bldg_watchtower`、`tw_bldg_palisade`（木栅段）、`tw_bldg_gatehouse`、`tw_bldg_hut` | 村庄 pad 与据点，中世纪化 |
| `tw_prop_*` | `tw_prop_banner`（军旗）、`tw_prop_stakes`（拒马）、`tw_prop_cart`、`tw_prop_haystack`、`tw_prop_marker`（阵地标记） | 战场叙事 + 部队旗帜 |
| `tw_char_*`（角色部件，见 §3.2） | 头/盔/躯干/臂/腿/武器/盾 | 兵种组装 |

配色沿用 kit_overhill 的深饱和策略（PT 强日光下 0.5 已接近白），阵营色**不写死在 kit 里**：所有需要换色的面用 `ch_TINT()` 的纯品红占位，由运行时按部队替换（ScadRig 的 tint 语义）。

三角预算：建筑 ≤ 2500，道具 ≤ 300，**角色部件合计 ≤ 200/兵**（数量级是 768 个实例，这条最重要）。

### 3.2 兵种 rig 与动作

角色部件放进 `kit_tw.scad` 的 `tw_head_* / tw_helm_* / tw_torso_* / tw_arm_* / tw_leg_* / tw_wpn_* / tw_shield_*` 分段，**pivot 直接复用 `kit_char` 的 `ch_pivot_torso/head/arm_l/arm_r/leg_l/leg_r()`**，这样 `ch_clip_idle/walk` 可以作为动作基线直接用或改写。

MVP 三个兵种（薄文件，照 `assets/scad/characters/worker.scad` 的写法）：

| 文件 | 组成 | 动作 |
|---|---|---|
| `assets/scad/characters/tw_spearman.scad` | 锅盔 + 布面甲(tint) + 圆盾(左臂) + 长矛(右臂) | idle / march（持矛竖直）/ walk / run |
| `assets/scad/characters/tw_swordsman.scad` | 链甲兜帽 + 罩袍(tint) + 鸢盾 + 剑 | idle / walk / run |
| `assets/scad/characters/tw_archer.scad` | 皮帽 + 皮甲(tint) + 弓 + 箭袋 | idle / walk / run |

约定复述（违反只会 warning 不会报错，容易埋雷）：骨骼 = `bone_` 前缀 module，每骨骼只调用一次；bone 调用外层只允许 `translate`/`rotate`；顶层恰好一个 `bone_*` 调用；1 unit = 1 m、Z-up、根骨骼原点落地、**鼻子朝 −Y**；clip 是顶层 `anim_<name>` 纯数据 list。

**部件数纪律**：一个兵 = 6 个 part 节点（torso / head / arm_l / arm_r / leg_l / leg_r），武器和盾**合并进对应手臂骨骼**（不新开骨骼），目标单兵 node-section ≤ 10。M2 必须实测记录实际 section 数。

动作产出路径：先用 `ch_clip_*()` 跑通 → 用 ScadLibrary **角色工作室**（Ctrl+3）的贴底时间轴调关键帧 → 保存会安全回写到文件末尾 `SCADLIBRARY_RIG_EDITOR_BEGIN/END` 区间，不动骨架与几何。

### 3.3 战场地图（400×400 m，proc + 可人工调整）

**生成路径**（一次性）：写 `assets/scad/specs/tw_greenfield.json` → `gnb scad compose --spec ...` → 产物在 `assets/scad/proc/generated/tw_greenfield.scad`。因为 `generated/` 属于**可被重新生成覆盖**的目录（`assets/scad/README.md`），首版验收后**另存为 `assets/scad/proc/nexttotalwar/greenfield_400.scad` 作为手工维护主文件**，此后用 ScadLibrary 过程页（拖 feature 手柄）继续调整，spec 只作为出生记录。

TERR 参数：

```scad
TERR = ["gkterr1", [400, 400], [176, 176], <seed>, [0, 2.2, 0.45], undef, "temperate",
    [ ["ridge",    [[-190, 120], [-90, 150], [-20, 130]], 60, 18],   // 北侧山脊：限制侧翼
      ["ridge",    [[40, -150], [140, -120], [195, -80]], 55, 15],   // 南侧山脊
      ["plateau",  [-60, -40], 45, 5],                               // 缓坡高地：抢占价值
      ["river",    [[10, 200], [-20, 60], [-10, -60], [20, -200]], 9, 1.6],
      ["road",     [[-200, -20], [-60, 0], [60, 10], [200, 30]], 6], // 东西主路，穿河出桥沟
      ["pad",      [-150, 60], [40, 30], 0],                         // 西村
      ["pad",      [150, -50], [36, 26], 0] ]];                      // 东村
```

设计意图（地形要服务玩法，不是好看）：

- **中央 200×160 m 的缓坡开阔地**是主战场，能容纳两军 12 支部队展开。
- 河把战场分成东西，两座桥是**咽喉**，制造"抢桥/绕行"的路线选择。桥严格按 §2.5 的契约摆（桥长 ≥ 2.5×河宽 = ≥ 23 m，用 `oh_prop_bridge(L = 24)`，锚点取离河心线 ≥ 1.3×河宽的路面）。
- 两条山脊是**天然侧翼屏障**，坡度 > NavGrid 的 maxSlopeAngle 时自然不可走，形成走廊。
- 两个 pad 放村庄（`oh_bldg_cabin` + `tw_bldg_palisade` + `tw_bldg_watchtower`），是方位地标兼出生区。
- 林地用 `ter_scatter` 按生物群系过滤撒（`[hMin, hMax, slopeMax, avoidWater, biomes]`），**只在山脊侧和地图边缘**，中央战场保持空旷（既是玩法需要，也是节点预算需要）。

**植被密度按节点预算倒推**，不照搬 1km 图的经验值：目标全图 ≤ 600 棵树/岩石/草簇（0.16 km²，视觉上比 1km 图的 700-900 棵/km² 略密，符合"战场边缘有林"的观感），详见 §5。

---

## 4. 运行时架构

### 4.1 目录与文件地图

```text
src/Application/Game/NextTotalwar/
├── CMakeLists.txt                       # 照抄 AirportSim：GLOB + gk_configure_application + NextGameplay
├── NextTotalwarGameInstance.{hpp,cpp}   # 入口、OnTick 编排、场景生命周期、Agent 查询
├── NextTotalwarConfig.hpp               # 兵种表/阵型表/预算常量（C++ struct，非 JSON）
├── NextTotalwarTypes.h                  # FRegiment / FSoldier / FOrder / ESelectionState
├── Battle/
│   ├── FormationLayout.{h,cpp}          # 阵型槽位解算（纯函数，进单测）
│   ├── RegimentSystem.{h,cpp}           # 部队生成、状态机、命令下达
│   ├── MarchSystem.{h,cpp}              # 路径推进、士兵 seek、地形贴合、朝向
│   └── ArmyDeployment.{h,cpp}           # 从 JSON 部署表建两军
├── Render/
│   ├── BattleCamera.{h,cpp}             # RTS 相机
│   ├── RegimentVisualPool.{h,cpp}       # rig 注入、士兵节点、animator、LOD、换色
│   └── SelectionOverlay.{h,cpp}         # 框选矩形、阵型框、世界→屏幕投影
└── NextTotalwarUI.{h,cpp}               # HUD、部队面板、预算/性能守卫面板

assets/configs/nexttotalwar/
├── units.json                           # 兵种定义（rig 路径、速度、阵型默认值、颜色）
└── battles/greenfield.json              # 部署表（每方部队列表、位置、朝向）
```

注册两处（漏了就构建不到）：`src/Application/Game/CMakeLists.txt` 加 `add_subdirectory(NextTotalwar)`；`gnb.toml` 的 `[targets].all` 加 `"NextTotalwar"`。

### 4.2 数据模型：Def（原型） vs Runtime（实例）

沿用 Brotato3D 的分离约定（[AGENT_GUIDE/Brotato3D.md](../../../AGENT_GUIDE/Brotato3D.md) §3）：

```cpp
struct FUnitDef            // 只读原型，来自 units.json
{
    std::string id;             // "spearman"
    std::string rigPath;        // "assets/scad/characters/tw_spearman.scad"
    float marchSpeed = 2.2f;    // m/s，阵型行军
    float runSpeed   = 4.4f;
    float catchUpFactor = 1.35f;// 士兵追赶槽位的速度上限倍率
    int   defaultRanks = 4;     // 默认横列数
    float fileSpacing = 1.15f;  // 同排间距 m
    float rankSpacing = 1.35f;  // 排间距 m
    float baseWalkClipSpeed = 1.8f;  // clip 的原生位移速度，用于 SetPlaySpeed 匹配
};

struct FSoldier            // 每兵一份
{
    glm::vec3 position{};       // 世界坐标，y 来自 TerrainComponent
    float yaw = 0.0f;
    int slotIndex = -1;
    float phaseOffset = 0.0f;   // 动画去同步
    Assets::Node* worldNode = nullptr;      // rig 根的父节点
    NextGameplay::FRigAnimator animator;
};

struct FRegiment           // 每部队一份（命令/寻路/选择的粒度）
{
    int id = -1;
    int faction = 0;                 // 0 = 玩家, 1 = 敌方
    const FUnitDef* def = nullptr;
    glm::vec3 anchor{};              // 阵型中心（地面点）
    float facing = 0.0f;             // 阵面朝向（弧度）
    int ranks = 4;
    std::vector<FSoldier> soldiers;
    // 命令与行军
    ERegimentState state = ERegimentState::Idle;   // Idle / Marching / Reforming
    glm::vec3 orderTarget{};
    float orderFacing = 0.0f;
    std::vector<glm::vec3> path;     // NavGrid A* 折线
    size_t pathCursor = 0;
};
```

### 4.3 一帧的数据流（`OnTick`）

```text
1. camera_.Tick(dt)                                  // 输入已在 OnKey/OnScroll 里记状态
2. selection_.Tick(dt)                               // 框选拖拽状态机（只算，不改场景）
3. RegimentSystem::Tick(dt)
      每支部队：状态机推进（Idle / Marching / Reforming）
                Marching → 沿 path 推进 anchor，facing 按路径切线插值
                到达 → Reforming，facing 插值到 orderFacing，重算槽位
4. MarchSystem::Tick(dt)
      每名士兵：目标 = FormationLayout::SlotWorld(anchor, facing, slot) → NavGrid 无关
                seek/arrive（速度上限 = def->marchSpeed * catchUpFactor）
                y = terrain->SampleHeight(x, z)
                yaw → 移动方向（速度 > 阈值）否则 → 部队 facing
5. RegimentVisualPool::Tick(dt)
      LOD 分档 + 分帧更新 animator（§4.6）
      写 worldNode 的 translation/rotation
6. MarkTransformDirty()
```

**不做**：士兵之间的碰撞/推挤（阵型槽位天然不重叠，行军中的短暂穿插在 lowpoly 俯视下不可见）；士兵级 NavGrid 查询（每帧 768 次 raycast/A\* 是纯浪费）。

### 4.4 阵型解算（纯函数，可单测）

```cpp
// FormationLayout.h —— 无任何引擎依赖，进 gkNextUnitTests
glm::vec2 SlotLocalOffset(int slotIndex, int soldierCount, int ranks,
                          float fileSpacing, float rankSpacing);
glm::vec3 SlotWorld(const glm::vec3& anchor, float facing, const glm::vec2& localOffset);
glm::vec2 FormationHalfExtent(int soldierCount, int ranks, float fileSpacing, float rankSpacing);
```

规则：`files = ceil(count / ranks)`；`row = slot / files`，`col = slot % files`；`x = (col − (files−1)/2) · fileSpacing`（左右居中），`z = −row · rankSpacing`（**前排在阵面朝向侧**，后排往后排）；最后一排不足时居中补齐。

单测断言（`src/Tests/Test_NextTotalwarFormation.cpp`）：
槽位互不重合；整体包围盒与 `FormationHalfExtent` 一致；`facing` 旋转 90° 后所有槽位等于原槽位绕 anchor 旋转 90°；同参数两次调用逐位相同（确定性）。

### 4.5 行军与地形

- 命令下达时对 **anchor** 跑一次 `navGrid_.FindPath(anchor, target, referenceHeight)`；失败则直线兜底并在 HUD 提示。
- anchor 沿折线以 `marchSpeed` 推进；facing 用当前段方向做角度插值（转向角速度上限，避免整队瞬间甩头）。
- 士兵位置每帧重算槽位世界坐标 → `seek`；距离 < 0.05 m 时吸附，避免抖动。
- Y 一律 `terrain->SampleHeight(x, z)`；坡度大的地方阵型会自然起伏，这正是想要的观感。
- 水：NavGrid 建好后必须 `MaskUnwalkable` 挡掉水面（§2.5），否则部队会趟河。

### 4.6 视觉池与 LOD（`FRegimentVisualPool`）

**注入（`BeforeSceneRebuild`）**：

1. 每个用到的兵种 `FScadRigLoader::LoadRig` 一次 → `FRigAsset`。
2. 每兵种把 `asset.partModels` **注入一份**到 `models`，记下 `partModelIds`（共享给该兵种全部士兵）。
3. 非 tint section 材质每兵种一份；tint section 材质**每部队一份**（12 份），实现"同阵营不同部队可区分"。

**实例化（`OnSceneLoaded`）**：每名士兵一个世界 Node（位置 + yaw）+ `FRigInstance::Instantiate` 的骨骼子树；`FRigAnimator::Bind` 后 `SetPhaseOffset(hash(soldierId))` 去同步，`SetPlaySpeed(speed / baseWalkClipSpeed)` 匹配步频。

**LOD（按到相机距离，MVP 只做前两档）**：

| 档 | 条件 | 行为 | node-section |
|---|---|---|---|
| L0 | < 120 m | animator 每帧更新 | 满额（≤10/兵） |
| L1 | 120–260 m | animator 每 3 帧更新一次（按士兵 id 取模错峰） | 满额 |
| L2 | 扩军后启用（非 MVP） | 合并成单 part 静态姿势模型，只留 1 个渲染节点 | 1/兵 |

L2 需要一个"把 rig 各 part 按某一姿势烘成单个 Model"的工具函数，属于 M5 之后的扩军工作，MVP 768 兵在预算内不需要。

### 4.7 选择与命令

**选择**（左键）：

- 按下记 `dragStart_`，移动超过 6 px 进入框选，`ImGui::GetForegroundDrawList()` 画矩形（照抄 `NextRAGameInstance::DrawSelectionOverlay`）。
- 命中判定用**部队**粒度：把 anchor 和阵型四角投影到屏幕，与矩形求交；点选时用射线与地面交点到 anchor 的距离 + 阵型半径。
- Shift 加选；双击选中同兵种全部；空点取消。
- 反馈：选中部队的士兵节点置 `RenderOutlineFlags::selected`（只在选择变化时改，不每帧刷），并用 ImGui 画贴地阵型框。**若描边在光栅渲染器下效果不佳，降级为只画阵型框**（M4 决策点）。

**命令**（右键）：

- 按下：`GetScreenToWorldRay` → 与地形求交（沿射线步进 + `SampleHeight` 二分，或先与 y=anchorY 平面求交再迭代修正）→ 得到目标点，画预览阵型框。
- 拖拽：拖出的方向 = 阵面朝向，预览框实时旋转；拖动距离 < 阈值则沿用部队当前朝向。
- 松开：对每支选中部队下达命令。多支部队时，把它们按当前 anchor 的横向次序排布到目标点两侧（保持相对位置），不要全部叠在同一点。

### 4.8 相机

`FBattleCamera`：focus(XZ) + distance + yaw + pitch(固定 ~55°)。WASD/方向键或屏幕边缘平移（速度随 distance 缩放），滚轮缩放（20–220 m），Q/E 绕 focus 旋转 yaw；focus 的 Y 取 `SampleHeight` 平滑跟随，避免过山时穿地。focus 被 clamp 在地图边界内。

### 4.9 UI/HUD

- 底部部队条：每支部队一格（兵种图标色块 + 人数 + 状态），点击选中。
- 左上：选中部队信息（兵种、人数、状态、阵型排数，`[`/`]` 调排数）。
- 调试面板（F1 开关）：`GetIndirectDrawBatchCount()` / 32767 进度条（>28000 变红）、士兵数、animator 更新数、帧率、NavGrid 就绪状态、当前渲染器。

---

## 5. 预算表（MVP 目标值）

| 项 | 数量 | node-section | 三角形 |
|---|---:|---:|---:|
| 地形（单 model，最多 10 biome section） | 1 | ~10 | 61,952 |
| 水面 | 1 | 1 | ~2 |
| 植被/岩石/草簇（`ter_scatter`） | ≤ 600 | ≤ 900 | ~55,000 |
| 建筑/道具（两村 + 桥 + 旗帜） | ≤ 120 | ≤ 400 | ~40,000 |
| 士兵（768 = 2 方 × 6 队 × 64） | 768 | ≤ 7,680（≤10/兵） | ~140,000 |
| **合计** | | **≈ 9,000 / 32,767（27%）** | **≈ 300,000** |

余量说明：预算允许后续扩到约 2,400 兵仍不触顶（届时再加 L2 合并模型可到万级）。**任何让单兵 node-section 超过 10 的资产改动都要重算这张表。**

CPU 侧量级：768 个 animator（L1 分帧后每帧约 300 次 `Update`，各采样 7 骨骼）+ 768 次向量 seek + 每帧 ~9,000 条 nodeProxy 重建上传。这些都要在 M0/M5 用真实 profiler 数据校准，**不要沿用本表当实测结论**。

---

## 6. 开发计划

每个里程碑都以"可截图/可断言"的状态收尾，不允许攒到最后一次性验证。

### M0 — 目标骨架 + 技术尖刀（先做，决定后面所有方案）

**交付**

- 新 target `NextTotalwar`（CMakeLists + `Game/CMakeLists.txt` + `gnb.toml`），最小 GameInstance：注册 scad loader、加载 `assets/scad/proc/terrain_layout_demo.scad`、`FBattleCamera` 雏形、HUD 显示 node-section 计数。
- **尖刀 A（共享 model 多实例）**：注入 1 个 box model，创建 5,000 个共享同一 `modelId` 的 render node 铺成网格。判定：画面正确、`GetIndirectDrawBatchCount()` 线性增长、无几何错乱。**这一条决定 §2.4 的视觉池方案能否成立。**
- **尖刀 B（rig 多实例）**：加载 `assets/scad/characters/agent_basic.scad`，不经 `FCharacterPool`，用共享 part model 实例化 300 个 rig 并播 walk clip。记录：node-section 数、CPU 帧时间、animator 耗时。
- **尖刀 C（渲染器选型）**：同一场景切 `r.rendererType` 0 / 2 / 4，各记 fps 与画面观感，定 MVP 默认值写进 `ConfigureCVars`。

**验收**

```bash
gnb.bat build NextTotalwar
gnb.bat shot --target NextTotalwar --ui
```

三组尖刀数据写进 `.spec/journal/`（或本文件的"实测记录"追加段）。若尖刀 A 失败，改为"每兵种 × 每阵营一份 model 拷贝"并更新 §2.4、§5。

### M1 — 战场地图

**交付**：`assets/scad/specs/tw_greenfield.json` → compose → 另存 `assets/scad/proc/nexttotalwar/greenfield_400.scad`；桥按契约摆放；植被按生物群系过滤散布；节点数落在 §5 预算内。

**验收**

```bash
gnb.bat scad catalog                                     # 0 bad / 0 warning
gnb.bat shot --scene assets/scad/proc/nexttotalwar/greenfield_400.scad
```

肉眼过：比例、桥两端衔接、村庄共面、中央开阔、无悬空/穿地/发白。用 PIL 裁剪放大复核桥与村庄。**记得把 kit 与场景 cp 到 `out/build/<preset>/assets/scad/`**（两处不同步 = 白改）。

### M2 — 兵种资产

**交付**：`assets/scad/lib/kit_tw.scad`（角色部件 + 中世纪建筑/道具）；`tw_spearman/tw_swordsman/tw_archer.scad` 三个 rig + idle/walk/march/run clip；`assets/scad/source/tw_showcase.scad`（部件陈列 + 三兵种并排特写）。

**验收**

```bash
gnb.bat scad catalog
gnb.bat shot --scene assets/scad/source/tw_showcase.scad
./out/build/windows/bin/gkNextUnitTests "[ScadRig]"
```

必须记录每个 rig 的 part 数与 section 数，确认 ≤ 10/兵；rig 加载日志无 `SCADRIG:` warning。动作用 ScadLibrary 角色工作室（Ctrl+3）微调。

### M3 — 部队、阵型与视觉池（静态）

**交付**：`FormationLayout`（+ 单测）、`FRegiment`/`FSoldier` 数据模型、`units.json` / `battles/greenfield.json`、`ArmyDeployment`、`RegimentVisualPool`（注入 + 实例化 + 每部队换色）。此时部队站在部署位置播 idle，不能移动。

**验收**

```bash
gnb.bat build NextTotalwar gkNextUnitTests
./out/build/windows/bin/gkNextUnitTests "[NextTotalwar]"
gnb.bat shot --target NextTotalwar --ui
```

截图必须能看清：12 个方阵、阵型整齐、贴合起伏地形、双方颜色可区分、HUD 的 node-section 计数在预算内。

### M4 — 选择与命令

**交付**：`FBattleCamera` 完整版、`SelectionOverlay`（框选矩形 + 阵型框 + 世界→屏幕投影）、点选/框选/Shift 加选/双击全选、右键点地与拖拽定向、选中反馈、`RegisterAgentQueries` 暴露 `game.selectedRegiments` / `game.regimentCount` / `game.marchingRegiments`。

**验收**：新增 `assets/agentscripts/nexttotalwar-select.agentscript.json`

```bash
gnb.bat validate --script assets/agentscripts/nexttotalwar-select.agentscript.json
```

脚本流程：等 Running → 拖拽框选左半屏 → `assert game.selectedRegiments ge 1` → 右键拖拽下令 → 截图 → 退出。

### M5 — 行军表现

**交付**：NavGrid 构建 + 水域 mask、部队级 A\*、anchor 推进与转向插值、士兵 seek/追赶、地形贴合、动画状态切换（idle ↔ walk/march ↔ run）、去同步与步频匹配、LOD 分帧。

**验收**

```bash
gnb.bat validate --script assets/agentscripts/nexttotalwar-march.agentscript.json
```

脚本：选中 → 下达跨河命令 → `wait-until game.marchingRegiments eq 0`（带超时）→ `assert` 部队 anchor 到达目标附近 → 截图。人工再看一遍：过桥不断连、上坡不穿地、后排能追上、脚步不整齐划一（相位错开）。

### M6 — 打磨与收口

**交付**：HUD 完整（部队条、排数调整、预算守卫）、性能 profiler 采样记录、`AGENT_GUIDE/NextTotalwar.md`（代码结构梳理，照 Brotato3D 那篇的体例）、把本文件状态从"计划中"改为"已实现"并更新实测数据、`docs/README.md` 索引项。

**验收**：M1–M5 全部脚本重跑一遍绿；`gnb.bat build NextTotalwar gkNextUnitTests` 干净。

---

## 7. 验证矩阵

| 改动类型 | 必跑 |
|---|---|
| 仅 C++ 玩法 | `gnb.bat build NextTotalwar`；`gkNextUnitTests "[NextTotalwar]"`；`gnb.bat shot --target NextTotalwar --ui` |
| 阵型/命令逻辑 | 上面 + 对应 agentscript |
| SCAD kit / 角色 | `gnb.bat scad catalog`；showcase `gnb shot`；同步 cp 到 build assets |
| 地图 | 场景 `gnb shot` + 裁剪放大复核桥/村；跑一次 march 脚本确认连通 |
| 触碰 Gameplay/Sim/Rig 共享层 | 额外 `gnb.bat build AirportSim StudioSim CitySolSim CharacterDemo` |

---

## 8. 风险与降级方案

| 风险 | 触发信号 | 降级 |
|---|---|---|
| 共享 model 多实例不成立（§2.4 矛盾未证伪） | M0 尖刀 A 画面错乱 | 每兵种 × 每阵营一份 model 拷贝（6 份），tint 仍走每部队材质 |
| node-section 触顶 | HUD 计数 > 28000 | 先砍植被 → 再降每兵 section（合并颜色）→ 最后上 L2 合并模型 |
| CPU 被 animator/nodeProxy 吃满 | 帧时间 CPU 侧 > 8 ms | 扩大 L1 分帧步长、远处直接停动画、减少士兵数（每队 64 → 48） |
| PathTracing 下上千动态实例掉帧 | M0 尖刀 C | 默认光栅渲染器，PT 仅作"截图模式" |
| 过河/上桥断连 | 部队卡在岸边 | 按 ScadTerrain 桥契约重摆桥；必要时在桥面加 pad 或加宽路 |
| 阵型在陡坡上拉扯变形 | 观感崩坏 | 限制部队可进入坡度（NavGrid maxSlopeAngle 收紧），并在山脊侧用地形挡住 |

---

## 9. 非 MVP 的后续方向（记录，不构成授权）

战斗结算（近战贴脸 + 弓箭抛射）、士气与溃逃、单位卡/兵牌 UI、敌方 AI 指挥官、冲锋与冲击力、骑兵兵种（马 + 骑手同一 rig，`anim_gallop`）、攻城器械与城墙、战役地图层、多人（可参考 NextRA 的 lockstep 与 order 协议）。

这些都要在 MVP 验收后各自立项、各自定验收标准，不要在 MVP 里"顺手做一半"。

---

## 10. MVP 实测记录（2026-07-30）

- 尖刀 A：运行时采用“每兵种一份共享 mesh + 每士兵实例节点”，3 个 model
  分别供 256 名士兵复用；阵营/部队色由逐节点 material 选择。
- 尖刀 B：三份 SCAD rig 均为 7 bone / 6 part，包含 idle / walk / march / run；
  单测约束每 rig 总三角形不超过 300。MVP 大军运行时选择一 part 共享 mesh，
  以节点位移与相位 bob 表现行军，避免 768 个多 part animator 的 CPU/节点成本。
- 尖刀 C：`SoftwareModernNoAmbient` 在当前 Windows/NVIDIA 环境首次管线启动超过
  Agent Control 的 30 秒连接窗口；`SoftwareModern` 约 1–2 秒启动，故默认
  `r.rendererType=2`。
- 400×400 m Greenfield：176×176 cells、两座桥、双村庄、边缘林地；
  SCAD loader 实测 442 个地图节点、95,418 triangles、0 warnings。
- 全场：12 regiment / 768 soldiers，实测 1,072 node-sections（上限的 3%）；
  `gnb shot --target NextTotalwar --ui` 截图样本约 92 FPS。
- NavGrid：200×200（40,000 cells），38,395 geometry-walkable，水域语义屏蔽
  940 cells。低模桥端若因 2m 网格离散化断连，游戏层按最近桥生成语义桥路线，
  且桥廊高度采样使用桥面而不是 TerrainComponent 河床。
- 自动验收：
  `nexttotalwar-select.agentscript.json` 框选 6 队并下达定向命令；
  `nexttotalwar-march.agentscript.json` 下达约 101.7 m 跨河命令并等待全部重整完成。
