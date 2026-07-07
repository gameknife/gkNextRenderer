---
title: "NextRA —— 战斗深度扩展（朝向/炮塔/克制/碰撞/防御）架构设计"
category: design
status: 已实现
owner: engine
created: 2026-06-29
last_updated: 2026-06-29
supersedes_iteration: mvp
---

# NextRA —— 战斗深度扩展（架构设计）

> 状态：**✅ 已实现**。本文是交付给后续 AI agent / 开发者接手实现的**架构设计**，配套开发计划见 [`combat-depth-plan.md`](combat-depth-plan.md)。
>
> **前置必读**（MVP 已落地的地基，本设计建立在其之上，**不重复**这些内容）：
> - [`docs/designs/nextra-rts-mvp-design.md`](../designs/nextra-rts-mvp-design.md) §4.2 不变量、§5 确定性仿真层、§10 风险表 —— 全部继续生效，本轮**一个字不改地继承**。
> - [`docs/plans/nextra-rts-mvp-plan.md`](../plans/nextra-rts-mvp-plan.md)（M0–M6 已全部交付）。
> - [`docs/projects/nextra/README.md`](../projects/nextra/README.md)（当前可玩状态）。
> - [`AGENTS.md`](../../AGENTS.md)（构建 / 测试纪律）。
>
> **本轮代号 / 范围一句话**：MVP 是"能打的骨架"；本轮把它做成"**有看头、有深度、像红警的战斗**"——单位会转向、炮塔会追踪目标、兵种相互克制、不再穿墙叠点、能造炮塔围墙防御。**仍不碰**资源经济与真联机。

---

## 1. 背景：MVP 现状与本轮定位

### 1.1 MVP 交付了什么（本轮的起点）

M0–M6 已落地一个**单机可玩 + 骨架**的确定性 RTS：

- **真能玩**：选兵 → 移动 / 攻击 / 攻击移动 → 兵营免费出步兵坦克 → 战斗死亡 → 摧毁敌基地判胜；带血条 / 选圈 / 小地图 / 调试网格 / 帧插值 / SyncHash 显示。
- **确定性地基扎实**：Q16.16 定点数学（[`Fixed.h`](../../src/Application/Game/NextRA/Sim/Fixed.h) `Sqrt` 真算、无 float 泄漏）、整数格子 A\*（[`PathfindGrid.cpp`](../../src/Application/Game/NextRA/Sim/PathfindGrid.cpp) 确定性 tie-break）、SyncHash（[`SyncHash.cpp`](../../src/Application/Game/NextRA/Sim/SyncHash.cpp) 按 actorId 排序滚 FNV-1a）、定点 sin/cos 查表。

### 1.2 MVP 在"战斗深度"上的具体缺口（本轮要补的）

调研（含逐文件精读 + 引擎可复用设施确认）结论如下，每条都标了根因位置：

| 缺口 | 现状（根因） | 本轮对策 |
| --- | --- | --- |
| **单位永不转向** | `FSimTransform.facing/prevFacing`（[`SimComponents.h:12-18`](../../src/Application/Game/NextRA/Sim/SimComponents.h)）字段**存在但从未被读写**；`RenderProxySystem::Sync`（[`RenderProxySystem.cpp:56`](../../src/Application/Game/NextRA/Render/RenderProxySystem.cpp)）**只 `SetTranslation` 不 `SetRotation`**。 | §2 朝向 + 炮塔 |
| **没有炮塔概念** | 坦克只是"一个大一点的 box"（[`NextRAGameInstance.cpp:535-578`](../../src/Application/Game/NextRA/NextRAGameInstance.cpp) 单 node），炮管不会转。 | §2 父子层级 + 独立炮塔朝向 |
| **兵种无克制** | 步兵/坦克只有 HP/伤害/射程不同（[`NextRAConfig.hpp`](../../src/Application/Game/NextRA/NextRAConfig.hpp)）；CombatSystem（[`SimWorld.cpp:432`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)）恒定 `hp -= damage`。 | §3 装甲/武器类型表 |
| **单位穿透、叠成一点** | `FPathfindGrid` 只有**静态**阻挡（[`PathfindGrid.cpp:39`](../../src/Application/Game/NextRA/Sim/PathfindGrid.cpp) `SetBlocked`），单位/建筑**不写入 grid**，单位互相 O(n²) 索敌却互不避让。 | §4 碰撞 + 占位 |
| **无防御建筑** | 只有兵营（出兵）+ 基地（胜负），没有炮塔/围墙。 | §5 防御建筑 |
| **配置散落、加兵种要改 5+ 处** | NextRAConfig 是分散的 `constexpr` 函数（按 typeId if-else），加兵种要改 Config + SimWorld + GameInstance + Render 多处。 | §6 数据驱动化（C0 前置） |

