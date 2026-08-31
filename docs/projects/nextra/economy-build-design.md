---
title: "NextRA —— 经济与建造（资金/矿车/电力/侧边栏建造/多生产线，红警基础 Playable）架构设计"
category: design
status: 待实现
owner: engine
created: 2026-07-18
last_updated: 2026-07-18
supersedes_iteration: combat-depth
---

# NextRA —— 经济与建造（架构设计）

> 状态：**📝 待实现**。本文是交付给后续 AI agent / 开发者接手实现的**架构设计**，配套开发计划见 [`economy-build-plan.md`](economy-build-plan.md)。
>
> **前置必读**（已落地的地基，本设计建立在其之上，**不重复**这些内容）：
> - [`architecture.md`](architecture.md) 的确定性仿真、order/replay 与表现层隔离不变量全部继续生效。
> - [`AGENTS.md`](../../../AGENTS.md)（构建 / 测试纪律）。
>
> **本轮代号 / 范围一句话**：D1 把战斗做出了深度；本轮把"免费随便造"的战斗沙盒推进到**红警基础 Playable**——采矿 → 资金 → 建造场侧边栏造建筑 → 地图放置 → 多生产线出兵 → 与走同一经济循环的 AI 对抗。**仍不碰**真联机（网络栈激活留给 D4）。

---

## 1. 背景：D1 之后的现状与本轮定位

### 1.1 起点（MVP + D1 已交付）

- **战斗已有深度**：朝向 + 炮塔追踪、装甲/武器克制表（[`NextRAConfig.hpp`](../../../src/Application/Game/NextRA/NextRAConfig.hpp) `kDamageMultiplier` + `FUnitDef[]` 表化）、建筑占位 + 目标格避让 + 软推离（[`OccupancyGrid.h`](../../../src/Application/Game/NextRA/Sim/OccupancyGrid.h)）、防御炮塔 + 围墙、DeathSystem 释放占位。
- **确定性地基扎实**：Q16.16 定点、整数 A\*、SyncHash 按 actorId 全覆盖、定点 `Atan2FromVec2`/`TurnToward`、确定性双跑验证流程成熟。
- **7 个兵种/建筑已表化**：infantry / tank / rocketeer / barracks / base / turret / wall，加条目 = Config 表加一行。

### 1.2 本轮要补的缺口（红警对照表存量）

| 缺口 | 现状（根因） | 本轮对策 |
| --- | --- | --- |
| **无资源经济** | 生产免费：`ProductionSystem`（[`SimWorld.cpp:419`](../../../src/Application/Game/NextRA/Sim/SimWorld.cpp)）只看 tick 不看钱；sim 无任何 per-player 状态 | §2 玩家经济状态 + §3 资源系统 |
| **无建造系统** | 建筑只能 `SpawnBuilding` 预置，玩家不能造；红警灵魂"侧边栏选建筑 → 计时 → 地图放置"完全缺失 | §4 建造系统 |
| **无电力** | 无概念 | §5 电力系统 |
| **单生产线、队列深度 1** | `FProduction.queuedTypeId` 单槽（[`SimComponents.h:56-61`](../../../src/Application/Game/NextRA/Sim/SimComponents.h)），只有兵营，无取消 | §6 多生产线 + 队列 |
| **无科技前置** | 任何单位随时可造 | §6 前置表 |
| **AI 不懂经济** | 纯节奏脚本（每 120 tick 免费造兵），经济系统上线后会直接饿死 | §7 AI 经济适配 |
| **战斗反馈弱**（次要） | 即时命中无弹道，交战"看不见谁在打谁" | §8 表现补强（P1 可裁剪） |
| 遗留债 T3/T9 | `actors_` 只增不减；hp 无 clamp 可为负（[`SimWorld.cpp:603`](../../../src/Application/Game/NextRA/Sim/SimWorld.cpp)） | E0 顺手消化 |

### 1.3 "红警基础 Playable" 的定义（本轮北极星）

一局完整游戏跑通以下玩家旅程，全程确定性（双跑 hash 一致）：

> 开局只有**建造场 + 初始资金** → 侧边栏造**电厂** → 造**精炼厂**（附赠矿车，矿车自动采矿回厂换钱）→ 造**兵营 / 战车厂** → 资金花在出兵（步兵/火箭兵/坦克，克制阵容）与**炮塔/围墙**防线上 → 电力不足则生产减速、炮塔哑火 → 对面的 **AI 走同一套经济循环**并周期进攻 → 摧毁对方基地取胜。打掉对方矿车/精炼厂能真实拖垮其经济。

