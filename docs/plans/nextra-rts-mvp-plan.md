---
title: "NextRA —— OpenRA 风格帧同步 RTS 原型（MVP 开发计划）"
category: plan
status: 草案
owner: engine
created: 2026-06-26
last_updated: 2026-06-26
---

# NextRA —— OpenRA 风格帧同步 RTS 原型（MVP 开发计划）

> 状态：**📝 草案 / ⚪ 待实现**。本文是交付给后续 AI agent / 开发者的**分阶段开发计划**，与架构设计配套：先读 [`docs/designs/nextra-rts-mvp-design.md`](../designs/nextra-rts-mvp-design.md)（目标架构 + 约束 + 风险），本文只讲**落地顺序、每阶段任务 / 交付物 / 验收 / 验证命令**。
>
> **代号**：`NextRA`（target 名）。**前置必读**：设计文档 §4–§6、[`AGENTS.md`](../../AGENTS.md)（构建 / 测试纪律）、[`AGENT_GUIDE/CharacterDemo.md`](../../AGENT_GUIDE/CharacterDemo.md)（GameInstance 模板）、`src/Application/Game/Brotato3D/`（proc 几何 + 自包含游戏先例）。
>
> **三项已确认取向**（同设计文档 §1）：最小可玩闭环 / 定点整数 sim / 先本地 loopback。

---

## 0. 阅读与执行约定

- **每个里程碑独立可验收、独立可构建**：完成即 `./gnb build NextRA` 通过 + 对应验收项达成 + （若动到可单测逻辑）`gkNextUnitTests` 绿。
- **构建纪律**（`AGENTS.md`）：改 NextRA 只构建 `NextRA`；动到加入单测的纯逻辑再加 `gkNextUnitTests`；**不要无脑全量 `gnb build`**。
- **确定性是第一公民**：凡碰 `Sim/` 的改动，提交前必须跑"确定性双跑"测试（§验收-D）。
- **任务勾选**：每里程碑下 `[ ]` 列表即可执行任务清单；建议接手 agent 配合 `.spec/TODO.md` 工作流逐项落。
- **关键纪律红线**（违反即返工，详见设计 §4.2 / §10）：sim 内禁 float；输入只产 order；系统 / 遍历顺序固定；sim 与 render registry 隔离；不得用 `FNavGrid` 做 sim 寻路。

---

## 1. 里程碑总览

| 里程碑 | 主题 | 产出 | 验收一句话 | 依赖 |
| --- | --- | --- | --- | --- |
| **M0** | 脚手架 + 俯视相机 | NextRA target、空 sim、静态场景 | `gnb run NextRA` 出俯视画面 + 几个 proc 盒子 | — |
| **M1** | 定点数学 + 固定 tick sim 骨架 | `Fixed`/`WMath`/`SimWorld`、累加器、单单位定点直线移动 + 渲染插值 | 一个盒子在 sim 里定点匀速移动，渲染平滑；定点单测绿 | M0 |
| **M2** | 选择 + 移动指令闭环（单机） | 选择 / 框选、右键 Move、Order 系统、定点格子 A\* | 框选单位右键地面 → 寻路移动到点 | M1 |
| **M3** | 战斗闭环 | `FHealth`/`FAttack`、Targeting/Combat/Death、攻击移动 | 两军遭遇自动交战、阵亡消失 | M2 |
| **M4** | 生产 + 胜负 | `FProduction` 出兵、基地血量、胜负判定、2 席位 | 兵营出兵、摧毁敌基地弹胜利 | M3 |
| **M5** | Lockstep + loopback + sync/replay | OrderManager gate、`LoopbackTransport`、SyncHash、Replay | 双 World 逐 tick hash 一致；replay 重放一致 | M4 |
| **M6** | 打磨 + 调试工具 | HUD、调试覆盖层、延迟 / 丢包注入开关、minimap(可选) | 可演示对局 + 可视化 tick/hash/order | M5 |

> M0–M4 是**单机可玩 RTS**；M5 把它"帧同步化"；M6 打磨。每步都基于前一步可运行。

---

## 2. 里程碑详情

### M0 — 脚手架 + 俯视相机