### 1.3 In Scope / Out of Scope

| 维度 | 本轮 In Scope | 本轮 Out of Scope（留作后续） |
| --- | --- | --- |
| 战斗 | 朝向 / 炮塔追踪 / 兵种克制 / 软推离阵型 / 防御炮塔 / 围墙 | 溅射、投射物弹道、地形高低差、士气、压制 |
| 兵种 | 现有步兵/坦克归位克制表 + 新增 1 种（反坦克兵，验证克制通路） | 大量新兵种、阵营特性、科技解锁 |
| 建造交互 | 炮塔/围墙用**兵营式生产或场景预置**产出 | "选建筑 → 地图放置" 的建造放置 UI（属"经济+建造"迭代） |
| 经济 | **不做**（生产仍免费） | 资源 / 矿 / 电力 / 造价 |
| 联机 | **不做**（沿用 MVP 单机 playerCount=1） | 真双 World lockstep、SocketTransport |
| 表现 | 朝向/炮管旋转的帧插值 | 骨骼动画、特效、音效 |

> **划界说明**：炮塔/围墙的"机制"在本轮（能造、能开火、能挡路、能被摧毁），但"自由放置建造"的交互留给后续"经济+建造"迭代。本轮炮塔/围墙先用与 MVP 一致的最简产出（兵营生产 / 开局预置）。

---

## 2. 朝向与炮塔旋转

### 2.1 设计目标

- **车身朝向 = 移动方向**：单位移动时朝向速度方向；静止面向上一次方向。
- **炮塔朝向 = 目标方向**：有目标时炮塔（炮管）转向目标，与车身朝向**独立**。
- **限速转向**：朝向不是瞬切，按 `turnSpeed` 每 tick 推进，制造"转向感"（红警坦克调炮的观感）。
- **帧插值平滑**：渲染层在 `prevFacing → currFacing` 间插值，与位置插值同理。

### 2.2 Sim 层改动

**新增定点角度工具（`Sim/WMath.h`，确定性，禁 `std::atan2`）**：

```cpp
// 由水平方向向量算朝向角（整数有理近似 + 象限映射，返回 WAngle 0..4096 一圈）
WAngle Atan2FromVec2(FFixed x, FFixed z);
// 限速转向：curr 朝 target 按 maxStep 推进（环形最短角差，确定性）
WAngle TurnToward(WAngle curr, WAngle target, WAngle maxStep);
```

实现要点（参考现有 `SinApproxHalfTurn` 用整数多项式的风格，[`WMath.h:39-52`](../../src/Application/Game/NextRA/Sim/WMath.h)）：
- `Atan2FromVec2`：按象限处理，主象限内用整数有理近似 `atan`（如 Bhaskara 风格多项式，全程 `int64_t`），再映射到 `[0, 4096)`。朝向精度要求低，多项式近似足够且完全确定。
- `TurnToward`：算 `delta = ShortestAngleDiff(target, curr)`（环形，处理 0/4096 跨越），`delta > 0 ? +min(|delta|, maxStep) : -min(...)`。`AdvanceYawToward`（[`GameplayMath.hpp:22-40`](../../src/Gameplay/Gameplay/GameplayMath.hpp)，浮点版）的整数对应物。

**组件改动（`Sim/SimComponents.h`）**：

```cpp
// 现有 FSimTransform 已有 facing/prevFacing，本轮真正启用：
//   - facing：车身朝向
//   - prevFacing：上一 tick 值，供渲染插值（与 prevPos 同机制）

// 新增炮塔组件（只给有炮塔的单位挂，如坦克、炮塔建筑）
struct FTurret
{
    WAngle facing;          // 炮塔当前朝向（朝目标）
    WAngle prevFacing;      // 上一 tick，渲染插值
    WAngle turnSpeed;       // 每 tick 最大转角（来自 Config）
    FActorId targetActor = static_cast<FActorId>(-1);
};
```