### 1.4 In Scope / Out of Scope

| 维度 | 本轮 In Scope | 本轮 Out of Scope（留作后续） |
| --- | --- | --- |
| 经济 | 矿藏格子 + 矿车采矿状态机 + 精炼厂 + 资金 + 造价扣费 | 多种资源（宝石）、矿石再生、出售建筑、维修 |
| 建造 | 建造场队列 → 就绪 → 地图放置（footprint 校验 + 建造半径）+ 取消退款 | 建筑升级、MCV 展开、多建造场加速 |
| 电力 | 发电厂、低电降效（生产减速 + 炮塔离线） | 电厂受损降输出、超级武器充能 |
| 生产 | 兵营 + 战车厂双生产线、多槽队列、科技前置、rally 点设置 | 机场/海军、暂停队列、批量 5 连造 |
| AI | 脚本化 build order 走 order 通道（经济完整、周期攻击波） | 真策略 AI（侦察/骚扰/回防，属 D5） |
| 战斗表现 | 纯表现层弹道 tracer / 炮口闪光 / 死亡反馈（P1 可裁剪） | sim 层投射物、溅射、动画、音效 |
| 联机 | **不做**（沿用单机 playerCount=1 + AI 注入） | 真双 World lockstep（D4） |

### 1.5 三个关键决策

| 决策点 | 本轮取向 | 理由 |
| --- | --- | --- |
| 矿车是否可被攻击 | **可**（Light 装甲、高 HP、无武装） | 打经济是红警核心博弈；矿车就是普通 actor，反而不做才要加特判 |
| 资源是否有限 | **有限**（矿脉枯竭、不再生；再生留常量开关默认关） | 逼迫扩张与抢矿；无限矿会让龟缩最优 |
| 电力硬上限还是降效 | **降效**：低电时生产速度减半 + 炮塔停止索敌开火 | 红警 1 语义；硬禁止会造成死锁（没电→造不了电厂） |
| （附）胜负条件 | **沿用"基地被毁判负"** | 建造场被毁即无法再建，语义自洽；"全建筑清除"留 E7 可选常量 |

---

## 2. 玩家经济状态

### 2.1 数据模型（sim 内，进 SyncHash）

```cpp
// SimWorld 持有，按 playerId 索引的定长数组（不是 ECS 组件——玩家不是 actor）
struct FPlayerState
{
    int32_t credits = 0;        // 资金
    int32_t powerProduced = 0;  // 每 tick 由 PowerSystem 汇总重算
    int32_t powerConsumed = 0;
};
std::array<FPlayerState, kMaxPlayers /*=2*/> players_;
```

- 全 `int32_t`，无定点需求（资金是整数）。
- **SyncHash 覆盖**：在 actor 循环之后按 playerId 升序滚入 credits/powerProduced/powerConsumed。
- 初始资金 `startingCredits`（Config 常量，默认 2500）。

### 2.2 造价与扣费语义

- `FUnitDef` 新增 `int32_t cost`（见 §9 表结构）。
- **下单立即扣全款**：`Produce` order 在 OrderApply 时校验 `credits >= cost`，通过则立刻 `credits -= cost` 并入队；不足则**静默丢弃 order**（HUD 侧按钮同时做显示层置灰，见 R-ECO4）。
- **取消全额退款**：`CancelProduce` order 移除队尾/指定项并 `credits += cost`。
- 不做红警的"边造边扣"（进度分期扣款）——立即扣全款实现最简、确定性无歧义、退款语义清晰。

---

## 3. 资源系统：矿藏 + 矿车 + 精炼厂

### 3.1 矿藏格子（FResourceGrid）

```cpp
// 与 FPathfindGrid 同分辨率同 CPos，每 cell 一个矿量
class FResourceGrid
{
    int32_t AmountAt(CPos cell) const;
    int32_t Extract(CPos cell, int32_t amount);  // 返回实际采到量，减到 0 为止
    // 内部: std::array<int32_t, W*H>
};
```

