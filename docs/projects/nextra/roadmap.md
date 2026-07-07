---
title: "NextRA —— 迭代路线图与方向库（现状 / 红警对照 / 候选方向 / 技术债）"
category: roadmap
status: 活跃
owner: engine
created: 2026-06-29
last_updated: 2026-06-29
---

# NextRA —— 迭代路线图与方向库

> 本文是 NextRA 的**迭代决策参考库**，不是单次迭代的设计或计划。用途：每次启动新一轮迭代前，先读本文——它汇总了 **MVP 现状背景、红警核心玩法对照、所有候选方向及其取舍、共性技术债登记**，帮助需求方和接手 agent 选定下一轮方向。
>
> 单次迭代的设计 / 计划文档另起（见 §4 已有文档索引），不堆在本文里。本文只讲"**现在在哪、能往哪走、各方向的代价与价值、先还哪些债**"。
>
> **前置**：[`docs/designs/nextra-rts-mvp-design.md`](../designs/nextra-rts-mvp-design.md)、[`docs/plans/nextra-rts-mvp-plan.md`](../plans/nextra-rts-mvp-plan.md)（MVP 设计与计划，已交付）、[`docs/projects/nextra/README.md`](../projects/nextra/README.md)（开发者运行指南）。

---

## 1. 一句话定位

NextRA 是用引擎现成 `NextGameInstanceBase` + proc 几何搭的 **OpenRA / 红警风格确定性帧同步 RTS 原型**。目标玩家体验：俯视相机下框选单位、下达移动 / 攻击命令，单位在定点整数世界里寻路、交战、阵亡，造兵建筑产出新单位，摧毁敌方基地即胜——**所有玩法状态变更只通过 order 驱动的确定性 sim**，从第一天就为多人帧同步打地基。

MVP 已交付"能打的骨架"；本文负责回答"**下一步往哪推进最有价值、代价最小**"。

---

## 2. 现状背景：MVP 交付了什么

M0–M6 已落地一个**单机可玩 + 确定性骨架**的 RTS。逐系统状态（接手 agent 必读，决定后续每步建立在什么之上）：

### 2.1 真能玩的部分

| 系统 | 状态 | 关键位置 |
| --- | --- | --- |
| 定点数学 | ✅ 真实现，质量好 | Q16.16 `FFixed`（[`Fixed.h`](../../src/Application/Game/NextRA/Sim/Fixed.h)），`Sqrt` 整数二分，sin/cos 编译期查表（[`WMath.h:54-82`](../../src/Application/Game/NextRA/Sim/WMath.h)） |
| 固定 tick sim | ✅ | `SIM_HZ=20` 累加器 + 限速追帧（[`NextRAConfig.hpp:11`](../../src/Application/Game/NextRA/NextRAConfig.hpp)） |
| 整数格子 A\* | ✅ 确定性 tie-break | [`PathfindGrid.cpp:72-90`](../../src/Application/Game/NextRA/Sim/PathfindGrid.cpp)，4 邻接，48×48 |
| 战斗闭环 | ✅ | 索敌（O(n²) 最近敌人）→ 射程停车 → 冷却扣血 → 死亡销毁（[`SimWorld.cpp:332-467`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)），即时命中无弹道 |
| 生产闭环 | ✅ | 兵营单队列造步兵(55tick)/坦克(90tick)，rally 点 spawn（[`SimWorld.cpp:292-330`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)） |
| 胜负判定 | ✅ | 基地被毁 → 定胜负（[`SimWorld.cpp:457-464`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)） |
| SyncHash | ✅ 真实现 | FNV-1a，按 actorId 排序覆盖全状态（[`SyncHash.cpp:32-91`](../../src/Application/Game/NextRA/Sim/SyncHash.cpp)） |
| 选择 / 框选 | ✅ | 左键单击 / 拖框(>8px) / Shift 加选（[`NextRAGameInstance.cpp`](../../src/Application/Game/NextRA/NextRAGameInstance.cpp)） |
| 指令 | ✅ 4 种 | Move / AttackMove(Shift) / Attack / Produce |
| 俯视相机 | ✅ 基本 | WASD 平移 + 滚轮缩放，无旋转 / 无边界（[`RtsCamera.cpp`](../../src/Application/Game/NextRA/Render/RtsCamera.cpp)） |
| 渲染插值 | ✅ | sim↔render 位置 lerp，1cell=1单位（[`RenderProxySystem.cpp:26-58`](../../src/Application/Game/NextRA/Render/RenderProxySystem.cpp)） |
| HUD | ✅ | 选圈 / 血条 / 框选框 / 生产按钮 / 调试网格 / 小地图 / 胜负横幅 / order log |
| AI 占位 | ✅ 简陋 | player 1 每 120tick 造步兵、每 180tick AttackMove 玩家基地，无策略 |