**系统改动（`Sim/SimWorld.cpp`）**：
- `MovementSystem`（现有 [`SimWorld.cpp:469-539`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)）：单位移动时，若位移非零，`facing = TurnToward(facing, Atan2FromVec2(dir.x, dir.z), bodyTurnSpeed)`；每 tick 开头 `prevFacing = facing`（与现有 `prevPos = pos` 同行，[`SimWorld.cpp:486-487`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)）。
- `CombatSystem` / 新增 turret 更新逻辑：有 `FTurret` 且有 target 时，`turret.facing = TurnToward(turret.facing, Atan2ToTarget, turret.turnSpeed)`；`prevFacing = facing` 存档。

### 2.3 渲染层改动

**单位几何：父子 node 组合体**（完全照搬 Brotato3D 先例 [`Brotato3DEffectSystem.cpp:197-289`](../../src/Application/Game/Brotato3D/Brotato3DEffectSystem.cpp)：bodyNode + facingNode SetParent + 多 box 卡车）：

- 坦克 = 车身 box（root）+ 炮塔 box（`SetParent(车身)`，本地偏移在顶部）+ 炮管细长 box（`SetParent(炮塔)`，本地 z 偏移伸向前）。
- 引擎父子层级天然解耦：车身只 `SetTranslation`+`SetRotation(bodyYaw)`，炮塔只 `SetRotation(turretYaw)`，互不影响（`Node::RecalcTransform` 递归下传，[`Node.cpp:92-112`](../../src/Engine/Assets/Core/Node.cpp)）。
- 步兵 = 单 box + `SetRotation(bodyYaw)`（无需炮塔）。

**`RenderProxySystem::Sync`（[`RenderProxySystem.cpp:26-58`](../../src/Application/Game/NextRA/Render/RenderProxySystem.cpp)）追加朝向插值**：

```cpp
// 现有：node->SetTranslation(glm::mix(prevPos, currPos, alpha));
// 新增（车身）：
const float prevYaw = WAngleToRadians(transform->prevFacing);
const float currYaw = WAngleToRadians(transform->facing);
node->SetRotation(glm::slerp(angleAxis(prevYaw, up), angleAxis(currYaw, up), alpha));
// 炮塔子节点同理（按 FTurret.prevFacing/facing 插值），独立 SetRotation
```

`WAngleToRadians`：纯渲染层换算（`facing.value / 4096.0f * 2π`），**只在此处出现 float**，不进 sim。

### 2.4 复用 / 新建清单

| 能力 | 复用 | 新建 |
| --- | --- | --- |
| 父子层级 transform 传播 | `Node::SetParent/AddChild`（[`Node.h:53-60`](../../src/Engine/Assets/Core/Node.h)）、`RecalcTransform` 递归（[`Node.cpp:92-112`](../../src/Engine/Assets/Core/Node.cpp)） | — |
| proc 组合几何 | `FProcModel::CreateBox`（[`FProcModel.h:11`](../../src/Engine/Assets/Loaders/FProcModel.h)）、Brotato3D 多 box 范本（[`Brotato3DEffectSystem.cpp:232-289`](../../src/Application/Game/Brotato3D/Brotato3DEffectSystem.cpp)） | — |
| 朝向限速算法思路 | `AdvanceYawToward`（[`GameplayMath.hpp:22-40`](../../src/Gameplay/Gameplay/GameplayMath.hpp)，浮点版，仅作思路参考） | 定点 `Atan2FromVec2` / `TurnToward`（`Sim/WMath.h`） |
| sim 朝向字段 | `FSimTransform.facing/prevFacing`（[`SimComponents.h:12-18`](../../src/Application/Game/NextRA/Sim/SimComponents.h)，已存闲置） | `FTurret` 组件 |
| 渲染朝向应用 | `Node::SetRotation`（quat）、Brotato3D bodyNode->SetRotation 范式 | `RenderProxySystem::Sync` 加朝向插值段 |

---

## 3. 兵种克制：装甲 / 武器类型表

### 3.1 数据模型

引入两个枚举 + 一张二维伤害系数表（`constexpr`，结构对齐 OpenRA MiniYaml rules，未来可换加载器）：