- 地图初始化时铺 2 块**对称矿区**（双方基地附近各一块，逐 cell 初始矿量 Config 常量，默认 600/cell、每块 ~30 cell）。
- **渲染**：有矿 cell 铺扁平金色 box（proc 几何），矿量减少时按档位缩小/变暗（表现层读 sim 只读快照，与血条同法）。矿采空移除节点。
- **SyncHash 覆盖**：逐 cell 滚入非零矿量（cell index 升序）。
- **规则**：有矿的 cell 不可放置建筑（§4.3）；矿 cell 不阻挡移动。

### 3.2 矿车（Harvester）状态机

矿车 = 普通 mobile actor（`FMobile` + `FHealth` + Light 装甲无武装）+ 新组件：

```cpp
struct FHarvester
{
    enum class EState : uint8_t { Idle, MoveToOre, Harvesting, MoveToRefinery, Unloading };
    EState state = EState::Idle;
    int32_t load = 0;            // 已采资金值
    CPos targetOreCell;          // 当前目标矿 cell
    FActorId refineryActor = kInvalidActor;  // 回哪个精炼厂
    int32_t actionTicksLeft = 0; // 采/卸计时
};
```

新增 `HarvesterSystem`（固定插入 Step 顺序：ProductionSystem 之后、MovementSystem 之前；遍历按 actorId 升序）：

- **Idle → MoveToOre**：找最近非零矿 cell。**确定性 tie-break**：Chebyshev 距离升序 → cell index 升序（R-ECO3）。找不到矿则保持 Idle（每 N tick 重试）。
- **MoveToOre → Harvesting**：到达目标 cell（复用 `IssueMove` 的路径机制，goal 达成判定同现有 MovementSystem）后开始采集：每 `harvestTicks`（默认 20 tick = 1s）`Extract` 一档（默认 50），累入 `load`；目标 cell 采空则就地重新找矿。
- **满载（load ≥ capacity，默认 500）→ MoveToRefinery**：找最近己方精炼厂（tie-break：距离 → actorId 升序），走到其**卸货格**（精炼厂 footprint 南侧预留 cell）。精炼厂全灭则 Idle 等待（载货保留）。
- **Unloading**：站定 `unloadTicks`（默认 30）后 `players_[owner].credits += load; load = 0` → 回 MoveToOre。
- **玩家可打断**：对矿车下 Move/AttackMove order 时清 harvester 自动目标、置 Idle（order 优先）；到点后下一次 Idle 重试自动找矿恢复运转。
- **SyncHash 覆盖**：state/load/targetOreCell/refineryActor 全滚入。

### 3.3 精炼厂（Refinery）

- 建筑（footprint 2×3，含 1 个卸货预留 cell 不写阻挡）、Building 装甲、耗电。
- **放置完成即附赠一辆矿车**（红警语义，双方一致）：`SpawnMobile(harvester)` 于卸货格旁，省去"先造战车厂才能有矿车"的死锁。
- 矿车也可在战车厂补造（§6）。

---

## 4. 建造系统：侧边栏 → 计时 → 地图放置

### 4.1 三段式流程（红警语义）

```
[侧边栏点建筑按钮]                [建造完成]                  [地图点击落位]
Produce order(建筑 typeId)   →   base 队列计时走完，        →   PlaceBuilding order(typeId, cell)
资金校验+立即扣款，入 base 队列    进入"就绪"槽（不自动放置）      sim 校验通过 → SpawnBuilding + 写 grid
```

- **建筑由建造场（base）生产**：复用现有 `Produce` order + `FProduction` 通道，produce 目标是 base actor。建筑类型完成时不 spawn，而是置入 `FProduction.readyBuildingTypeId`（每建造场同时只挂 1 个就绪建筑，红警同款）。
- **就绪后玩家进入放置模式**：HUD 侧边栏按钮变"Ready"，点击进入放置态——鼠标位置经 `GetScreenToWorldRay`（[`NextRAGameInstance.cpp:748`](../../../src/Application/Game/NextRA/NextRAGameInstance.cpp)，MVP 右键移动已用的地面命中）换算成 cell，画 footprint ghost 预览（合法绿 / 非法红）。
- **左键落位** → 发 `PlaceBuilding` order（只带整数 `CPos` + typeId，见 R-ECO2）；右键/Esc 退出放置态（就绪保留，可再进）。

### 4.2 新增 order 类型

```cpp
enum class EOrderType : uint8_t
{
    Move = 0, AttackMove = 1, Attack = 2, Produce = 3,
    PlaceBuilding = 4,   // targetPos(整数 cell 语义) + produceTypeId
    CancelProduce = 5,   // actorIds[0]=生产建筑, produceTypeId=取消项（就绪建筑也可取消）
    SetRally = 6,        // actorIds[0]=生产建筑, targetPos=新 rally
};
```

