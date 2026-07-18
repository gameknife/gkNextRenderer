---
title: "NextRA 架构不变量"
category: project
status: 现行
owner: NextRA
created: 2026-07-17
last_updated: 2026-07-17
---

# NextRA 架构不变量

NextRA 是 20 Hz fixed-step lockstep 原型。旧 MVP 设计中的里程碑已经过期，但下面这些不变量仍解释当前 `src/Application/Game/NextRA/` 的结构，修改时必须保留。

## 分层

```text
SDL/UI/AI input
      ↓ 只生成 FOrder
Order codec / FOrderManager / transport seam
      ↓ tick bucket 全员到齐
FSimWorld fixed-step simulation
      ↓ read-only snapshot + interpolation
FRenderProxySystem / UI / debug
```

输入、AI 和表现层不得直接改 `FSimWorld` component。所有会影响同步状态的动作必须编码成 `FOrder`，由 `ApplyOrders` 在确定的 exec tick 应用。

## 确定性规则

- `simHz=20`；真实帧只向 accumulator 加时间，每帧最多追赶 `maxCatchupTicksPerFrame` 个 tick。
- sim 坐标使用 Q16.16 `FFixed`，角度使用离散 `WAngle`，随机使用显式状态的 `FSimRandom`。sim system 中不得引入 wall clock、`float` 物理积分、平台随机源或渲染 delta。
- system 执行顺序由 `FSimWorld::Step()` 固定。actor id、player id、逻辑网格扫描和 tie-break 必须稳定；不能依赖无序容器迭代来决定目标、伤害或寻路结果。
- `ComputeSyncHash()` 按排序后的 actor id 混入 tick、winner、PRNG 与关键 component。新增会影响未来模拟的状态时，必须同步加入 hash；纯 render link 不加入。
- render interpolation 只读 `prev/current` sim transform。表现层可以平滑、换模型和画特效，但不能把插值结果写回 sim。

## 战斗与移动契约

`FSimWorld::Step()` 的系统顺序是同步协议的一部分：Production → Targeting/Turret/Combat/Death → rebuild occupancy → Movement → Separation → Targeting/Turret/Combat/Death。改变顺序会改变同 tick 内生产、索敌、移动后开火和死亡释放占位的结果，必须视为 replay/sync 行为变更并更新测试，不能作为普通重排。

单位能力集中在 `NextRAConfig.hpp` 的 `FUnitDef` 表，包括生命、速度、射程、伤害、冷却、生产时间、armor/weapon、车身与炮塔转速、footprint 和角色 flags。Combat 使用整数 weapon × armor 百分比矩阵并执行 `damage * multiplier / 100`；新增单位应先扩表和表现映射，不要在 CombatSystem 再加 type-id 分支。

车身方向来自移动向量，炮塔方向来自目标向量；两者都使用 `WAngle`、`Atan2FromVec2()` 与 `TurnToward()` 的定点实现。sim 中禁止换成 `std::atan2`、浮点 quaternion 或归一化向量。RenderProxy 才把 `prevFacing/currentFacing` 转为浮点并插值；炮塔 child 的局部 yaw 是 world turret yaw 减 body yaw。

建筑 footprint 写入 `FPathfindGrid` 的静态阻挡，DeathSystem 必须在销毁 entity 前释放。动态单位由整数 `FOccupancyGrid` 每 tick 重建：Movement 避让被其他 actor 占用的目标 cell，Separation 按 actor id 升序、只处理较小 id 邻居，并用固定主轴整数推离。这里不得改用引擎浮点 BVH/物理查询或无稳定顺序的邻域容器。

`ComputeSyncHash()` 当前覆盖 position/body facing、health、attack target/cooldown、turret facing/target、mobile goal/path cursor、production、base flag 等状态。新增 projectile、buff、fog、资源或其他会影响未来系统决策的 component 时，必须加入稳定序列化/hash；仅添加表现节点不应污染 hash。

## Order 与 lockstep

`FOrderManager` 把本地 order 加 input latency 后放入 tick bucket。每个 player 即使没有 order 也要提交 heartbeat/空 bucket；只有所有 player 到齐，`CanAdvance(tick)` 才允许推进。player bucket 使用有序 `std::map` 合并，保证跨 player 的执行顺序稳定。

Order 二进制格式与 replay 是协议面。修改字段时要同时更新 serialize/deserialize、失败边界、replay version/round-trip 测试和 sync hash 语义，不能只改内存 struct。

`INetTransport` 是 seam，当前 `LoopbackTransport` 和 UI 的 delay/drop/reorder 只是本进程故障注入。它们验证 order gate 和诊断能力，不代表 socket、P2P、lobby、断线恢复或权威服务器已完成。

## Sim 与 scene 的双注册表

sim actor 由 `FSimWorld` 的 entt registry 拥有；scene node 由引擎 Scene 拥有。二者通过 `FRenderLink`/`FRenderProxySystem` 映射，生命周期方向是 sim spawn/destroy 通知表现层创建/隐藏 node。

不要把 `Assets::Node*`、GPU resource、ImGui state 或 component shared_ptr 放进同步状态。反过来，也不要用 node instance id 作为 sim actor 的隐式真值；映射必须显式且可重建。

## 验证

功能现状与候选后续方向见 [roadmap](roadmap.md)。涉及 sim/order 的改动至少应验证：相同 seed + 相同 order stream 得到相同逐 tick sync hash、codec/replay round-trip、不同 render FPS 不改变结果，以及 delay/drop/reorder 注入不会让未到齐 tick 被错误推进。

真实联网若立项，验收应是两个独立进程交换同一 order stream 并比较 hash；不能用 loopback UI 截图代替。