```cpp
enum class EArmorType : uint8 { Flesh, Light, Heavy, Building };
enum class EWeaponType : uint8 { Bullet, Shell, Rocket, Flame };

// weapon × armor → 伤害百分比（整数，CombatSystem: damage * mul / 100）
constexpr int32_t kDamageMultiplier[4][4] = {
    //            Flesh  Light  Heavy  Building
    /* Bullet */ {  100,    75,    25,     20 },
    /* Shell  */ {   60,   100,   150,     80 },
    /* Rocket */ {   90,   120,   130,    140 },
    /* Flame  */ {  140,    60,    40,     60 },
};
```

### 3.2 CombatSystem 改动（[`SimWorld.cpp:432`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)）

```cpp
// 现有：targetHealth->hp -= attack->damage;
// 改为：
const EWeaponType weapon = UnitWeaponType(attackerType);
const EArmorType armor  = UnitArmorType(targetType);
const int32_t mul = kDamageMultiplier[static_cast<int>(weapon)][static_cast<int>(armor)];
targetHealth->hp -= attack->damage * mul / 100;
```

> 整数百分比，无 float，无歧义，确定性安全。`attack->damage` 本身仍由 Config 提供（基础伤害），克制是乘数。

### 3.3 现有兵种归位 + 新增验证兵种

| 兵种 | Armor | Weapon | 说明 |
| --- | --- | --- | --- |
| Infantry（步兵） | Flesh | Bullet | 基础，对 Heavy（坦克）刮痧 |
| Tank（坦克） | Heavy | Shell | 克制 Heavy（互相高伤），但被 Rocket/Bullet 反制 |
| **新增：Rocketeer（反坦克兵）** | Flesh | Rocket | 验证克制通路：对 Heavy 加成（130%）、对 Building 高伤（140%），但对步兵低效（90%）。**本轮就加这一个**，证明加兵种不再需要改 CombatSystem。 |

> 这张表是"加兵种零散改动"问题的核心解药：未来加兵种 = Config 表加一行（带 armor/weapon），CombatSystem **零改动**。

---

## 4. 碰撞与动态占位（确定性优先）

### 4.1 三层处理

| 层 | 机制 | 解决 | 确定性 |
| --- | --- | --- | --- |
| **静态占位** | 建筑 / 炮塔 / 围墙 footprint 写入 `FPathfindGrid.SetBlocked`（[`PathfindGrid.cpp:39`](../../src/Application/Game/NextRA/Sim/PathfindGrid.cpp)） | 单位寻路绕开建筑、不穿墙 | 已具备（A\* 确定性） |
| **目标格避让** | 移动单位的目标 cell 被占用 → 停 / 重算路径 | 单位不互相叠成一点 | 新增（整数 cell→occupant 哈希） |
| **软推离** | 接近的单位按固定规则相互分散 | 单位群自然成阵型、不挤团 | 新增（确定性排序 + 整数位移） |

### 4.2 占位网格哈希（确定性）

新增 `Sim/OccupancyGrid.{h,cpp}`（整数，与 `FPathfindGrid` 同分辨率同 `CPos`）：

```cpp
class FOccupancyGrid {
    void Clear();
    void Add(FActorId actor, CPos cell);     // 每 tick 重建
    bool IsOccupied(CPos cell) const;        // 目标格避让用
    std::span<const FActorId> ActorsAt(CPos cell) const;
};
```

- 每 tick 在 `MovementSystem` 前**重建一次**（遍历 actors，按整数 CPos 入桶）。完全整数，**禁用引擎 `CPUAccelerationStructure`**（它是浮点 BVH，会破坏 SyncHash，见 §7 红线 R-NEW1）。
- 目标格避让：单位推进前查 `IsOccupied(目标cell)`，占用则停（不清 goal，下一 tick 重试）或重算路径。

### 4.3 软推离算法（确定性，高风险点，单列 §7 风险）

每 tick 在移动系统后、对所有有移动意图的单位：

1. **处理顺序固定**：按 `actorId` 升序（已有 `actors_` 列表的自然顺序，[`SimWorld.cpp:334`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp) 同款）。
2. 对每个单位 A，查其 cell 周围（固定半径，如 1 圈）的占用单位 B（B < A，已处理；B > A，待处理）。
3. 若 A、B 距离小于最小间距，施加排斥位移：方向 = `Sign(A.cell - B.cell)`（整数符号，按主轴优先 x 再 z 的固定规则定方向，避免 0 向量歧义），位移量 = 定点常量。
4. **排斥位移结果进 SyncHash**（位置已覆盖，无需额外字段，但必须保证 sim 顺序确定）。

