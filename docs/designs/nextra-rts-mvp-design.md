---
title: "NextRA —— OpenRA 风格帧同步 RTS 原型（MVP 架构设计）"
category: design
status: 草案
owner: engine
created: 2026-06-26
last_updated: 2026-06-26
---

# NextRA —— OpenRA 风格帧同步 RTS 原型（MVP 架构设计）

> 状态：**📝 草案 / ⚪ 待实现**。本文是交付给后续 AI agent / 开发者接手实现的**架构设计**，配套开发计划见 [`docs/plans/nextra-rts-mvp-plan.md`](../plans/nextra-rts-mvp-plan.md)。实现前请先通读 §2 现状分析、§4 总体架构、§5 确定性仿真层、§6 Lockstep，再按计划文档分阶段落地。
>
> **代号**：`NextRA`（application / target 名）
> **参考对象**：[OpenRA](https://github.com/OpenRA/OpenRA)（确定性 lockstep RTS 引擎）
> **前置必读**：[`AGENTS.md`](../../AGENTS.md)、[`AGENT_GUIDE/CharacterDemo.md`](../../AGENT_GUIDE/CharacterDemo.md)（GameInstance + NextGameplay 模板）、本仓库 `src/Application/Game/Brotato3D/`（**最接近的 proc 几何 + 自包含游戏先例**）。
>
> **已与需求方确认的三项关键取向**（决定本文范围）：
> 1. **玩法范围 = 最小可玩闭环**：2 玩家、1–2 兵种、框选 + 移动 + 攻击移动 + 战斗死亡、1 个生产建筑出兵、摧毁敌方基地获胜，**无资源经济**。
> 2. **确定性方案 = 定点整数 sim 坐标**：模仿 OpenRA 的 `WPos`（1024 子格）整数 / 定点数学，sim 层与渲染层 float 解耦，渲染插值。
> 3. **网络深度 = 先本地 loopback 验证**：实现完整 lockstep 内核（order 队列 + 固定 tick + sync hash + replay），用进程内 loopback "网络" + 人工延迟跑通确定性与回放；**真 socket 留作下一里程碑，接口预留好**。

---

## 1. 愿景与 MVP 边界

### 1.1 一句话定位

用引擎现成的 `NextGameInstanceBase` + proc 几何，搭一个 **OpenRA 风格的确定性帧同步 RTS 原型**：俯视相机下，玩家框选单位、右键下达移动 / 攻击命令，单位在**定点整数世界**里寻路、交战、阵亡，造兵建筑产出新单位，摧毁敌方基地即胜。**所有玩法状态变更只能通过 order 驱动的确定性 sim**，从第一天就为多人帧同步打地基。视觉资产全部用 proc 几何体（盒 / 球 / 拉伸多边形）+ 单色 Lambert 材质占位，后续替换。

### 1.2 MVP 做什么 / 不做什么

| 维度 | MVP 内（In Scope） | MVP 外（Out of Scope，留作扩展） |
| --- | --- | --- |
| 玩法 | 框选 / 框选多选、右键移动、攻击移动、自动索敌交战、单位阵亡、生产建筑出兵、摧毁敌基地获胜 | 资源采集 / 经济、科技树、多兵种克制、阵营特性、升级 |
| 单位 | 1–2 个兵种（步兵 / 坦克）+ 1 生产建筑（兵营）+ 1 基地（胜负目标），全部 proc 几何 | 飞行单位、运输、隐身、建造队列依赖、墙体 |
| 玩家 | 2 个席位（本地双席位 + loopback 第二实例）；简单脚本 AI 占位可选 | 真人匹配、天梯、观战、4+ 玩家 |
| 确定性 | **定点整数 sim 坐标 + 固定 tick + 确定性 PRNG + sync hash**（§5、§6） | 跨平台 / 跨编译器位级一致的严格认证（MVP 同构验证即可，但架构按跨平台设计） |
| 网络 | **完整 lockstep 内核 + 进程内 loopback transport + 人工延迟 / 丢包注入 + replay**（§6.4） | 真 UDP/TCP socket、断线重连、追帧（catch-up）、NAT 穿透、专用 server |
| 寻路 | **自写定点格子 A\***（确定性，§5.5）；现有 `NextGameplay::FNavGrid` **不可直接进 sim**（§10 风险 R2） | 分层 / HPA\*、群体避让 flocking、动态障碍重算优化 |
| 渲染 | proc 几何 + faction 上色 + sim→render 插值 + 选择圈 + 血条 | 骨骼动画、特效、贴图、阴影调优、迷雾战争 |
| 相机 / UI | 俯视 RTS 相机（平移 / 缩放）+ ImGui HUD（选择、生产按钮、调试覆盖层） | 美术化 UI、小地图（minimap 列为可选）、回放播放器 UI |
| 平台 | Windows 桌面优先（与现有 dev loop 一致） | 移动端、主机 |

### 1.3 设计目标的优先级

1. **确定性优先于功能**：宁可少做一个兵种，也要保证 sim 逐 tick 可复现（这是帧同步的全部价值）。
2. **sim / 渲染严格分层**：sim 不碰 wall-clock、不碰输入、不碰 float 渲染态；渲染只读 sim 快照做插值（§4.2 不变量）。
3. **order 是唯一真相来源**：玩家操作 → order → 队列 → sim 应用。任何"输入直接改状态"都是 bug。
4. **接口先行**：transport / 单位数据 / 寻路都用接口隔离，loopback 与未来 socket、proc 几何与未来美术资产、定点 A\* 与未来 HPA\* 可平滑替换。

---

## 2. 现状分析：引擎能复用什么、缺什么

### 2.1 可直接复用

| 引擎设施 | 位置 | 在 NextRA 的用途 |
| --- | --- | --- |
| 游戏入口 `NextGameInstanceBase` | [`src/Engine/Runtime/GameInstance.hpp:20`](../../src/Engine/Runtime/GameInstance.hpp)（`OnTick(double)`）、`:36`（`BeforeSceneRebuild`）、`:95`（`CreateGameInstance`） | NextRA 主类继承它，`OnTick` 驱动固定 tick 累加器，`BeforeSceneRebuild` 注入地形 / 初始单位几何，输入回调收集 order |
| proc 几何 `Assets::FProcModel` | [`src/Engine/Assets/Loaders/FProcModel.hpp:11`](../../src/Engine/Assets/Loaders/FProcModel.hpp)（`CreateBox`）、`:12`（`CreateSphere`） | 全部占位资产：单位 / 建筑 / 地形 / 子弹 |
| 场景装配 `Assets::SceneBuilder` | [`src/Engine/Runtime/Scene/SceneBuilder.hpp:12`](../../src/Engine/Runtime/Scene/SceneBuilder.hpp)（`AddLambertianMaterial`）、`:17`（`CreateRenderNode`） | 把 proc model + faction 颜色装成渲染 node |
| 节点 transform | [`src/Engine/Assets/Core/Node.hpp:25`](../../src/Engine/Assets/Core/Node.hpp)（`SetTranslation`）、`:35`（`WorldTransform`） | 渲染层每帧把插值后的 float 位置写进 node |
| ECS + 反射 | entt + entt::meta（见 `AGENTS.md` "Key Architectural Patterns"） | sim World 用独立 `entt::registry`；组件可选反射给编辑器 / 调试面板 |
| 屏幕拾取 `RayCastGPU` | [`src/Engine/Runtime/Engine.hpp:156`](../../src/Engine/Runtime/Engine.hpp)；CPU 版 [`src/Engine/Assets/Acceleration/CPUAccelerationStructure.hpp:127`](../../src/Engine/Assets/Acceleration/CPUAccelerationStructure.hpp)（`RayCastInCPU`） | 鼠标 → 世界射线 → 命中地面 / 单位，转 `WPos` 生成 order（**仅本地表现，不入 sim hash**） |
| ticked task | [`src/Engine/Runtime/Engine.hpp:185`](../../src/Engine/Runtime/Engine.hpp)（`AddTickedTask`） | 可选：HUD / 调试刷新 |
| 共享游戏层 `NextGameplay` | `src/Gameplay/`（[`AI/PathFollower.h`](../../src/Gameplay/AI/PathFollower.h) 等） | **仅渲染 / 表现侧参考**；其寻路 `FNavGrid` 基于浮点 BVH，**不可进 sim**（§10 R2） |

### 2.2 关键缺口（NextRA 必须新建）

| 缺口 | 说明 | 对应章节 |
| --- | --- | --- |
| **固定 tick 仿真层** | 引擎 `OnTick(double deltaSeconds)` 是**变量 dt**（[`GameInstance.hpp:20`](../../src/Engine/Runtime/GameInstance.hpp)），不可直接做确定性。需在 `OnTick` 里用累加器以固定步长驱动 `Sim::Step()` | §5.4 |
| **定点数学 / 整数世界坐标** | 引擎全是 float（GLM）。sim 必须自带 `fixed`/`WPos`/`WAngle`，禁用 float | §5.1–5.3 |
| **Order / 命令系统** | 引擎无任何 gameplay 命令概念 | §6.1 |
| **Lockstep / OrderManager** | 引擎"remote play"是 H.264 像素串流（见 [`docs/designs/webrtc-remoteplay-design.md`](webrtc-remoteplay-design.md)），**与 gameplay 帧同步无关**。lockstep 全新 | §6.2–6.4 |
| **Sync hash / replay** | 无去同步检测、无回放 | §6.5–6.6 |
| **确定性寻路** | 现有 NavGrid 非确定性，需自写定点格子 A\* | §5.5 |
| **RTS 相机 / 选择 / HUD** | 无俯视相机、框选、生产 UI | §7、§8 |

---

## 3. OpenRA 关键设计提炼（我们借鉴什么）

> 目的不是复刻 OpenRA 代码，而是吸收它"为什么能稳定帧同步"的设计决策，落到本引擎的 ECS 上。

| OpenRA 概念 | 要点 | NextRA 的落地 |
| --- | --- | --- |
| **确定性 lockstep** | 每个客户端跑**完全相同**的确定性 sim；网络只传**命令（order）不传状态** | §6 整体 |
| **Net order frame / order latency** | order 不在本帧执行，而是调度到 `currentTick + latency` 帧，给网络留传输窗口；所有 peer 对该帧 order 达成一致后才推进 | §6.3 lockstep gate |
| **整数世界坐标** | `WPos`：1 cell = 1024 子单位（`WDist`/`WAngle` 同族定点）；避免浮点不确定 | §5.2 `WPos`/§5.3 `WAngle` |
| **World / Actor / Trait** | World 持有 Actors；Actor 由 Traits 组合；`ITick` 等接口驱动 | World=`entt::registry`；Actor=entity；Trait→**组件 + 系统**（§5.6） |
| **数据驱动 rules（MiniYaml）** | 单位 / 建筑属性写在 yaml，运行时组装 | MVP 用 **C++ 数据表 / 轻量 JSON**（§5.7），结构对齐未来可换 yaml |
| **Sync hashing** | 每 tick 对带 `[Sync]` 的状态算 hash，peer 间比对，第一时间发现 desync | §6.5 `SyncHash` |
| **确定性 PRNG** | 同 seed 的 MersenneTwister，随机数本身也进 sync | §6.5 `FSimRandom` |
| **Server = order 中继** | 服务器不跑权威 sim，只转发 / 排序 order（+ 充当时钟） | §6.4 transport 抽象；loopback 即"零延迟中继" |
| **Replay = order log** | 录制 = seed + 每帧 order；回放 = 重新喂给同一 sim | §6.6 replay |
| **Immediate orders** | 暂停 / 聊天等不影响 sim 的 order 立即执行，不进 lockstep gate | §6.1 order 分类 |

---

## 4. NextRA 总体架构

### 4.1 分层

```
┌──────────────────────────────────────────────────────────────────────┐
│  Presentation 表现层 (float, 每渲染帧)                                  │
│   - RenderProxySystem: 读 sim 快照(prev/curr WPos) → 插值 → Node       │
│   - RTS 俯视相机 / 选择圈 / 血条 / HUD (ImGui)                          │
│   - 不回写 sim                                                          │
└───────────────▲─────────────────────────────────┬────────────────────┘
                │ 只读 sim 快照                      │ 鼠标拾取 → WPos
┌───────────────┴─────────────────────────────────▼────────────────────┐
│  Input 输入层 (本地, 不入 hash)                                          │
│   - 选择 / 框选 / 右键 → 生成 Order (不直接改状态)                       │
└───────────────────────────────┬───────────────────────────────────────┘
                                 │ LocalOrders(issueTick)
┌────────────────────────────────▼──────────────────────────────────────┐
│  OrderManager / Lockstep (协调)                                         │
│   - 收集本地 order → Transport 广播                                      │
│   - 收齐所有 player 对 execTick 的 order → 放行                          │
│   - 维护 order latency、确认进度                                         │
└──────────────┬──────────────────────────────▲─────────────────────────┘
               │ ExecOrders(tick)              │ 远端 order
┌──────────────▼───────────┐        ┌──────────┴───────────────────────┐
│  Simulation 仿真层         │        │  Transport (INetTransport)        │
│  (定点整数, 固定 tick)     │◄──────►│   - LoopbackTransport (MVP)       │
│   World = entt::registry   │        │   - (未来) SocketTransport        │
│   系统按固定顺序执行        │        │   + 人工延迟 / 丢包注入 (测试)     │
│   每 tick → SyncHash       │        └───────────────────────────────────┘
└────────────────────────────┘
```

### 4.2 核心不变量（实现者必须守住，违反即 desync 源）

1. **sim 只读 order 队列与确定性 PRNG**——绝不读 wall-clock、鼠标、相机、屏幕分辨率、float 渲染态。
2. **sim 内禁用 float**——位置 / 速度 / 角度 / 距离 / 伤害全用定点；任何 `float`/`double`/`glm::vec3` 出现在 sim 组件或系统里都视为 bug（§10 R1）。
3. **系统执行顺序固定**，组件遍历顺序确定（不依赖 entt 默认 view 迭代顺序，见 §5.6 注意事项）。
4. **输入 → order，order → sim**——没有任何旁路。选择 / 相机 / HUD 是纯本地表现态，不入 sync hash。
5. **渲染只插值不预测**——表现层根据 `accumulator/simStep` 在 `prevWPos→currWPos` 间插值；不自行外推影响逻辑。

---

## 5. 确定性仿真层（Simulation Core）

### 5.1 定点数学 `fixed`

- 类型：`struct fixed { int64_t raw; }`，约定 **16.16 或 32.16 定点**（建议 `int64_t` 底、`SHIFT=16`，即 1.0 = 65536）。提供 `+ - * /`、比较、`FromInt`/`ToInt`/`FromFloat`(仅初始化期)/`ToFloat`(仅渲染期)。
- 乘除用 `int64_t` 中间量防溢出：`(a.raw * b.raw) >> SHIFT`、`(a.raw << SHIFT) / b.raw`。
- `Sqrt(fixed)`：定点牛顿迭代或整数二分；`Hypot`/`Length` 基于它。
- 三角：**预生成定点 sin/cos 查表**（按 `WAngle` 索引），禁用 `std::sin`（平台不一致）。
- 约束：sim 内所有派生量（速度积分、距离比较、伤害）只走 `fixed`；`ToFloat` **只允许在表现层**调用。

### 5.2 世界坐标 `WPos` / `WDist` / `CPos`

- `WPos { fixed x, y, z }`：世界位置。约定 **1 cell = 1024 世界单位**（对齐 OpenRA），即 `WPos` 内部以 cell×1024 的定点表达，子格精度足够平滑移动。
- `WDist`：标量距离 / 长度（射程、半径），同定点。
- `CPos { int cx, cy }`：格子坐标（寻路 / 占位用），`WPos↔CPos` 互转为整数除 / 乘 1024。
- `WVec`：位移向量，方向 + 长度运算都在定点。

### 5.3 朝向 `WAngle`

- `WAngle { int32_t a; }`，约定 **0..4095 一圈**（12-bit，对齐 OpenRA 的 1024/圈思路，分辨率取 4096 更顺滑），转角限速、面向目标都走整数环差。
- `Sin/Cos(WAngle)` 查 §5.1 的表，返回 `fixed`。

### 5.4 固定 tick 驱动

- 常量 `SIM_HZ = 20`（每 tick 50ms；可配 25Hz/40ms。OpenRA 默认 ~25 tick/s）。`SIM_STEP = 1/SIM_HZ`。
- 在 `NextRAGameInstance::OnTick(double deltaSeconds)` 内累加：
  ```
  accumulator += deltaSeconds;
  while (accumulator >= SIM_STEP && lockstep.CanAdvance(nextTick)) {
      sim.Step(nextTick);     // 固定步长，纯定点
      accumulator -= SIM_STEP;
      nextTick++;
  }
  renderAlpha = clamp(accumulator / SIM_STEP, 0, 1);   // 交给表现层插值
  ```
- `lockstep.CanAdvance(tick)`：只有当该 tick 所有 player 的 order 都到齐才放行（§6.3）。MVP 单机 latency 可设 0；loopback 双实例时由 gate 控制。
- **限速防螺旋**：单帧最多追 N 个 sim tick，避免卡顿后疯狂追帧。

### 5.5 确定性寻路（定点格子 A\*）

> **关键决策**：现有 `NextGameplay::FNavGrid` 用场景 BVH 浮点射线采样可走性（见 [`src/Gameplay/AI/NavGrid.h`](../../src/Gameplay/AI/NavGrid.h)），**非确定性，禁止进 sim**。NextRA 自写整数网格 A\*。

- 地图 = `W×H` 的 `CPos` 格子，每格 `passable`/`blocked`（建筑占位 + 地形）。
- A\*：开放集用确定性容器（二叉堆 + tie-break 用 `CPos` 顺序，保证同输入同输出），代价 / 启发用整数（曼哈顿 / 八方向用定点欧氏近似）。
- 路径平滑可选；移动沿 waypoint，单位 `FMobile` 每 tick 朝当前 waypoint 推进 `speed` 个定点单位。
- 动态阻挡（单位互相）：MVP 用简单"目标格被占则停 / 重算"，不做群体避让。

### 5.6 World 与组件

- `World` 持有 sim 专用 `entt::registry`（与引擎渲染 registry **物理隔离**，避免渲染组件污染 sim）。
- Actor = entity。组件（全定点 / 整数）：

| 组件 | 字段 | 说明 |
| --- | --- | --- |
| `FSimTransform` | `WPos pos; WAngle facing; WPos prevPos; WAngle prevFacing` | prev 供渲染插值；curr 为权威 |
| `FOwner` | `uint8 playerId` | 阵营 / 归属 |
| `FHealth` | `int hp; int maxHp` | 阵亡判定 |
| `FMobile` | `WDist speed; CPos goal; path; pathCursor` | 可移动单位 |
| `FAttack` | `WDist range; int damage; int cooldown; int cooldownLeft; entity target` | 攻击参数 + 当前目标 |
| `FUnitType` | `uint16 typeId` | 指向数据表（§5.7） |
| `FProduction` | `queue; int progressLeft; CPos rallyPoint` | 生产建筑 |
| `FSelectableTag` / `FBuildingTag` / `FBaseTag` | — | 标签：可选 / 建筑 / 胜负目标 |
| `FRenderLink` | `uint32 renderNodeId` | sim entity ↔ 渲染 node 映射（**表现层维护**，不入 hash） |

- **系统按固定顺序每 tick 执行**（顺序本身是确定性契约的一部分）：
  1. `OrderApplySystem`：把本 tick 的 exec orders 落成组件意图（设 goal / target / 入生产队列）。
  2. `ProductionSystem`：推进生产，到点在 rally 点 spawn 新 actor。
  3. `MovementSystem`：寻路推进 `FMobile`，更新 `FSimTransform`（先存 prev 再更新 curr）。
  4. `TargetingSystem`：为 `FAttack` 选目标（攻击移动 / 自动索敌；选择规则确定性：按距离 + entity id tie-break）。
  5. `CombatSystem`：射程内冷却到点 → 扣血。
  6. `DeathSystem`：hp≤0 销毁 entity（及其占位）；基地死亡 → 置胜负标志。
  7. `SyncHashSystem`：算本 tick hash（§6.5）。
- **遍历顺序注意**：不要依赖 `registry.view<>()` 的默认迭代顺序跨运行 / 跨平台一致。MVP 简单做法：维护一个**按 entity 创建序的稳定 actor 列表**，系统遍历该列表；或对 view 结果按 entity id 排序后处理。entity id 分配本身确定（只在 sim 内 create/destroy）。

### 5.7 单位数据定义

- MVP 用 **C++ `constexpr` 数据表 / 轻量 JSON**（参照 Brotato3D 的 `*Config.hpp` 硬编码风格，见 [`AGENT_GUIDE/Brotato3D.md`](../../AGENT_GUIDE/Brotato3D.md)）。
- 每个 `typeId` → `{ maxHp, speed(WDist), range(WDist), damage, cooldown, buildTime, footprint(CPos 尺寸), 几何描述(盒/球尺寸+颜色) }`。
- 结构对齐 OpenRA rules，未来可换 yaml 加载器而不动 sim。

---

## 6. 命令 / Order 系统 + Lockstep

### 6.1 Order 数据结构与分类

```cpp
enum class EOrderType : uint8 { Move, AttackMove, Attack, Stop, Produce, /*Immediate:*/ Pause, Chat };

struct FOrder {
    EOrderType type;
    uint8      playerId;
    uint32     issueTick;            // 本地发起 tick
    // 载荷（按 type 取用）：
    std::vector<uint32> actorIds;    // 选中的单位（sim entity id 的稳定映射）
    WPos       targetPos;            // 移动 / 攻击移动目标
    uint32     targetActor;          // 攻击目标
    uint16     produceTypeId;        // 生产
};
```

- **Scheduled orders**（Move/Attack/Produce…）：进 lockstep gate，调度到 `execTick` 执行，所有 peer 一致。
- **Immediate orders**（Pause/Chat…）：不影响 sim 状态，立即处理，不入 hash。
- **序列化**：定长 / 紧凑二进制编码，loopback 也走真序列化（保证未来 socket 一致、且能被 replay 复用）。

### 6.2 OrderManager

- 每 sim tick：
  1. 收集本地 input 产生的 `FOrder`（issueTick = currentTick）。
  2. 打包 `{execTick = currentTick + orderLatency, orders[]}` 交 `Transport.Broadcast`。
  3. 从 `Transport` 收取远端 player 的同结构包，按 `execTick` 归档。
  4. 暴露 `GetExecOrders(tick)` 给 `OrderApplySystem`。
- `orderLatency`：MVP loopback 可设 2–3 个 tick（模拟网络窗口）；单机调试设 0。

### 6.3 Lockstep gate（推进协议）

```
对每个 execTick T：
  needed = 所有在局 player 集合
  当 received_orders[T] 覆盖 needed 时 → CanAdvance(T) = true
  否则 sim 暂停在 T（accumulator 继续涨但不 Step），等待
```

- 即 OpenRA 的 net-frame 模型：**只有所有人对 T 的命令都到齐，才一起 Step(T)**，从而每个 peer 的第 T 帧输入完全相同 → 状态完全相同。
- 空命令也要发"该 tick 我没有 order"的心跳包，否则 gate 永远等不到。

### 6.4 Transport 抽象

```cpp
struct INetTransport {
    virtual void Broadcast(uint32 execTick, std::span<const FOrder>) = 0;
    virtual bool Poll(std::vector<FPeerOrders>& out) = 0;     // 收远端
    virtual int  PlayerCount() const = 0;
    virtual ~INetTransport() = default;
};
```

- **`LoopbackTransport`（MVP 交付）**：进程内持有 N 个 `World`（或 N 个 OrderManager 共享一条环形管道），`Broadcast` 把包投递给所有 peer 的收件箱，`Poll` 取出。内置**人工延迟（按 tick 延后投递）+ 丢包 / 乱序注入开关**，用于压测 gate 与确定性。
- **`SocketTransport`（MVP 外，接口已留）**：未来用 UDP/ENet 实现同接口，sim / OrderManager 零改动。

### 6.5 Sync hash 与确定性 PRNG

- `FSimRandom`：seeded（开局 seed 全 peer 一致），实现确定性 PRNG（如 xorshift128 / PCG，整数）。**所有 sim 随机都走它**，其内部 state 进 hash。
- `SyncHashSystem` 每 tick 计算 `hash(tick)`：把所有 actor 的 `{pos, facing, hp, target, cooldownLeft}` + `FSimRandom.state` 以**确定顺序**（按 entity id）滚动进 FNV-1a / CRC64。
- 每 peer 把 `hash(tick)` 随下一批 order 附带交换；发现某 tick hash 不一致 → **desync**：打印两边状态 diff、dump replay，停局。
- MVP loopback 双 World 跑同一份 order，hash 必须逐 tick 相等——这是确定性的核心自动化验收。

### 6.6 Replay

- 录制：`{ seed, playerCount, orderLatency, [每 execTick 的全部 orders] }` 写入 `.nrarep` 文件。
- 回放：新建 World，喂同 seed + order log，逐 tick Step，**重算 hash 序列**应与录制时一致。
- 价值：(1) 确定性回归测试的黄金手段；(2) desync 复现；(3) 未来观战 / 战报。

---

## 7. 表现层（Presentation）

### 7.1 sim → render 映射与插值

- 每个 sim actor 创建时，表现层用 `FProcModel::CreateBox/CreateSphere`（[`FProcModel.h:11`](../../src/Engine/Assets/Loaders/FProcModel.hpp)）+ `SceneBuilder::AddLambertianMaterial`（按 `FOwner.playerId` 取 faction 色）+ `CreateRenderNode`（[`SceneBuilder.h:17`](../../src/Engine/Runtime/Scene/SceneBuilder.hpp)）建一个渲染 node，记到 `FRenderLink`。
- 每渲染帧：`renderPos = lerp(prevPos.ToFloat(), currPos.ToFloat(), renderAlpha)`，朝向同理（环形插值），写 `Node::SetTranslation`（[`Node.h:25`](../../src/Engine/Assets/Core/Node.hpp)）。`renderAlpha` 来自 §5.4。
- actor 销毁 → 移除对应 node。

### 7.2 占位几何约定

| 实体 | 几何 | 备注 |
| --- | --- | --- |
| 步兵 | 小盒 / 矮胶囊 | faction 色 |
| 坦克 | 盒身 + 细长盒炮管 | 炮管随 facing 转 |
| 兵营（生产） | 中盒 | 顶部小块标识 |
| 基地（胜负目标） | 大盒 | 血量醒目 |
| 子弹 | 小球或省略（瞬时命中） | MVP 可先不画弹道 |
| 地形 | 大平面 + 可选网格线 | 格子可视化便于调试寻路 |

### 7.3 相机 / 选择表现

- **俯视 RTS 相机**：实现 `OverrideRenderCamera`（[`GameInstance.hpp` 同接口](../../src/Engine/Runtime/GameInstance.hpp)），固定俯角，WASD / 屏幕边缘平移、滚轮缩放。相机是本地态，**不进 sim**。
- **选择圈 / 血条**：选中单位画环（proc 线 / 贴地 box）+ 头顶血条（ImGui overlay 或 proc）。

---

## 8. 输入与交互（产生 Order）

- **屏幕 → 世界**：鼠标射线用 `RayCastGPU`（[`Engine.hpp:156`](../../src/Engine/Runtime/Engine.hpp)）或 CPU `RayCastInCPU`（[`CPUAccelerationStructure.h:127`](../../src/Engine/Assets/Acceleration/CPUAccelerationStructure.hpp)）命中地面 / 单位 → 命中点 `float` 转 `WPos`（仅这一步 float→定点，发生在**生成 order 时**，order 内是定点，故确定）。
- **选择**：左键单击选单位、拖框多选、双击选同类型；选择集是**本地表现态**。
- **命令**：右键——命中敌方单位 → `Attack`；命中地面 → `Move`；按住修饰键 → `AttackMove`。生产建筑选中后 HUD 出"造步兵 / 造坦克"按钮 → `Produce`。
- **关键纪律**：输入处理**只构造 `FOrder` 入 OrderManager**，绝不直接改 sim 组件（§4.2 不变量 4）。

---

## 9. 目录结构与 target 接入

### 9.1 建议文件布局（MVP 自包含于 application，后期可下沉 NextGameplay）

```
src/Application/Game/NextRA/
├── NextRAGameInstance.{hpp,cpp}     # 入口：OnInit/OnTick(累加器)/输入/相机/HUD/编排
├── NextRAConfig.hpp                 # 兵种 / 数值数据表（对齐 Brotato3D 风格）
├── Sim/
│   ├── Fixed.h                      # 定点数学
│   ├── WMath.h                      # WPos/WDist/WAngle/WVec + sin/cos 表
│   ├── SimWorld.{h,cpp}             # entt registry + Step(tick) + 系统编排
│   ├── SimComponents.h              # FSimTransform/FHealth/FMobile/FAttack/...
│   ├── Systems/*.{h,cpp}            # OrderApply/Production/Movement/Targeting/Combat/Death
│   ├── PathfindGrid.{h,cpp}         # 定点格子 A*
│   ├── SimRandom.h                  # 确定性 PRNG
│   └── SyncHash.{h,cpp}
├── Net/
│   ├── Order.h                      # FOrder + 序列化
│   ├── OrderManager.{h,cpp}         # 收集 / 调度 / gate
│   ├── INetTransport.h
│   ├── LoopbackTransport.{h,cpp}    # MVP transport（含延迟 / 丢包注入）
│   └── Replay.{h,cpp}               # 录制 / 回放
└── Render/
    ├── RenderProxySystem.{h,cpp}    # sim→node 插值
    ├── RtsCamera.{h,cpp}
    └── NextRAHud.{h,cpp}            # 选择圈 / 血条 / 生产按钮 / 调试覆盖层
```

### 9.2 CMake 接入（参照 CharacterDemo）

- 在 [`src/CMakeLists.txt`](../../src/CMakeLists.txt) 新增 `add_executable(NextRA ...)`（参照 `CharacterDemo` 的 `:206`），`target_include_directories(NextRA PRIVATE Application/Game/NextRA)`，`DEV_MODE=1`。
- 链接：`target_link_libraries(NextRA PRIVATE gkNextEngine ...)`（若复用 NextGameplay 则照 `:602–603` 加 `NextGameplay`；MVP 寻路自写，可暂不依赖）。
- sim 的纯逻辑（Fixed/WMath/PathfindGrid/SyncHash/Order）尽量做成**不依赖 Vulkan 的可单测单元**，加入 `gkNextUnitTests` 源集，便于确定性单测（参照现有 `Test_*` 用法）。
- `CreateGameInstance` 在 `NextRAGameInstance.cpp` 定义（参照 [`CharacterDemoGameInstance.cpp:32`](../../src/Application/Game/CharacterDemo/CharacterDemoGameInstance.cpp)）。

---

## 10. 关键风险与缓解

| # | 风险 | 影响 | 缓解 |
| --- | --- | --- | --- |
| **R1** | float 泄漏进 sim | 跨 peer desync | 约定 sim 内禁 float（§4.2/§5.1）；code review + 可加 grep/clang-tidy 检查 `float`/`glm` 出现在 `Sim/`；sync hash 早暴露 |
| **R2** | 误用 `FNavGrid`（浮点 BVH）做 sim 寻路 | desync | **明令禁止**；自写定点格子 A\*（§5.5）；NavGrid 仅可用于纯表现 |
| **R3** | entt view 迭代顺序不确定 | desync | 系统遍历走稳定 actor 列表 / 按 entity id 排序（§5.6） |
| **R4** | `std::sin`/`sqrt`/浮点三角进 sim | 跨平台不一致 | 定点查表 + 整数 sqrt（§5.1） |
| **R5** | sim / render registry 混用 | 渲染态污染逻辑、hash 抖动 | 两个 registry 物理隔离；`FRenderLink` 是唯一桥且不入 hash（§5.6） |
| **R6** | lockstep gate 心跳缺失 | 空命令帧永久卡住 | 每 tick 必发包（哪怕空 order）（§6.3） |
| **R7** | 卡顿后追帧螺旋 | 表现层假死 | `OnTick` 单帧限追 N tick（§5.4） |
| **R8** | 容器 / 排序的实现定义行为 | 隐蔽 desync | A\* tie-break、目标选择 tie-break 全部显式确定（§5.5/§5.6） |
| **R9** | MVP 同构验证通过但跨平台仍 desync | 真多人翻车 | 架构按跨平台设计（定点 + 查表），但**明确 MVP 只认证同构 loopback**；跨平台认证列入 MVP 外 |

---

## 11. 验证手段（贯穿开发）

1. **单元测试（`gkNextUnitTests`）**：定点数学（乘除 / sqrt / 三角往返误差界）、`WPos↔CPos` 转换、order 序列化往返、A\* 同输入同输出、sync hash 对同状态稳定。
2. **确定性双跑**：同一 order log 在两个全新 `SimWorld` 各跑一遍，**逐 tick hash 必须相等**（自动化断言）。
3. **loopback 集成**：双 OrderManager + `LoopbackTransport`，开人工延迟 / 丢包注入，仍逐 tick hash 一致；gate 行为正确（缺包即等待）。
4. **replay 回归**：录一局 → 回放 → hash 序列与原局一致。
5. **肉眼 / 渲染**：`gnb run NextRA` 实机操作；`gnb shot --target NextRA` 截图验证画面与 HUD（见 `AGENTS.md` "Agent Visual Validation"）。
6. **构建纪律**：按 `AGENTS.md` targeted build——改 NextRA 只 `./gnb build NextRA`（+ 动到可单测逻辑则 `./gnb build gkNextUnitTests`）。

---

## 12. 附录：OpenRA ↔ NextRA 概念对照

| OpenRA | NextRA |
| --- | --- |
| `World` | `SimWorld`（`entt::registry`） |
| `Actor` + `Trait` | entity + 组件 + 系统 |
| `WPos`/`WDist`/`WAngle`（int） | `WPos`/`WDist`/`WAngle`（`fixed`/int） |
| `CPos`（cell） | `CPos` |
| `Order` / `OrderManager` | `FOrder` / `OrderManager` |
| net order frame / order latency | `execTick = tick + orderLatency` + lockstep gate |
| `[Sync]` + sync hash | `SyncHashSystem` |
| MersenneTwister（synced） | `FSimRandom`（synced PRNG） |
| server = order relay | `INetTransport`（loopback / 未来 socket） |
| replay | `.nrarep` order log |
| MiniYaml rules | `NextRAConfig` 数据表（未来可换 yaml） |

---

*本设计是 NextRA 的"目标架构 + 约束"。落地顺序、里程碑与验收见配套开发计划：[`docs/plans/nextra-rts-mvp-plan.md`](../plans/nextra-rts-mvp-plan.md)。*
