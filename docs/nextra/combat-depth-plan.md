---
title: "NextRA —— 战斗深度扩展（朝向/炮塔/克制/碰撞/防御）开发计划"
category: plan
status: 草案
owner: engine
created: 2026-06-29
last_updated: 2026-06-29
supersedes_iteration: mvp
---

# NextRA —— 战斗深度扩展（开发计划）

> 状态：**📝 草案 / ⚠️ 待实现**。本文是交付给后续 AI agent / 开发者的**分阶段开发计划**，与架构设计配套：**先读 [`combat-depth-design.md`](combat-depth-design.md)**（目标架构 + 约束 + 风险），本文只讲**落地顺序、每阶段任务 / 交付物 / 验收 / 验证命令**。
>
> **代号**：`NextRA`（沿用）。**前置必读**：[`combat-depth-design.md`](combat-depth-design.md)（本轮设计）、[`docs/designs/nextra-rts-mvp-design.md`](../designs/nextra-rts-mvp-design.md) §4.2/§10（继承的不变量与红线）、[`AGENTS.md`](../../AGENTS.md)（构建/测试纪律）、[`docs/projects/nextra/README.md`](../projects/nextra/README.md)（当前可玩状态）。
>
> **三项已确认取向**（见 design §1.3）：朝向+炮塔 / 装甲-武器克制表 / 建筑占位+软推离 / 防御炮塔+围墙（机制完整、产出简化）。**不碰**经济与真联机。

---

## 0. 阅读与执行约定

- **每个里程碑独立可验收、独立可构建**：完成即 `./gnb build NextRA` 通过 + 对应验收达成 + （若动到可单测逻辑）`gkNextUnitTests` 绿。
- **构建纪律**（`AGENTS.md`）：改 NextRA 只构建 `NextRA`；动到加入单测的纯逻辑再加 `gkNextUnitTests`；**不要无脑全量 `gnb build`**。
- **确定性是第一公民**：凡碰 `Sim/` 的改动（尤其 **C4 软推离**），提交前必须跑"确定性双跑"（§验收-D）。
- **任务勾选**：每里程碑下 `[ ]` 列表即可执行任务清单；建议接手 agent 配合 `.spec/TODO.md` 工作流逐项落。
- **关键纪律红线**（违反即返工，详见 design §7）：sim 内禁 float / 禁 `std::atan2` / 禁 `CPUAccelerationStructure` / 禁 `FNavGrid`；软推离排斥方向用整数 cell 差符号；系统遍历顺序固定按 actorId；SyncHash 必须覆盖新增字段。

---

## 1. 里程碑总览

| 里程碑 | 主题 | 产出 | 验收一句话 | 依赖 | 风险 |
| --- | --- | --- | --- | --- | --- |
| **C0** | 数据驱动化重构 | `NextRAConfig` 表化为 `FUnitDef[]` + 装甲/武器枚举 | 步兵/坦克行为零变更，现有单测全绿 | — | 低（纯重构） |
| **C1** | 朝向 + 炮塔旋转 | 定点 `Atan2FromVec2`/`TurnToward`、`FSimTransform.facing` 启用、`FTurret`、坦克组合体几何、渲染朝向插值 | 坦克车身朝移动方向、炮管朝目标旋转、帧插值平滑；定点朝向单测绿 | C0 | 中（朝向插值） |
| **C2** | 兵种克制（装甲/武器表） | `kDamageMultiplier[weapon][armor]`、CombatSystem 查表、新增 Rocketeer 反坦克兵 | Rocketeer 对坦克/建筑高伤、对步兵低效，实战体现克制；系数单测绿 | C0 | 低 |
| **C3** | 碰撞：建筑占位 + 目标格避让 | `FOccupancyGrid`、建筑/炮塔/围墙 footprint 写 grid、移动单位目标格避让 | 单位不再穿建筑、不再叠成一点；占位/避让确定性单测绿 | C0 | 中 |
| **C4** | 软推离（确定性分散） | `SeparationSystem`（整数 cell 差符号排斥） | 单位群自然成阵型不挤一团；**确定性双跑逐 tick hash 一致** | C3 | **高**（desync） |
| **C5** | 防御炮塔 + 围墙 | 炮塔（`FAttack`+`FTurret`+footprint）、围墙（footprint+高血量）、DeathSystem 释放占位 | 炮塔自动开火+炮管旋转、围墙阻挡；被摧毁后释放 grid | C1, C3 | 中 |