### 4.3 放置校验（sim 权威，OrderApply 内，R-ECO4）

`PlaceBuilding` 依次校验，任一失败**静默丢弃**（就绪槽保留）：

1. 下单玩家的某个 base 存在该 typeId 的就绪建筑；
2. footprint 所有 cell：界内、`FPathfindGrid` 未阻挡、`FOccupancyGrid` 无单位、`FResourceGrid` 无矿；
3. **建造半径**：footprint 任一 cell 与**任一己方建筑** footprint cell 的 Chebyshev 距离 ≤ `buildRadiusCells`（默认 6）；
4. 通过 → `SpawnBuilding` + footprint 写 grid（D1-C3 通道复用）+ 清就绪槽。

> 客户端 ghost 预览用同一套规则做**本地预判着色**（绿/红），但只是 UI 提示——sim 校验是唯一权威（联机安全语义，非法 order 丢弃不 desync）。预判逻辑提成 `CanPlaceBuilding(world, playerId, typeId, cell)` 纯函数，sim 与 HUD 共用一份，防止两处规则漂移。

### 4.4 围墙的简化

围墙走同一放置流程（单 cell footprint），本轮**逐段点放**；拖动连线放置留后续。围墙造价低、建造快，就绪→放→再下一段的节奏可接受。

---

## 5. 电力系统

- `FUnitDef` 新增 `int32_t powerDelta`：电厂 +100，建造场 +30（自带小电机），精炼厂 −30、兵营 −20、战车厂 −30、炮塔 −20，其余 0。
- 新增 `PowerSystem`（每 tick、在 ProductionSystem 之前）：按 actorId 升序汇总存活建筑的 powerDelta，正和入 `powerProduced`、负和绝对值入 `powerConsumed`。**每 tick 全量重算**（≤几十个建筑，O(n) 便宜且无增量维护的状态错位风险）。
- **低电判定**：`powerConsumed > powerProduced` 时该玩家进入低电：
  - **生产减速**：ProductionSystem 该玩家的进度**隔 tick 推进**（速度减半，用 `tick % 2` 全局奇偶，无 per-actor 状态）；
  - **炮塔离线**：TargetingSystem/CombatSystem 跳过该玩家的**建筑**攻击者并清其 target（单位不受影响）。
- HUD：电力条（produced/consumed）+ 低电红字警告。

---

## 6. 多生产线与科技前置

### 6.1 生产类别（producedBy）

`FUnitDef` 新增 `EProductionCategory producedBy`：

| 类别 | 生产建筑 | 单位 |
| --- | --- | --- |
| Building | 建造场（base） | powerPlant / refinery / barracks / warFactory / turret / wall |
| Infantry | 兵营 | infantry / rocketeer |
| Vehicle | 战车厂 | tank / harvester |

- 多个同类建筑 = 天然并行队列（FProduction 本就 per-actor）。红警的"多兵营加速单队列"不做，取"多队列并行"这个更简单且同样成立的语义。
- HUD 侧边栏按类别分组（建筑 / 步兵 / 载具三栏），生产按钮自动路由到玩家**actorId 最小的存活对应建筑**（确定性选择；玩家也可选中特定生产建筑后下单）。

### 6.2 队列升级（FProduction）

```cpp
struct FProduction
{
    std::vector<uint16_t> queue;          // 多槽 FIFO（上限 kProductionQueueDepth=5）
    int32_t progressLeft = 0;             // 队首项剩余 tick
    uint16_t readyBuildingTypeId = 0;     // 就绪待放置的建筑（仅建造场用）
    WPos rallyPoint;
};
```

- 队首完成：单位 → rally 点 spawn（现有语义）；建筑 → 进就绪槽，就绪槽被占时队首**暂停不推进**（红警同款）。
- `CancelProduce`：取消指定项退全款；取消就绪建筑同理。
- **SyncHash 覆盖**：queue 内容 + progressLeft + readyBuildingTypeId。

### 6.3 科技前置表

`FUnitDef` 新增 `uint16_t prerequisiteTypeId`（0 = 无；单前置，够用且最简）：

```
powerPlant ← 无        refinery ← powerPlant     barracks ← powerPlant
warFactory ← refinery  turret   ← barracks       wall ← powerPlant
infantry/rocketeer ← barracks（producedBy 隐含）  tank/harvester ← warFactory（隐含）
```