### 2.2 骨架 / 死代码 / 占位（关键认知，影响迭代选型）

- **整个真实网络栈是死代码**：`LoopbackTransport` / `Replay` / `INetTransport` 零调用方。`OrderManager` 实际配 **playerCount=1**，gate 只等 player 0 自己；AI 的 order 通过 `injectedAIOrders_` **本地直接注入伪装成 player 1**，不走网络。所以"帧同步"目前是**名义上有、实际未验证**。
- **`SimRandom.NextU32()` 从未被调用**：确定性是"因为没有随机"而非"用确定性随机"，SyncHash 里的 RandomState 恒定。
- **单位永不转向**：`FSimTransform.facing/prevFacing` 字段存在但从未读写；渲染只 `SetTranslation` 不 `SetRotation`。坦克只是个大 box。
- **无碰撞 / 占位**：`FPathfindGrid` 只有静态阻挡（且全图只有 1 个演示阻挡格 `(0,0)`），单位/建筑不写入 grid，单位互相穿透、穿建筑。
- **配置硬编码**：`NextRAConfig.hpp` 是分散 `constexpr` 函数（按 typeId if-else），加兵种要改 Config + SimWorld + GameInstance + Render 多处。

> **结论**：MVP 是一个"玩法瘦、网络假、但确定性地基扎实"的原型。后续任何方向都能稳健地往上盖。

---

## 3. 红警核心玩法对照表（迭代目标的标尺）

这张表是判定"还差多远、该补什么"的标尺。✅ = 已具备，⚠️ = 有骨架/占位，❌ = 缺。

| 红警核心玩法 | 当前状态 | 缺口 / 备注 | 相关候选方向 |
| --- | --- | --- | --- |
| 建造场 + 侧边栏建造（选建筑→计时→地图放置） | ❌ 无 | **最大缺口**，红警灵魂 | D2 经济+建造 |
| 资源经济（矿车采矿→精炼厂→资金） | ❌ 无 | 生产免费，无资源概念 | D2 经济+建造 |
| 电力系统 | ❌ 无 | | D2 经济+建造 |
| 多生产线（兵营 / 战车厂 / 机场） | ⚠️ 仅兵营 | | D2 经济+建造 |
| 科技树 / 建筑前置 | ❌ 无 | | D2 经济+建造 |
| 防御建筑 / 炮塔 / 围墙 | ❌ 无 | | **D1 战斗深度（已规划）** |
| 兵种克制（rock-paper-scissors） | ⚠️ 步兵/坦克但无克制 | | **D1 战斗深度（已规划）** |
| 单位碰撞 / 动态避让 | ❌ 穿透、穿建筑 | | **D1 战斗深度（已规划）** |
| 朝向 / 炮塔 / 动画 | ❌ 无 | facing 字段闲置 | **D1 战斗深度（已规划）** |
| 编队 / Stop / Hold / 巡逻 | ❌ 无 | 仅 Move/Attack/AttackMove/Produce | D3 操作手感 |
| 迷雾 / 视野 | ❌ 无 | | D6+ 进阶玩法 |
| 多人联机 / 回放 | ⚠️ 骨架死代码 | transport/replay 零调用 | D4 联机激活 |
| 真实 AI | ⚠️ 脚本占位 | 只造步兵、无策略 | D5 AI |