> C0 是**前置支撑**（否则加兵种改动爆炸）；C1/C2/C3 在 C0 之后可**并行**；C4 依赖 C3（占位哈希）；C5 依赖 C1（炮塔朝向）+ C3（占位）。**C4 是本轮最高风险里程碑**（软推离是 desync 高发区）。

---

## 2. 里程碑详情

### C0 — 数据驱动化重构

**目标**：把 `NextRAConfig.hpp` 从分散 `constexpr` 函数重构为 `FUnitDef[]` 表，并引入装甲/武器枚举与伤害系数表，为后续 C1–C5（加朝向字段、加炮塔、加兵种、加占位）扫清"加一处改五处"的障碍。**零行为变更**。

任务：
- [ ] 定义 `EArmorType` / `EWeaponType` 枚举（`NextRAConfig.hpp`，design §3.1）。
- [ ] 定义 `kDamageMultiplier[weapon][armor]` 二维 `constexpr` 表（design §3.1）。
- [ ] 定义 `FUnitDef` 结构 + `kUnitDefs[]` 表 + `UnitDef(typeId)` 查表函数（design §6.2）：先填现有 4 个（infantry/tank/barracks/base），新字段（`armor`/`weapon`/`bodyTurnSpeed`/`hasTurret`/`turretTurnSpeed`/`footprint`/`acquireRange`）先给默认值，本轮后续里程碑再赋真值。
- [ ] 把 `SimWorld::SpawnMobile`（[`SimWorld.cpp:20-48`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)）里硬编码的 `acquireRange=CellDistance(5)` / `cooldownTicks=12`（[`SimWorld.cpp:34`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)）改为读 `UnitDef(typeId)`，修复 MVP 的兵种无关 bug。
- [ ] 把 `NextRAGameInstance`、`SimWorld`、`RenderProxySystem` 里所有按 typeId if-else 取属性的调用点改为 `UnitDef(typeId).*`，确保步兵/坦克数值逐项不变。

**交付物**：表化的 `NextRAConfig.hpp` + 改为查表的 SimWorld/GameInstance。

**验收**：
1. `./gnb build NextRA gkNextUnitTests` 通过。
2. `gkNextUnitTests` 现有 NextRA 单测全绿（行为零变更）。
3. `gnb shot --target NextRA` 截图与 MVP 对比，步兵/坦克外观、移动、战斗无可见差异。
4. **红线自检**：grep `Sim/` 无新增 float（本里程碑本就不该有）。

**风险**：低。纯重构，靠现有单测 + 截图保护行为。注意 `acquireRange`/`cooldownTicks` 从硬编码改读表后，需确认步兵/坦克现有数值与原硬编码一致（原值 5 格 / 12 tick）。

---

### C1 — 朝向 + 炮塔旋转

**目标**：让单位会转向、坦克炮管会追踪目标。车身朝移动方向、炮塔朝目标，二者独立，帧插值平滑。打通"定点朝向 sim → 父子 node 渲染"全链路。

任务：
- [ ] `Sim/WMath.h` 新增定点 `Atan2FromVec2(FFixed x, FFixed z) → WAngle`（整数多项式 + 象限映射，禁 `std::atan2`，design §2.2）。
- [ ] `Sim/WMath.h` 新增 `TurnToward(WAngle curr, WAngle target, WAngle maxStep) → WAngle`（环形最短角差 + 限步，design §2.2）。
- [ ] `SimComponents.h` 新增 `FTurret { facing; prevFacing; turnSpeed; targetActor }` 组件（design §2.2）。
- [ ] `SimWorld::MovementSystem`（[`SimWorld.cpp:469-539`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)）：每 tick 开头 `prevFacing = facing`（与现有 `prevPos = pos` 同行）；位移非零时 `facing = TurnToward(facing, Atan2FromVec2(dir.x, dir.z), bodyTurnSpeed)`。
- [ ] `SimWorld` 新增炮塔转向逻辑：有 `FTurret` 且有 target 时 `prevFacing = facing; facing = TurnToward(facing, Atan2ToTarget, turret.turnSpeed)`（可入 CombatSystem 或新增 TurretSystem，顺序固定）。
- [ ] `SimWorld::SpawnMobile` / `SpawnBuilding`：坦克 + 炮塔建筑挂 `FTurret`（读 `UnitDef.hasTurret`）。
- [ ] `SyncHash.cpp`（[`SyncHash.cpp:32-91`](../../src/Application/Game/NextRA/Sim/SyncHash.cpp)）：覆盖 `facing` + `FTurret.facing`（确定顺序）。
- [ ] `Render/RenderProxySystem::Sync`（[`RenderProxySystem.cpp:26-58`](../../src/Application/Game/NextRA/Render/RenderProxySystem.cpp)）：`SetTranslation` 后追加 `SetRotation(slerp(angleAxis(prevYaw), angleAxis(currYaw), alpha))`；炮塔子节点按 `FTurret` 独立 SetRotation（design §2.3）。
- [ ] `NextRAGameInstance`：坦克几何改为车身 box（root）+ 炮塔 box（SetParent）+ 炮管细长 box（SetParent 炮塔），照搬 Brotato3D（[`Brotato3DEffectSystem.cpp:232-289`](../../src/Application/Game/Brotato3D/Brotato3DEffectSystem.cpp)）；步兵单 box + SetRotation。
- [ ] `Test_NextRAFixed.cpp`：`Atan2FromVec2` 往返误差、`TurnToward` 边界（0/4096 跨越、目标即当前、maxStep=0）单测。