- 校验 = "玩家存在存活的 prerequisiteTypeId 建筑"，在 `Produce` OrderApply 时做（sim 权威）；HUD 按钮置灰 + tooltip 显示缺什么（显示层）。

---

## 7. AI 经济适配

AI 与玩家走**完全相同的经济与建造规则**，全部经现有 `injectedAIOrders_` order 通道（[`NextRAGameInstance.cpp:921`](../../../src/Application/Game/NextRA/NextRAGameInstance.cpp)），sim 内零 AI 特判：

- **脚本化 build order**（tick 驱动状态机）：电厂 → 精炼厂 → 兵营 → 电厂 → 战车厂 → 炮塔×2，每步等资金够+前置满足才下单。
- **放置位置**：预author 的相对建造场偏移表（AI 基地朝向固定）；目标格被占则按**确定性螺旋扫描**找最近合法 cell（复用 §4.3 的 `CanPlaceBuilding` 纯函数）。
- **运营循环**：矿车被打掉且有战车厂+资金 → 补矿车；资金 > 阈值 → 按固定配比出兵（2 步兵 : 1 火箭兵 : 1 坦克）；每 ~90s 集结攻击波 AttackMove 玩家基地（沿用现有节奏机制）。
- AI 决策只读 sim 公开状态（自己的资金/建筑存活/矿车数），无随机；如需抖动用 `FSimRandom`（顺手消化 T5，可选）。

---

## 8. 战斗表现补强（P1，可裁剪，纯表现层）

即时命中的 sim 语义**不变**（无 sim 投射物），只加可读性：

- SimWorld 每 tick 积累 `FAttackEvent { attacker, target, weapon }` 事件表，GameInstance 帧末 `ConsumeAttackEvents()`（与 `ConsumeDestroyedRenderNodeIds` 同模式，[`SimWorld.h:48`](../../../src/Application/Game/NextRA/Sim/SimWorld.h)）。
- 渲染层按事件生成 **tracer**（细长发光 box 从炮口→目标飞 100–150ms 后消亡）、炮口闪光（短暂 scale/emissive 脉冲）、死亡反馈（box 缩没/变黑 0.2s 后移除）。
- 全部渲染态、零 sim 字段、零 SyncHash 影响。武器类型映射颜色（Bullet 黄 / Shell 橙 / Rocket 白烟）。

---

## 9. 数据表扩展（FUnitDef 新增列）

```cpp
struct FUnitDef
{
    // ... 现有字段不变 ...
    int32_t cost = 0;                       // §2.2 造价（0 = 不可生产，如 base）
    int32_t powerDelta = 0;                 // §5 正=供电 负=耗电
    EProductionCategory producedBy = ...;   // §6.1 生产路由
    uint16_t prerequisiteTypeId = 0;        // §6.3 前置（0=无）
    bool harvester = false;                 // §3.2 挂 FHarvester
};
```

新增 typeId：`powerPlantTypeId=8`、`refineryTypeId=9`、`warFactoryTypeId=10`、`harvesterTypeId=11`。基线数值（E7 平衡里程碑再调）：

| 条目 | cost | buildTicks | hp | power | 备注 |
| --- | --- | --- | --- | --- | --- |
| Power Plant | 300 | 100 | 300 | +100 | footprint 2×2 |
| Refinery | 1400 | 160 | 450 | −30 | 2×3，附赠矿车 |
| Barracks（改可建造） | 400 | 120 | 260 | −20 | 现有 def 补列 |
| War Factory | 2000 | 200 | 500 | −30 | 3×3 |
| Turret（改可建造） | 600 | 140 | 300 | −20 | |
| Wall | 100 | 30 | 420 | 0 | |
| Harvester | 1100 | 140 | 350 | — | Light 装甲，速度略慢于坦克 |
| Infantry / Rocketeer / Tank | 100 / 300 / 700 | 现值 | 现值 | — | 补 cost 列 |

---

## 10. 确定性与红线

### 10.1 继承（一字不改）

[`architecture.md`](architecture.md) 中的确定性仿真、统一 order、codec/replay 与 sim/render 隔离
不变量继续生效。

### 10.2 本轮新增红线（违反即 desync / 联机隐患）