---

## 4. 候选方向库

每个方向给：**范围 / 价值 / 依赖 / 风险 / 工作量**，供选型权衡。方向编号 D1–D6，按"玩法收益 / 技术债缓解"排序，非强制顺序——可按需求方优先级挑选。

### D1 — 战斗与单位深度 ⬅️ **已选定为当前迭代**

- **范围**：朝向 + 炮塔追踪、装甲/武器类型表克制、整数占位 + 软推离碰撞、防御炮塔 + 围墙、Config 表化。
- **价值**：让战斗"有看头、像红警"，消化 4 个红警核心缺口（朝向/克制/碰撞/防御）+ 1 个共性债（配置硬编码）。
- **依赖**：无（直接建立在 MVP 之上）。
- **风险**：软推离是 desync 高发区（整数符号方向 + actorId 全序 + SyncHash 覆盖）。
- **工作量**：大（6 个里程碑）。
- **状态**：✅ 已出设计 + 计划，见 [`combat-depth-design.md`](combat-depth-design.md) / [`combat-depth-plan.md`](combat-depth-plan.md)。

### D2 — 经济 + 建造系统（红警灵魂）

- **范围**：矿区 / 矿车采矿 → 精炼厂 → 资金；电力系统；建造场侧边栏选建筑 → 计时 → **地图放置**；战车工厂多生产线；简单科技前置（建造解锁）。
- **价值**：补**最大缺口**（§3 第一行），把"免费随便造"变成真 RTS 经济循环，是红警与"格斗游戏 + 造兵按钮"的本质区别。完成后游戏才真正"像红警"。
- **依赖**：建议 D1 的 **C0 数据驱动化**（Config 表化）先做（D1 已规划含此），否则建筑/单位定义改动会爆炸。建造放置交互需要"屏幕→世界地面命中→预览→落位"，引擎 `RayCastGPU`（[`Engine.hpp:156`](../../src/Engine/Runtime/Engine.hpp)）可复用（MVP 已用它做右键移动）。
- **风险**：
  - 经济状态（资金/电力）必须进 SyncHash 且 order 驱动（Produce 前要校验资金），否则破坏确定性。
  - 建造放置是 order（选位置→下建筑），落位后 footprint 写 grid（D1 的占位机制可复用）。
  - 矿车 AI 寻路 + 采矿动画是新增 sim 子系统，确定性需小心。
- **工作量**：**最大**（红警最复杂的系统）。建议拆成 D2a 资源经济 + D2b 建造放置两轮。
- **关键决策点**（接手时需与需求方确认）：矿车是否可被攻击 / 资源是否有限（矿脉枯竭）/ 电力是硬上限还是降效。

### D3 — 操作手感

- **范围**：编队 Ctrl+数字、Stop / Hold Position / Patrol、生产 / 建造快捷键、取消生产队列、攻击-移动-队列（Shift 连点设路径点）。
- **价值**：提升 RTS **操作密度**，让老玩家舒服。单独价值有限，但几乎零风险、工作量小，适合作为"小迭代"穿插在大迭代之间。
- **依赖**：Stop/Patrol 需新增 order 类型（`EOrderType` 加 `Stop/Patrol`，[`Order.h:13-19`](../../src/Application/Game/NextRA/Net/Order.h)）+ SimWorld 对应处理；Patrol 复用 MVP 遗留的 `FMobile.pingPong/pointA/pointB` 字段（目前闲置，[`SimComponents.h:31-41`](../../src/Application/Game/NextRA/Sim/SimComponents.h)）。编队是纯本地表现态（不入 hash）。
- **风险**：低。新增 order 走现有 lockstep 通道，确定性天然继承。
- **工作量**：小。