**目标**：跑起来一个空 NextRA，俯视相机看着一块地 + 几个静态 proc 盒子。打通 target / 入口 / 相机 / proc 几何四条管线。

任务：
- [x] 在 [`src/CMakeLists.txt`](../../src/CMakeLists.txt) 加 `add_executable(NextRA ...)`（参照 `CharacterDemo` `:206`、链接 `:602` 段），`target_include_directories` + `DEV_MODE=1`，链接 `gkNextEngine`。
- [x] `NextRAGameInstance.{hpp,cpp}` 继承 `NextGameInstanceBase`（[`GameInstance.hpp:20`](../../src/Engine/Runtime/GameInstance.hpp)），实现 `OnInit/OnTick/OnDestroy/OnRenderUI` 空壳 + `CreateGameInstance`（参照 [`CharacterDemoGameInstance.cpp:32`](../../src/Application/Game/CharacterDemo/CharacterDemoGameInstance.cpp)）。
- [x] `BeforeSceneRebuild`（[`GameInstance.hpp:36`](../../src/Engine/Runtime/GameInstance.hpp)）注入：大平面地形（`FProcModel::CreateBox` 扁盒，[`FProcModel.h:11`](../../src/Engine/Assets/Loaders/FProcModel.h)）+ 几个彩色盒（`SceneBuilder::AddLambertianMaterial`/`CreateRenderNode`，[`SceneBuilder.h:12/17`](../../src/Engine/Runtime/Scene/SceneBuilder.h)）。
- [x] `RtsCamera` + `OverrideRenderCamera`：固定俯角，WASD / 边缘平移、滚轮缩放。

**验收**：`./gnb build NextRA` 通过；`./gnb run NextRA` 显示俯视地面 + 盒子，相机可平移缩放；`gnb shot --target NextRA` 截图正确。

---

### M1 — 定点数学 + 固定 tick sim 骨架

**目标**：建立确定性地基。一个 sim actor 在定点世界里匀速直线移动，渲染层插值平滑显示。

任务：
- [x] `Sim/Fixed.h`：`fixed` 类型 + 四则 / 比较 / `FromInt/ToInt/ToFloat` + `Sqrt`（设计 §5.1）。
- [x] `Sim/WMath.h`：`WPos/WDist/WAngle/WVec/CPos` + 互转 + 定点 `Sin/Cos` 查表（§5.2/5.3）。
- [x] `Sim/SimComponents.h`：`FSimTransform/FOwner/FHealth/FMobile/...`（§5.6，先用到的几个）。
- [x] `Sim/SimWorld.{h,cpp}`：独立 `entt::registry` + `Step(tick)` + 系统编排骨架（先只有 Movement）+ 稳定 actor 列表（§5.6 遍历顺序）。
- [x] `NextRAGameInstance::OnTick` 接入固定 tick 累加器（`SIM_HZ=20`，§5.4），算 `renderAlpha`。
- [x] `Render/RenderProxySystem`：sim actor ↔ 渲染 node 映射（`FRenderLink`），每帧 `lerp(prevPos,currPos,alpha)` 写 `Node::SetTranslation`（[`Node.h:25`](../../src/Engine/Assets/Core/Node.h)）。
- [x] 把 `Fixed/WMath` 纯逻辑加入 `gkNextUnitTests` 源集，写定点 / 坐标转换单测。

**验收**：一个盒子在 sim 中定点匀速移动，渲染丝滑无抖动；`gkNextUnitTests` 定点 / 坐标用例绿；改 sim 步长 / 帧率插值仍正确。

---

### M2 — 选择 + 移动指令闭环（单机）

**目标**：完成"选择 → order → sim 寻路移动"全链路（单机 latency=0，先不上网络 gate）。

任务：
- [x] `Net/Order.h`：`FOrder` + 序列化往返（§6.1）。
- [x] `Net/OrderManager`（单机最小版）：收集本地 order，`GetExecOrders(tick)` 直接返回本 tick（latency=0）。
- [x] 输入：左键单击选 / 拖框多选（`RayCastGPU` [`Engine.hpp:156`](../../src/Engine/Runtime/Engine.hpp) 或 CPU `RayCastInCPU` [`CPUAccelerationStructure.h:127`](../../src/Engine/Assets/Acceleration/CPUAccelerationStructure.h)）；右键地面 → `Move` order（命中点 float→`WPos`，§8）。
- [x] `Sim/PathfindGrid.{h,cpp}`：定点格子 A\*（确定性 tie-break，§5.5）。
- [x] `Systems/OrderApplySystem` + `Systems/MovementSystem`：order 落 goal → A\* 路径 → `FMobile` 逐 tick 沿 waypoint 定点推进。
- [x] 选择表现：选中圈（`Render/NextRAHud` 起步）。