**验收**：
1. `./gnb build NextRA gkNextUnitTests` 通过。
2. 定点朝向单测绿（`Atan2FromVec2` 误差在容许界内、`TurnToward` 边界正确）。
3. `gnb shot --target NextRA --frames 600`：坦克移动时车身朝向行进方向、遇敌后炮管朝向敌方目标并随其移动而旋转，帧间无跳变。
4. **确定性双跑**（design §7.3）：朝向引入后，同 order log 双 World 逐 tick hash 仍一致。

**红线自检**：朝向计算全程定点，`std::atan2`/`std::sin` 不进 `Sim/`；float 只在渲染层 `WAngleToRadians` 出现。

**风险**：中。朝向插值若 `prevFacing` 未每 tick 存档会跳变（已在任务里强调与 `prevPos` 同机制）；炮塔父子层级的 WorldTransform 由引擎 `RecalcTransform` 递归保证（[`Node.cpp:92-112`](../../src/Engine/Assets/Core/Node.cpp)），无需手算矩阵。

---

### C2 — 兵种克制（装甲/武器表）

**目标**：用装甲/武器类型表实现 rock-paper-scissors，并新增 1 种兵种（反坦克兵）验证"加兵种不再改 CombatSystem"。

任务：
- [ ] `NextRAConfig.hpp`：infantry → `Flesh+Bullet`、tank → `Heavy+Shell`、rocketeer（新增 typeId 5）→ `Flesh+Rocket`；建筑 → `Building`（受击 armor）。
- [ ] `SimWorld::CombatSystem`（[`SimWorld.cpp:432`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)）：`targetHealth->hp -= attack->damage` 改为 `-= attack->damage * kDamageMultiplier[weapon][armor] / 100`（design §3.2）。
- [ ] `NextRAGameInstance`：rocketeer 几何（小 box + 不同颜色，标识反坦克武器）、生产按钮（兵营可造火箭兵）、AI 默认也造一点以观察克制。
- [ ] `Test_NextRAFixed.cpp`：伤害系数表查表单测（weapon×armor 全组合取值正确、整数百分比无溢出）。

**验收**：
1. `./gnb build NextRA gkNextUnitTests` 通过。
2. 系数单测绿。
3. `gnb shot --target NextRA`：实战中 rocketeer 对坦克造成显著高伤（vs 步兵对坦克刮痧），克制可见。
4. **红线自检**：CombatSystem 加兵种零改动（rocketeer 只改 Config 表）。

**风险**：低。纯数据 + 一行查表。注意伤害百分比为整数百分比，`damage * mul` 不溢出 int32（现有伤害量级安全）。

---

### C3 — 碰撞：建筑占位 + 目标格避让

**目标**：单位不再穿透建筑、不再互相叠成一点。建立整数占位哈希。