### D4 — 激活联机骨架（让"帧同步"名实相符）

- **范围**：把 GameInstance 接到 `LoopbackTransport`、playerCount 升到 2、真双 World lockstep + 心跳、Replay 录制/回放接通 + 回归测试。
- **价值**：**诚实性问题**——MVP 计划 M5 的"帧同步内核"目前是名义交付，transport/replay 是死代码。这一步让确定性 lockstep 真正可验证（双 World 逐 tick hash 比对 + replay 回放），是"RTS 帧同步原型"的技术存在理由。**若未来要做真联机，这是不可跳过的地基。**
- **依赖**：无（纯网络层工作，不动玩法）。
- **风险**：
  - `Order.cpp`/`Replay.cpp` 序列化用 `memcpy POD`（[`Order.cpp:9-26`](../../src/Application/Game/NextRA/Net/Order.cpp)），**跨字节序/跨平台不安全**——loopback 同机够用，真联机会炸，需在 D4 内换成显式字段序列化。
  - 双 World 需要确定性的"两侧输入完全一致"——当前 AI 注入逻辑（`injectedAIOrders_`）要改造为走 transport 的 player 1 order，不能本地直接注入。
- **工作量**：中。主要是接线 + 把"假注入"改成"真投递" + 修序列化 + 写双跑/replay 回归。
- **建议**：若项目目标是"做给人联机玩"，**D4 应优先于 D2**（先验证地基，再往上盖玩法）。

### D5 — 真实 AI

- **范围**：真正的 RTS bot——侦察、经济运营、兵种组合、多线骚扰、回防。**仍必须走 order 通道**以保确定性。
- **价值**：单机可玩性的核心（没人陪练时的对手）。MVP 的 AI 是纯节奏脚本，谈不上"对手"。
- **依赖**：建议 **D2 经济**先做（AI 不懂经济就没法运营）；D1 战斗深度（AI 要会兵种克制）。
- **风险**：中。AI 逻辑本身确定性（只要走 order），但"好 AI"是开放问题，工作量不可控。
- **工作量**：中–大（取决于 AI 强度目标）。建议先做"能打赢 MVP 脚本 AI"的最低版本，再迭代。

### D6 — 进阶玩法（迷雾 / 视野 / 特效 / 美术资产）

- **范围**：战争迷雾 + 视野系统、投射物弹道/溅射/死亡特效、proc 几何替换为美术模型、音效、minimap 可点击导航。
- **价值**：**表现层**打磨，提升观感，但不改变核心循环。
- **依赖**：迷雾/视野需 sim 层记录每个单位的视野范围（确定性，进 hash）+ 渲染层遮罩；特效是纯表现层。
- **风险**：迷雾视野涉及"敌我对信息可见性不对称"，在 lockstep 下要注意"sim 知道一切、表现层按视野过滤"，不能让本地视野过滤影响 sim。
- **工作量**：大（美术资产尤其不可控）。建议放在玩法循环（D1/D2）完整后。

---

## 5. 共性技术债登记（影响所有方向，建议优先消化）

这些债不是某个方向的专属，而是会**拖累任何后续扩展**。标注哪些已被 D1（战斗深度）消化。