**验收**：框选一组盒子，右键地面，单位寻路移动到目标并停；多单位目标不同时各自寻路；A\* 单测（同输入同路径）绿。**红线自检**：寻路全程无 float、未用 FNavGrid。

---

### M3 — 战斗闭环

**目标**：单位能自动索敌、交战、扣血、阵亡。攻击移动可用。

任务：
- [x] 组件补全 `FHealth/FAttack`（§5.6）；第二兵种（坦克：盒 + 炮管，炮管随 facing）。
- [x] `Systems/TargetingSystem`：射程 / 视野内选目标（确定性：距离 + entity id tie-break，§5.6）。
- [x] `Systems/CombatSystem`：冷却到点扣血（瞬时命中，子弹可省略或画小球）。
- [x] `Systems/DeathSystem`：hp≤0 销毁 actor + 移除 render node + 释放格子占位。
- [x] 命令扩展：右键命中敌 → `Attack`；修饰键 → `AttackMove`（移动途中自动交战）。
- [x] 血条表现（`NextRAHud`）。

**验收**：两军单位相遇自动开火、血量下降、阵亡消失；攻击移动行进中遇敌停下交战；战斗逻辑可在确定性双跑下复现（接 M5 后强校验，本阶段先人工 + 固定 seed 双跑）。

---

### M4 — 生产 + 胜负

**目标**：补齐"最小可玩闭环"——造兵 + 摧毁敌基地获胜，配两个席位。

任务：
- [x] `FProduction` + `Systems/ProductionSystem`：兵营选中 → HUD 出"造步兵 / 坦克"按钮 → `Produce` order → 计时 → rally 点 spawn。
- [x] 基地 `FBaseTag` + 高血量；`DeathSystem` 检测基地死亡 → 置胜负标志。
- [x] 2 席位：player 0 / player 1 各一基地 + 兵营 + 初始单位；faction 上色区分。
- [x] 胜负 UI：一方基地被摧毁 → 显示 Victory/Defeat。
- [x] （可选）极简脚本 AI 占位：player 1 周期性造兵 + 攻击移动向敌基地（**走 order 通道**，保持确定性）。

**验收**：完整对局可玩——造兵、推进、摧毁敌基地弹胜利；全流程仅通过 order 驱动 sim（红线自检）。

---

### M5 — Lockstep + loopback transport + sync hash + replay

**目标**：把单机 sim "帧同步化"。这是本计划的**技术核心验收点**。

任务：
- [x] `Sim/SimRandom.h`：确定性 PRNG，所有 sim 随机改走它（§6.5）；接入需要随机的系统（如 tie-break 外的散布）。
- [x] `Sim/SyncHash.{h,cpp}` + `SyncHashSystem`：每 tick 按 entity id 顺序滚 hash（pos/facing/hp/target/cooldown + PRNG state，§6.5）。
- [x] `Net/INetTransport.h` + `Net/LoopbackTransport`：进程内多 World 投递，含**人工延迟 + 丢包 / 乱序注入**开关（§6.4）。
- [x] `OrderManager` 升级为 lockstep：`execTick = tick + orderLatency`，gate 收齐所有 player 才 `CanAdvance`，空 tick 发心跳包（§6.2/6.3）；`OnTick` 累加器接 `CanAdvance` 限流（§5.4）。
- [x] `Net/Replay.{h,cpp}`：录制 `{seed, latency, 每 execTick orders}` 到 `.nrarep`；回放重建 World 重算 hash。
- [x] 测试装置：双 `SimWorld` + `LoopbackTransport` 跑同一对局；确定性双跑断言；replay 回归。