任务：
- [ ] `Sim/OccupancyGrid.{h,cpp}`：`Add/IsOccupied/ActorsAt`，整数 CPos→actor 哈希（design §4.2）。
- [ ] `SimWorld`：`MovementSystem` 前每 tick 重建 `FOccupancyGrid`（遍历 actors，按 `UnitDef.footprint` 写入对应 cells）。
- [ ] 建筑/炮塔/围墙 footprint（`UnitDef.footprint`）通过 `FPathfindGrid.SetBlocked`（[`PathfindGrid.cpp:39`](../../src/Application/Game/NextRA/Sim/PathfindGrid.cpp)）写入静态阻挡——建筑出生即占位，单位寻路绕开。
- [ ] 移动单位目标格避让：推进前查 `IsOccupied(目标cell)`，占用则停（保留 goal，下 tick 重试）或触发重算路径。
- [ ] `OccupancyGrid.cpp` 加入 `gkNextUnitTests` 源集（[`CMakeLists.txt:58-66`](../../src/CMakeLists.txt) NextRA 测试源段）。
- [ ] `Test_NextRAFixed.cpp`：占位哈希同输入同输出、目标格避让行为单测。

**验收**：
1. `./gnb build NextRA gkNextUnitTests` 通过。
2. 占位/避让单测绿。
3. `gnb shot --target NextRA`：单位遇建筑绕行不穿墙；右键多个单位到同一点，到达后分散在目标附近而非叠在同 cell。
4. **红线自检**：`FOccupancyGrid` 全程整数，**未用** `CPUAccelerationStructure` / `FNavGrid`（design §7 R-NEW1）。

**风险**：中。占位哈希本身确定；避让"停下重试"要注意不要让单位永久卡住（占用方离开后能恢复推进）。

---

### C4 — 软推离（确定性分散）⚠️ 高风险

**目标**：单位群自然排成阵型、不挤成一团。**这是本轮确定性最高风险点**——排斥位移若不确定会跨端 desync。

任务：
- [ ] `SimWorld` 新增 `SeparationSystem`（design §4.3）：移动系统后执行。
- [ ] 处理顺序**严格按 actorId 升序**（与现有系统遍历约定一致，[`SimWorld.cpp:334`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)）。
- [ ] 排斥方向用**整数 cell 差符号 + 固定主轴优先规则**（先 x 后 z，0 向量按固定约定），**禁 float 归一化**（design §7 R-NEW3）。
- [ ] 排斥位移量 = 定点常量；位移结果进 SyncHash（位置已覆盖，确认 `SeparationSystem` 在 hash 前执行且顺序确定）。
- [ ] `SimWorld::Step`（[`SimWorld.cpp:73-84`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)）：把 `SeparationSystem` 插入固定位置（建议 MovementSystem 之后、第二遍 Targeting 之前），**写入文档固定此顺序**。
- [ ] `Test_NextRAFixed.cpp`：软推离同输入同输出单测（两单位重叠 → 沿固定轴分开）。

**验收**：
1. `./gnb build NextRA gkNextUnitTests` 通过。
2. 软推离单测绿。
3. `gnb shot --target NextRA --frames 400`：选 8 个单位右键到一点，到达后自然分散成阵型而非堆成一团。
4. **§验收-D 硬性**：确定性双跑——开软推离后，同 order log 双 World 逐 tick hash 完全相等。**此项不过则 C4 不算完成**。

**红线自检**：排斥方向无 float / 无除零（整数符号）；处理顺序全 actorId 全序；结果进 hash。

**风险**：**高**。desync 高发区。缓解：先做最简规则（固定主轴符号），用确定性双跑反复验证；若双跑失败，第一时间 dump 两边 actor 位置 diff 定位首个分歧 tick。

---

### C5 — 防御炮塔 + 围墙

**目标**：补齐防御维度——炮塔自动开火 + 炮管旋转，围墙静态阻挡，被摧毁释放占位。

任务：
- [ ] `NextRAConfig.hpp`：turret（typeId 6，`FAttack`+`FTurret`+`Building` armor+footprint）+ wall（typeId 7，高血量 +footprint，无攻击）定义。
- [ ] `SimWorld::SpawnBuilding`（[`SimWorld.cpp:50-71`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)）：炮塔挂 `FAttack` + `FTurret`（自动进 Targeting/Combat/Turret 系统）；建筑出生时 footprint 写 `FPathfindGrid.SetBlocked`。
- [ ] `SimWorld::DeathSystem`（[`SimWorld.cpp:437-467`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)）：销毁 entity 前释放其 footprint cells（`SetBlocked(cell, false)`）；需让 DeathSystem 能访问 grid（design §5.3：把 grid 引用传入 SimWorld 或 GameInstance 侧处理，择一）。
- [ ] `NextRAGameInstance`：炮塔几何（基座 box + 炮塔 box SetParent + 炮管 box，朝向随 `FTurret.facing` 旋转，复用 C1 的朝向渲染）；围墙几何（薄长 box）；炮塔/围墙预置（开局放几个，或兵营可造）。
- [ ] `gnb shot` 验证：炮塔在范围内自动开火、炮管追踪敌方单位；围墙挡住单位寻路；摧毁围墙后单位恢复通行。