| # | 技术债 | 影响 | 状态 / 归属 |
| --- | --- | --- | --- |
| **T1** | 配置硬编码（`NextRAConfig` 分散 constexpr if-else） | 加兵种/建筑改 5+ 处源码，阻碍 D2/D5 | 🔨 **D1-C0 正在消化**（表化为 `FUnitDef[]`） |
| **T2** | 网络栈死代码（LoopbackTransport/Replay/INetTransport 零调用，playerCount=1） | "帧同步"名实不符，阻碍 D4 | ⬜ D4 |
| **T3** | `actors_` 只增不减（[`SimWorld.cpp`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp) 只 push 不 erase，死 actor 靠 `registry.valid` 过滤） | 长局性能单调退化、每系统全表扫描含死 actor | ⬜ 未归属，建议任何碰 Sim 的迭代顺手修（改 `actors_` 为惰性 compact 或用 entt view + id 排序） |
| **T4** | `Order.cpp`/`Replay.cpp` 序列化 `memcpy POD`（[`Order.cpp:9-26`](../../src/Application/Game/NextRA/Net/Order.cpp)） | 跨字节序/跨平台不安全，阻碍真联机 | ⬜ D4 |
| **T5** | `SimRandom.NextU32()` 从未被调用 | 确定性是"无随机"而非"确定性随机"，设施摆设 | ⬜ 留给需要散布/抖动的系统（如命中率、弹道散布） |
| **T6** | SyncHash UI "Peer 0" 重复行（[`NextRAGameInstance.cpp:120-121`](../../src/Application/Game/NextRA/NextRAGameInstance.cpp)） | 误导性"网络校验"假象 | ⬜ D4（接通双 peer 后自然消除） |
| **T7** | `acquireRange` 兵种无关硬编码（[`SimWorld.cpp:34`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp) 永远 `CellDistance(5)`） | 兵种索敌行为无差异 | 🔨 **D1-C0 正在消化**（读 `UnitDef.acquireRange`） |
| **T8** | `Step` 单 tick 跑两次 combat pass（[`SimWorld.cpp:76-83`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)） | 隐含"DPS 翻倍"规则，调参易踩坑 | ⬜ 未归属，建议 D1 调战斗数值时一并审视 |
| **T9** | `CombatSystem` 扣血无 clamp（hp 可为负，[`SimWorld.cpp:432`](../../src/Application/Game/NextRA/Sim/SimWorld.cpp)） | 功能无害但 SyncHash 混入负值，跨实现需一致 | ⬜ D1-C2（克制表）顺手 clamp |
| **T10** | "Net delay/Drop/Reorder" 滑块只影响 AI（与 `LoopbackTransport` 名义功能脱节） | UI 误导 | ⬜ D4 |
| **T11** | 遗留未启用字段（`FMobile.pingPong/pointA/pointB`、`facing`） | 代码噪音 | `facing` → D1-C1 启用；pingPong → D3 Patrol 可复用或删除 |

> **建议**：任何迭代接手时，先扫本表，把"顺手能修且影响本方向"的债纳入该迭代范围（如 D1 顺手消化 T1/T7/T9）。**T3（actors_ 只增不减）** 是性能隐患，建议下一次大改 Sim 时一并处理。

---

## 6. 已有文档索引

| 文档 | 类型 | 状态 |
| --- | --- | --- |
| [`docs/designs/nextra-rts-mvp-design.md`](../designs/nextra-rts-mvp-design.md) | MVP 架构设计 | ✅ 已交付（M0–M6 完成） |
| [`docs/plans/nextra-rts-mvp-plan.md`](../plans/nextra-rts-mvp-plan.md) | MVP 开发计划 | ✅ 已交付 |
| [`docs/projects/nextra/README.md`](../projects/nextra/README.md) | 开发者运行指南 | ✅ 活跃 |
| [`combat-depth-design.md`](combat-depth-design.md) | D1 战斗深度 架构设计 | 📝 待实现 |
| [`combat-depth-plan.md`](combat-depth-plan.md) | D1 战斗深度 开发计划 | 📝 待实现 |
| **本文** | 方向库 + 现状 + 技术债 | 🔄 活跃（每次迭代更新） |

> 后续每选定一个新方向（如 D2 经济），在本文 §4 标注 `⬅️ 已选定`，并新建对应的 `xxx-design.md` + `xxx-plan.md`，再把索引补进本表。

---

*本文随每次迭代更新：选定方向、消化技术债、推进红警对照表状态。它是 NextRA 的"产品 + 技术"总账。*