**验收（硬性）**：
- D1 **确定性双跑**：同 order log 两个全新 World 逐 tick hash 完全相等。
- D2 **loopback 一致**：双 World 经 `LoopbackTransport`（开延迟 + 丢包注入）跑完，逐 tick hash 一致；缺包时 gate 正确等待不前进。
- D3 **replay**：录一局 → 回放 → hash 序列与原局逐 tick一致。
- 以上尽量做成 `gkNextUnitTests` / 集成测试用例，纳入回归。

---

### M6 — 打磨 + 调试工具

**目标**：让对局可演示、可诊断。

任务：
- [x] HUD 完整化：选择圈 / 血条 / 生产按钮 / 当前选中信息 / 胜负横幅。
- [x] 调试覆盖层：当前 tick、各 peer hash、order log 滚动、A\* 路径 / 格子可走性可视化（参照 CharacterDemo 的 AI/NavGrid 调试 UI 风格，[`AGENT_GUIDE/CharacterDemo.md`](../../AGENT_GUIDE/CharacterDemo.md)）。
- [x] 运行时开关：order latency、人工延迟、丢包率（用于现场演示 lockstep 韧性）。
- [x] （可选）minimap：俯视缩略 + 单位点。
- [x] `docs/projects/nextra/`（可选）写一篇"NextRA 开发者指南"，并把状态从"草案"推进。

**验收**：`gnb run NextRA` 可完整演示一局并实时看到 tick/hash/order；`gnb shot --target NextRA --ui` 截图含 HUD。

---

## 3. 文件清单（汇总，详见设计 §9.1）

```
src/Application/Game/NextRA/
  NextRAGameInstance.{hpp,cpp}  NextRAConfig.hpp
  Sim/   Fixed.h WMath.h SimWorld.{h,cpp} SimComponents.h
         Systems/*  PathfindGrid.{h,cpp} SimRandom.h SyncHash.{h,cpp}
  Net/   Order.h OrderManager.{h,cpp} INetTransport.h LoopbackTransport.{h,cpp} Replay.{h,cpp}
  Render/ RenderProxySystem.{h,cpp} RtsCamera.{h,cpp} NextRAHud.{h,cpp}
```

CMake：`add_executable(NextRA …)` + 链接 `gkNextEngine`（+ 视情况 `NextGameplay`）；`Sim/` 纯逻辑加入 `gkNextUnitTests` 源集（参照 [`src/CMakeLists.txt:691`](../../src/CMakeLists.txt) 的 unit test 链接段）。

---

## 4. 总体 Definition of Done（MVP 完成判据）

1. `./gnb build NextRA` 与 `./gnb build gkNextUnitTests` 均通过。
2. 单机可玩：框选 / 移动 / 攻击移动 / 战斗死亡 / 造兵 / 摧毁敌基地获胜（M0–M4）。
3. 帧同步内核成立：确定性双跑、loopback（含延迟 / 丢包）、replay 三项 hash 一致（M5 D1–D3，进回归测试）。
4. 全部资产为 proc 几何；sim 内零 float；输入只产 order（红线全部守住）。
5. 文档：设计 + 本计划随实现更新状态；源码引用以 `文件:行号` 维护。

---

## 5. MVP 之后（明确不在本计划内）

- **真网络 transport**：`SocketTransport`（UDP/ENet）实现 `INetTransport`，sim/OrderManager 零改动；NAT / 大厅 / 专用 relay server。
- **断线重连 / 追帧（catch-up）/ 时间同步**：慢 peer 处理、加速重放追上。
- **跨平台位级确定性认证**：Windows/Linux/macOS 同 order log 同 hash。
- **资源经济**：矿 / 采集单位 / 精炼 / 造价消耗。
- **玩法扩展**：更多兵种与克制、阵营特性、科技树、建造依赖、迷雾战争、墙 / 防御塔。
- **数据驱动**：`NextRAConfig` → yaml/JSON rules 加载器（对齐 OpenRA MiniYaml）。
- **AI**：真正的 RTS bot（仍走 order 通道以保确定性）。
- **资产替换**：proc 几何 → 模型 / 骨骼动画 / 特效。

---

*实现遵循设计文档的约束（[`docs/designs/nextra-rts-mvp-design.md`](../designs/nextra-rts-mvp-design.md) §4.2 不变量 + §10 风险）。每里程碑完成后更新本文勾选与状态图例。*