**验收**：
1. `./gnb build NextRA gkNextUnitTests` 通过。
2. `gnb shot --target NextRA`：炮塔自动索敌开火 + 炮管旋转（复用 C1 朝向）；单位遇围墙绕行；摧毁围墙后 grid 释放、单位可穿。
3. **红线自检**：占位释放后 grid 状态与建筑生死一致（不能出现"建筑已毁但格子仍阻挡"的幽灵占位）。

**风险**：中。DeathSystem 访问 grid 需调整依赖（当前 grid 由 GameInstance 持有，[`SimWorld.h`](../../src/Application/Game/NextRA/Sim/SimWorld.h) 的 `IssueMove` 接收 grid 参数）；注意基地/兵营等现有建筑也要补 footprint 写 grid（C3 已铺路，C5 兜底确认）。

---

## 3. 文件清单（汇总，详见 design §8）

```
src/Application/Game/NextRA/
  NextRAConfig.hpp       [改] FUnitDef 表 + 装甲/武器枚举 + 伤害表 + rocketeer/turret/wall 定义
  Sim/
    WMath.h              [改] +Atan2FromVec2 / TurnToward
    SimComponents.h      [改] 启用 facing + FTurret / FFootprint
    SimWorld.{h,cpp}     [改] 朝向 + turret 转向 + 占位避让 + 软推离 + DeathSystem 释放 grid
    SyncHash.cpp         [改] 覆盖 facing / FTurret
    OccupancyGrid.{h,cpp}[新] 整数 cell→actor 占位哈希
  Render/
    RenderProxySystem.{h,cpp} [改] 朝向插值 + 炮塔子节点
  NextRAGameInstance.{hpp,cpp} [改] 组合体几何 + 炮塔/围墙预置 + 朝向接线
src/Tests/
  Test_NextRAFixed.cpp   [改] 朝向/克制/占位/软推离单测
src/CMakeLists.txt       [改] OccupancyGrid.cpp 加入 gkNextUnitTests 源集
```

---

## 4. 总体 Definition of Done（本轮完成判据）

1. `./gnb build NextRA` 与 `./gnb build gkNextUnitTests` 均通过。
2. 战斗有深度：单位转向、炮塔追踪、兵种克制（rocketeer 验证）、单位不穿墙不叠点且成阵型、炮塔/围墙可防御且可被摧毁释放占位（C1–C5）。
3. 确定性不破：**所有碰 Sim 的改动（尤其 C4 软推离）通过确定性双跑**，逐 tick hash 一致。
4. 红线全部守住：sim 内零 float / 零 `std::atan2` / 零 `CPUAccelerationStructure` / 零 `FNavGrid`；软推离排斥方向整数化；SyncHash 覆盖新字段。
5. 文档：design + 本计划随实现更新勾选与状态；源码引用以 `文件:行号` 维护。

---

## 5. 本轮之后（明确不在本轮内）

- **经济 + 建造放置交互**：矿区/矿车/精炼厂/电力、"选建筑→地图放置"的建造 UI（炮塔/围墙本轮的"简单产出"届时升级为完整建造流）。
- **真联机**：GameInstance 接 `LoopbackTransport`、playerCount=2 真双 World lockstep + 心跳、Replay 录制/回放接通（MVP 网络栈目前是死代码）。
- **战斗表现进阶**：投射物弹道、溅射、死亡特效、骨骼动画、音效。
- **AI**：真正的 RTS bot（仍走 order 通道保确定性）。
- **数据驱动加载器**：`NextRAConfig` 表 → yaml/JSON 运行时加载（本轮先 C++ 表化，结构对齐 yaml）。
- **更多兵种 / 阵营特性 / 科技树**。

---

*实现遵循 design 文档的约束（[`combat-depth-design.md`](combat-depth-design.md) §7 不变量 + 红线）。每里程碑完成后更新本文勾选与状态图例。*