> 关键：排斥方向用**整数 cell 差的符号**而非 float 归一化方向（避免浮点 / 除零 / NaN）；处理顺序用 actorId 全序。这是软推离确定性的全部秘诀。

---

## 5. 防御建筑：炮塔与围墙

### 5.1 炮塔（Turret）

- **= 带攻击的固定建筑**：复用现有 `FAttack` + `TargetingSystem` + `CombatSystem`（建筑本就进这些系统，[`SimWorld.cpp:332-435`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)），零新增战斗逻辑。
- **加 `FTurret` 组件**（§2.2）：炮管朝目标独立旋转。
- **加 `FBuildingTag` + footprint 占位**：写入 grid 静态阻挡（§4.1）。
- **产出**：本轮用兵营生产或开局预置（`SpawnBuilding`，[`SimWorld.cpp:50-71`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)），与 MVP 一致。

### 5.2 围墙（Wall）

- **= 纯静态阻挡 + 高血量 box**：无攻击、无炮塔，只有 `FHealth` + `FBuildingTag` + footprint 写 grid。
- **被摧毁**：`DeathSystem`（[`SimWorld.cpp:437-467`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)）销毁时**释放 grid 占位**（新增：销毁回调清 `SetBlocked(cell, false)`），让单位恢复通行。

### 5.3 DeathSystem 改动（释放占位）

```cpp
// DeathSystem 销毁 entity 前，若该 entity 占用了 grid，释放其 footprint cells
if (const auto* footprint = registry.try_get<FFootprint>(entity)) {
    for (CPos c : footprint->cells) grid.SetBlocked(c, false);
}
```

需让 `DeathSystem` 能访问 `FPathfindGrid`（当前 [`SimWorld.h`](../../src/Application/Game/NextRA/Sim/SimWorld.h) 的 grid 由 GameInstance 持有并传入 `IssueMove`，需把 grid 引用传入 SimWorld 或在 GameInstance 侧处理释放——**实现时择一，见 plan C5**）。

---

## 6. 数据驱动化（C0 前置支撑）

### 6.1 问题

当前 [`NextRAConfig.hpp`](../../src/Application/Game/NextRA/NextRAConfig.hpp) 是分散的 `constexpr` 函数（`UnitMaxHp`/`UnitDamage`/...，按 typeId if-else），加兵种 / 加 armor / 加炮塔要散落改 5+ 处。本轮要加反坦克兵、加炮塔、加围墙，**先重构 Config 为表**，否则改动会爆炸。

### 6.2 目标结构

```cpp
struct FUnitDef {
    uint16_t typeId;
    const char* name;
    int32_t maxHp;
    Sim::FFixed speedPerTick;     // 移动单位用，建筑为 0
    Sim::WDist attackRange;
    Sim::WDist acquireRange;      // 修正 MVP 的"acquireRange 兵种无关"硬编码 bug
    int32_t damage;
    int32_t cooldownTicks;
    EArmorType armor;
    EWeaponType weapon;
    Sim::WAngle bodyTurnSpeed;    // §2 车身转向速度
    bool hasTurret;               // 是否挂 FTurret（坦克/炮塔 true）
    Sim::WAngle turretTurnSpeed;  // hasTurret 时用
    CPos footprint;               // §4 占位尺寸（建筑>1，单位 1）
    // 几何描述（box 尺寸 / 是否组合体）由 GameInstance 按 typeId 取，仍可留在 Render 层
};

inline constexpr FUnitDef kUnitDefs[] = { /* infantry, tank, rocketeer, barracks, base, turret, wall */ };
inline const FUnitDef& UnitDef(uint16_t typeId);  // 查表
```

### 6.3 顺带修复的 MVP 隐患

- `acquireRange` 兵种无关硬编码（[`SimWorld.cpp:34`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp) 永远 `CellDistance(5)`）→ 改读 `UnitDef(typeId).acquireRange`。
- cooldown 同理（现硬编码 12，[`SimWorld.cpp:34`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)）。

> **零行为变更**：C0 里程碑重构 Config 表后，步兵/坦克现有行为必须逐项不变（用现有单测 + 确定性双跑保护）。这是纯重构，不动玩法。

---

## 7. 确定性与红线（继承 MVP，新增条目）

### 7.1 继承的不变量（MVP design §4.2，一字不改继续生效）