| # | 红线 | 缓解 |
| --- | --- | --- |
| **R-ECO1** | **经济状态必须全整数、全进 SyncHash**：credits / power / 矿量 grid / FHarvester 全字段 / FProduction 队列 | §2.1/§3.1/§6.2 的 hash 覆盖清单；双跑必测"采矿中 + 建造中 + 低电"的复合局面 |
| **R-ECO2** | **`GetScreenToWorldRay` 地面命中（float）只许做本地 ghost 预览**；`PlaceBuilding` order 只携带整数 cell；sim 不读任何屏幕/射线结果 | order 结构里 targetPos 落 cell 中心 WPos，OrderApply 端 `ToCell` 还原后按 §4.3 校验 |
| **R-ECO3** | **矿车找矿/找厂的目标选择必须确定性**：距离 tie-break 固定（Chebyshev 距离 → cell index / actorId 升序），遍历按 actorId 升序 | §3.2 状态机规则写死；单测覆盖 tie-break |
| **R-ECO4** | **HUD 的资金/前置/放置 gating 只是显示层，sim OrderApply 是唯一权威校验**，非法 order 静默丢弃（不崩、不半执行） | §4.3 校验链；`CanPlaceBuilding` 纯函数 sim/HUD 共用 |
| **R-ECO5** | **低电降效不得引入 per-actor 计时残留**：减速用全局 `tick % 2` 奇偶，不新增"欠账进度"状态 | §5 实现规则写死 |

### 10.3 验证手段

- **单测**（`gkNextUnitTests`，纯逻辑入测试源集）：扣费/退款/资金不足拒绝、`CanPlaceBuilding` 全规则分支、矿车状态机 tie-break 与打断恢复、前置校验、低电判定、FResourceGrid Extract 边界。
- **确定性双跑**：同 order log 双 World 逐 tick hash 相等——**E1（矿车）、E2（放置）、E5（AI 经济）三个里程碑硬性必过**；复合场景（边采矿边建造边低电边打矿车）纳入回归。
- **肉眼**：`gnb shot --target NextRA --frames N` 看矿区/矿车往返/建造放置/侧边栏（`--ui`）；一局完整 AI 对抗挂机跑通（§1.3 玩家旅程）。
- **构建纪律**：改 NextRA 只 `./gnb build NextRA`；动到入测纯逻辑加 `gkNextUnitTests`（AGENTS.md targeted build）。

---

## 11. 文件清单（本轮新增 / 改动）

```
src/Application/Game/NextRA/
  NextRAConfig.hpp       [改] FUnitDef 加 cost/powerDelta/producedBy/prerequisite/harvester 列；
                              新增 powerPlant/refinery/warFactory/harvester 定义；经济常量
  Net/
    Order.h / Order.cpp  [改] +PlaceBuilding / CancelProduce / SetRally（序列化同步扩展）
  Sim/
    SimComponents.h      [改] +FHarvester；FProduction 升级多槽队列 + readyBuildingTypeId
    SimWorld.{h,cpp}     [改] +players_ 状态 / PowerSystem / HarvesterSystem / 建筑就绪与放置 /
                              低电降效接线 / 附赠矿车 / T3 actors_ compact / T9 hp clamp
    ResourceGrid.{h,cpp} [新] 矿藏格子（整数，入测试源集）
    Placement.{h,cpp}    [新] CanPlaceBuilding 纯函数（sim/HUD 共用，入测试源集）
    SyncHash.cpp         [改] 覆盖 players_ / 矿量 grid / FHarvester / FProduction 队列
    Systems/OrderApplySystem.cpp [改] +PlaceBuilding/CancelProduce/SetRally + Produce 资金/前置校验
  Render/
    RenderProxySystem.{h,cpp} [改] 矿区节点档位更新；（E6）tracer/闪光/死亡反馈
  NextRAGameInstance.{hpp,cpp} [改] 侧边栏 HUD（三栏+资金+电力条）/ 放置模式 ghost 预览 /
                              AI 经济脚本 / 开局布局（仅建造场）/ 矿区几何生成
src/Tests/
  Test_NextRAFixed.cpp   [改] +经济/放置/矿车/前置/低电单测
src/Application/Game/NextRA/CMakeLists.txt 及测试源集 [改] ResourceGrid/Placement 入 gkNextUnitTests
```

---

*当前战斗深度以代码与 [`architecture.md`](architecture.md) 为准；本文件定义经济与建造循环，落地顺序与验收见 [`economy-build-plan.md`](economy-build-plan.md)。*