1. sim 只读 order 队列与确定性 PRNG，绝不读 wall-clock / 鼠标 / 相机 / float 渲染态。
2. sim 内禁 float / double / glm。
3. 系统执行顺序固定，组件遍历顺序确定。
4. 输入 → order → sim，无旁路。
5. 渲染只插值不预测。

### 7.2 本轮新增红线（违反即 desync 源）

| # | 红线 | 缓解 |
| --- | --- | --- |
| **R-NEW1** | **软推离 / 占位查询禁用引擎 `CPUAccelerationStructure`**（浮点 BVH，[`CPUAccelerationStructure.h:127`](../../src/Engine/Assets/Acceleration/CPUAccelerationStructure.h) `RayCastInCPU` 是它唯一公开 API，无邻域查询） | 自建整数 `FOccupancyGrid`（§4.2），cell→actor 哈希，全程整数 |
| **R-NEW2** | **朝向计算禁 `std::atan2` / `std::sin`** | 定点 `Atan2FromVec2`（整数多项式 + 象限映射，§2.2）；朝向角度换算 float **只允许在渲染层** `WAngleToRadians` |
| **R-NEW3** | **软推离排斥方向禁用 float 归一化**（除零 / NaN / 跨平台不一致） | 用整数 cell 差符号 + 固定主轴优先规则定方向（§4.3） |
| **R-NEW4** | **软推离 / 朝向 / 占位结果必须进 SyncHash** | `ComputeSyncHash`（[`SyncHash.cpp:32-91`](../../src/Application/Game/NextRA/Sim/SyncHash.cpp)）扩展覆盖 `facing` / `FTurret.facing` / 软推离后位置（位置已覆盖，确认顺序确定） |
| **R-NEW5** | **炮塔朝向 / 软推离的处理顺序跨端必须一致** | 一律按 `actorId` 升序（与现有系统遍历约定一致） |

### 7.3 验证手段（继承 MVP §11 + 本轮强化）

- **定点单测**（`gkNextUnitTests`）：`Atan2FromVec2` 往返误差界、`TurnToward` 边界（0/4096 跨越）、伤害系数表查表、占位哈希同输入同输出。
- **确定性双跑**（MVP D1 同款）：同 order log 双 World 逐 tick hash 相等——**本轮 C4 软推离、C5 占位释放必须过这条**。
- **肉眼**：`gnb shot --target NextRA` 看转向 / 阵型 / 炮塔追踪；`--ui` 看 HUD。
- **构建纪律**：改 NextRA 只 `./gnb build NextRA`，动到可单测纯逻辑再加 `gkNextUnitTests`（AGENTS.md targeted build）。

---

## 8. 文件清单（本轮新增 / 改动）

```
src/Application/Game/NextRA/
  Sim/
    WMath.h              [改] +Atan2FromVec2 / TurnToward
    SimComponents.h      [改] 启用 facing +新增 FTurret / FFootprint
    SimWorld.{h,cpp}     [改] Movement/Combat 加朝向；turret 转向；DeathSystem 释放占位
    SyncHash.cpp         [改] 覆盖 facing / FTurret
    OccupancyGrid.{h,cpp}[新] 整数 cell→actor 占位哈希（§4.2）
    Systems/             [新/改] 软推离逻辑（可入 SimWorld 或独立 SeparationSystem）
  Render/
    RenderProxySystem.{h,cpp} [改] +朝向插值 +炮塔子节点
  NextRAGameInstance.{hpp,cpp} [改] 坦克组合体几何；炮塔/围墙预置；朝向渲染接线
  NextRAConfig.hpp       [改] 表化 FUnitDef（§6）+ 装甲/武器枚举 + 伤害表
src/Tests/
  Test_NextRAFixed.cpp   [改] +朝向/克制/占位单测
```

CMake：`OccupancyGrid.cpp` 加入 `gkNextUnitTests` 源集（纯逻辑可单测），参照现有 [`CMakeLists.txt:58-66`](../../src/CMakeLists.txt) 的 NextRA 测试源段。

---

*本设计与 [`docs/designs/nextra-rts-mvp-design.md`](../designs/nextra-rts-mvp-design.md) 互补：MVP 定"确定性骨架"，本轮定"战斗深度"。落地顺序与验收见 [`combat-depth-plan.md`](combat-depth-plan.md)。*
