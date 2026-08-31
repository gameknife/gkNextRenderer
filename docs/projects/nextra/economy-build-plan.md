---
title: "NextRA —— 经济与建造（红警基础 Playable）开发计划"
category: plan
status: 待实现
owner: engine
created: 2026-07-18
last_updated: 2026-07-18
supersedes_iteration: combat-depth
---

# NextRA —— 经济与建造（开发计划）

> 状态：**📝 待实现**。本文是交付给后续 AI agent / 开发者的**分阶段开发计划**，与架构设计配套：**先读 [`economy-build-design.md`](economy-build-design.md)**（目标架构 + 约束 + 风险），本文只讲**落地顺序、每阶段任务 / 交付物 / 验收 / 验证命令**。
>
> **前置必读**：[`economy-build-design.md`](economy-build-design.md)（本轮设计）、
> [`architecture.md`](architecture.md)（确定性 sim/order 不变量）、[`AGENTS.md`](../../../AGENTS.md)（构建/测试纪律）。
>
> **迭代北极星**（design §1.3）：一局"开局仅建造场 → 采矿 → 建造 → 出兵 → 打垮走同一经济的 AI"的完整红警基础对局，全程确定性。**不碰**真联机。

---

## 0. 阅读与执行约定

- **每个里程碑独立可验收、独立可构建**：完成即 `./gnb build NextRA` 通过 + 对应验收达成 + （若动到可单测逻辑）`gkNextUnitTests` 绿。
- **构建纪律**（`AGENTS.md`）：改 NextRA 只构建 `NextRA`；动到入测纯逻辑再加 `gkNextUnitTests`；**不要无脑全量 `gnb build`**。
- **确定性是第一公民**：凡碰 `Sim/` 的改动，提交前跑"确定性双跑"；**E1 / E2 / E5 三个里程碑硬性必过**（design §10.3）。
- **sim 权威原则**：所有资金/前置/放置校验都在 OrderApply 端做，HUD gating 只是显示层（R-ECO4）。实现任何按钮前先把 sim 校验写好。
- **关键纪律红线**（违反即返工，详见 design §10.2）：经济状态全整数进 SyncHash（R-ECO1）；屏幕射线只做预览、order 只带整数 cell（R-ECO2）；矿车目标选择确定性 tie-break（R-ECO3）；低电降效用全局奇偶不留 per-actor 残留（R-ECO5）。

---

## 1. 里程碑总览

| 里程碑 | 主题 | 产出 | 验收一句话 | 依赖 | 风险 |
| --- | --- | --- | --- | --- | --- |
| **E0** | 玩家经济状态 + 造价扣费 | `FPlayerState`、`UnitDef.cost`、Produce 资金校验、HUD 资金显示；顺手消化 T3/T9 | 没钱造不了兵，取消退款，资金进 hash | — | 低 |
| **E1** | 矿藏 + 矿车 + 精炼厂 | `FResourceGrid`、`FHarvester` 状态机、精炼厂（暂预置）、附赠矿车 | 挂机资金随采矿增长、矿脉枯竭、打死矿车经济停摆；**双跑硬性** | E0 | **高**（状态机确定性） |
| **E2** | 建造场侧边栏 + 地图放置 | `PlaceBuilding/CancelProduce` order、`CanPlaceBuilding`、ghost 预览、就绪槽 | 从建造场造出精炼厂/兵营/炮塔/围墙并落位；非法位置拒绝；**双跑硬性** | E0 | 中 |
| **E3** | 电力系统 | powerPlant def、PowerSystem、低电降效（生产减速 + 炮塔离线）、电力条 HUD | 拆掉电厂 → 生产变慢 + 炮塔哑火 | E2 | 低 |
| **E4** | 战车厂 + 多队列 + 科技前置 | warFactory def、producedBy 路由、FProduction 多槽队列、前置表、侧边栏三栏 + SetRally | 兵营/战车厂并行出兵；前置未满足置灰且 sim 拒绝 | E2 | 中 |
| **E5** | AI 经济适配 | AI 脚本 build order + 放置 + 运营循环 + 攻击波，全走 order 通道 | 不干预下 AI 自建完整基地并周期进攻；打其经济有真实效果；**双跑硬性** | E1–E4 | 中 |
| **E6** | 战斗表现补强（**P1 可裁剪**） | AttackEvent 通道 + tracer/炮口闪光/死亡反馈（纯表现层） | 交战一眼看清谁在打谁；hash 零影响 | E0 | 低 |
| **E7** | 平衡 + 开局收尾 + 回归 | 数值平衡、开局仅建造场、对称矿区、审视 T8、README/roadmap 更新、全量回归 | §4 DoD 全达成，完整对局跑通 | E0–E5 | 低 |

> 依赖链主干 E0→E1→E2→E3→E4→E5→E7；**E6 独立**（E0 后随时可做/可裁剪）。E1 与 E2 可在 E0 后**并行**（E1 先用预置精炼厂，不等建造系统）。**E1 是本轮最高风险里程碑**（矿车状态机是新增最大 sim 子系统，desync 高发区）。

---

## 2. 里程碑详情

### E0 — 玩家经济状态 + 造价扣费（地基）

**目标**：sim 内建立 per-player 资金状态与"下单扣款、取消退款"语义，为一切后续里程碑供电。顺手消化 T3（`actors_` 只增不减）与 T9（hp 无 clamp）。

任务：
- [ ] `SimWorld` 新增 `std::array<FPlayerState, 2> players_`（design §2.1）+ `Credits(playerId)` 访问器；初始资金 `startingCredits=2500`（Config 常量）。
- [ ] `NextRAConfig.hpp`：`FUnitDef` 加 `cost` 列，现有 7 条目补值（design §9 基线表；base 造价 0 = 不可生产）。
- [ ] `OrderApplySystem`：`Produce` 校验 `credits >= cost` → 扣款入队；不足静默丢弃（R-ECO4）。
- [ ] 新增 `CancelProduce` order（`Order.h` 枚举 + 序列化 + OrderApply）：移除队列项全额退款。（本里程碑队列仍是单槽，E4 升级多槽后语义不变。）
- [ ] `SyncHash.cpp`：actor 循环后按 playerId 升序滚入 credits/power 字段（R-ECO1）。
- [ ] **T3**：`DeathSystem` 销毁 entity 时同步从 `actors_` 中移除（`std::erase`，保持升序不变）；确认无系统依赖"死 id 残留"。
- [ ] **T9**：CombatSystem 扣血后 `hp = std::max(hp, 0)`。
- [ ] HUD：顶部资金显示；生产按钮资金不足置灰（显示层）。
- [ ] `Test_NextRAFixed.cpp`：扣款/拒绝/退款单测；`actors_` compact 后 SyncHash 稳定单测。

**验收**：
1. `./gnb build NextRA gkNextUnitTests` 通过，新旧单测全绿。
2. `gnb shot --target NextRA --ui`：资金显示正确；连造扣款到不足时按钮置灰、sim 拒绝。
3. 确定性双跑通过（T3/T9 改动会改变 hash 值序列，但双侧一致即可）。

**风险**：低。注意 T3 移除 `actors_` 条目后，所有"按 actors_ 遍历"的系统仍按 actorId 升序（顺序性质不变）。

---

### E1 — 矿藏 + 矿车 + 精炼厂（资源闭环）⚠️ 高风险

**目标**：跑通"矿区 → 矿车自动采矿 → 回精炼厂 → 资金增长 → 矿脉枯竭"的经济闭环。本里程碑精炼厂先**开局预置**（建造流在 E2），矿车由精炼厂附赠。

任务：
- [ ] `Sim/ResourceGrid.{h,cpp}`：`AmountAt/Extract`（design §3.1），全整数；入 `gkNextUnitTests` 源集。
- [ ] 地图初始化：双方基地附近各铺一块对称矿区（~30 cell × 600/cell，Config 常量）。
- [ ] `NextRAConfig.hpp`：`refineryTypeId=9`（2×3 footprint 含卸货格）+ `harvesterTypeId=11`（Light 装甲、无武装、`harvester=true`）定义。
- [ ] `SimComponents.h`：新增 `FHarvester`（design §3.2）。
- [ ] `SimWorld` 新增 `HarvesterSystem`（插入 Step 固定顺序：ProductionSystem 后、MovementSystem 前）：Idle→找矿（Chebyshev 距离→cell index 升序 tie-break，R-ECO3）→采集（20 tick 一档 50）→满载 500→回厂（距离→actorId tie-break）→卸货 30 tick→credits 入账→循环；矿采空就地重找；无矿/无厂则 Idle 重试。
- [ ] 玩家 Move/AttackMove 打断矿车自动状态（order 优先，置 Idle）；空闲后自动恢复找矿。
- [ ] 精炼厂放置/预置完成即 `SpawnMobile(harvester)` 附赠矿车（卸货格旁）。
- [ ] `SyncHash.cpp`：覆盖矿量 grid（非零 cell 按 index 升序）+ `FHarvester` 全字段（R-ECO1）。
- [ ] 渲染：矿 cell 金色扁平 box，按矿量档位缩放；采空移除。矿车几何（车斗 box 组合，载量满时车斗变色）。
- [ ] `Test_NextRAFixed.cpp`：找矿 tie-break 确定性、Extract 边界（不足一档/采空）、状态机打断恢复、满载回厂选择单测。

**验收**：
1. `./gnb build NextRA gkNextUnitTests` 通过，单测绿。
2. `gnb shot --target NextRA --frames 1200`：矿车在矿区↔精炼厂往返，资金持续增长；矿区肉眼可见变薄。
3. 打死矿车 → 资金停止增长；矿采空 → 矿车 Idle。
4. **双跑硬性**：含"矿车采矿中 + 被攻击打断"的 order log，逐 tick hash 完全一致。**此项不过则 E1 不算完成**。

**风险**：**高**。矿车状态机是本轮最大新增 sim 子系统：找矿/找厂 tie-break、打断时机、卸货计时都是 desync 源。缓解：全部决策规则整数化写死（design §3.2）；双跑失败时 dump 首个分歧 tick 的 harvester 状态 diff。

---

### E2 — 建造场侧边栏 + 地图放置（建造闭环）

**目标**：红警灵魂交互——侧边栏点建筑 → 建造场计时 → 就绪 → 地图 ghost 预览落位。E1 的预置精炼厂改为可建造。

任务：
- [ ] `Order.h/.cpp`：新增 `PlaceBuilding`（targetPos=cell 中心 WPos + produceTypeId）与 `SetRally`；序列化扩展。
- [ ] `FProduction` 增加 `readyBuildingTypeId` 就绪槽：建筑类型完成不 spawn、入就绪槽；就绪槽被占则队首暂停（design §4.1）。
- [ ] `Sim/Placement.{h,cpp}`：`CanPlaceBuilding(world, playerId, typeId, cell)` 纯函数（界内 / 无阻挡 / 无单位 / 无矿 / 建造半径 ≤6，design §4.3）；入 `gkNextUnitTests` 源集。
- [ ] `OrderApplySystem`：`PlaceBuilding` = 就绪校验 + `CanPlaceBuilding` → `SpawnBuilding` + footprint 写 grid + 清就绪槽；失败静默丢弃（就绪保留）。`CancelProduce` 支持取消就绪建筑（退款）。
- [ ] HUD 放置模式：就绪按钮点击进入 → `GetScreenToWorldRay` 地面命中换 cell（[`NextRAGameInstance.cpp:748`](../../../src/Application/Game/NextRA/NextRAGameInstance.cpp) 复用）→ footprint ghost（`CanPlaceBuilding` 本地预判绿/红）→ 左键发 order、右键/Esc 退出（R-ECO2：射线只做预览）。
- [ ] 可建造清单接线：refinery / barracks / turret / wall 走建造流（powerPlant/warFactory 定义在 E3/E4 加入后自动进清单）；E1 的预置精炼厂改为开局仅建造场 + 教学性的初始矿车可留待 E7 统一定开局。
- [ ] `SyncHash.cpp`：覆盖 `readyBuildingTypeId`。
- [ ] `Test_NextRAFixed.cpp`：`CanPlaceBuilding` 全分支（越界/压建筑/压单位/压矿/超半径/合法）、就绪槽暂停语义、放置后 grid 阻挡生效单测。

**验收**：
1. `./gnb build NextRA gkNextUnitTests` 通过，单测绿。
2. `gnb shot --target NextRA --ui`：侧边栏下单 → 进度走完 → Ready → 放置预览绿/红正确 → 落位成建筑且单位绕行。
3. 非法落位（压单位/超半径）order 被 sim 丢弃，就绪保留可重放。
4. **双跑硬性**：含 PlaceBuilding/CancelProduce 的 order log 双跑 hash 一致。

**风险**：中。ghost 预览与 sim 校验共用 `CanPlaceBuilding` 是防"预览绿但 sim 拒"漂移的关键；放置瞬间 footprint 下有单位走过的竞态由 sim 校验兜底（拒绝后玩家重点）。

---

### E3 — 电力系统

**目标**：电厂供电、建筑耗电，低电生产减速 + 炮塔离线。

任务：
- [ ] `NextRAConfig.hpp`：`powerPlantTypeId=8`（2×2、+100）定义；`FUnitDef` 加 `powerDelta` 列并给现有建筑补值（design §9）。
- [ ] `SimWorld` 新增 `PowerSystem`（ProductionSystem 前，每 tick 按 actorId 升序全量重算 produced/consumed，design §5）。
- [ ] 低电降效：ProductionSystem 低电玩家进度隔 tick 推进（`tick % 2` 全局奇偶，R-ECO5）；TargetingSystem/CombatSystem 跳过低电玩家的建筑攻击者并清 target。
- [ ] HUD：电力条（produced/consumed）+ 低电红字警告；侧边栏低电时进度条视觉减速提示。
- [ ] `Test_NextRAFixed.cpp`：power 汇总、低电判定、隔 tick 推进语义单测。

**验收**：
1. `./gnb build NextRA gkNextUnitTests` 通过，单测绿。
2. `gnb shot --target NextRA --ui`：造电厂前低电警告；拆/炸电厂 → 生产肉眼变慢、己方炮塔停火；补电厂恢复。
3. 双跑通过。

**风险**：低。注意 powerProduced/Consumed 每 tick 全量重算（无增量状态），建筑死亡自然生效。

---

### E4 — 战车厂 + 多队列生产 + 科技前置

**目标**：双生产线（兵营=步兵、战车厂=载具）、多槽队列 + 取消、前置解锁、侧边栏三栏成型。

任务：
- [ ] `NextRAConfig.hpp`：`warFactoryTypeId=10`（3×3）定义；`FUnitDef` 加 `producedBy` + `prerequisiteTypeId` 列并全表补值（design §6.1/§6.3）；坦克/矿车归 Vehicle、步兵/火箭兵归 Infantry。
- [ ] `FProduction.queue` 升级 `std::vector<uint16_t>`（上限 5）：队首完成出货/入就绪槽后推进下一项；`CancelProduce` 指定项退款（E0 语义延续）。
- [ ] `OrderApplySystem`：`Produce` 增加 producedBy 路由校验（下给正确类别的建筑）+ 前置校验（存活前置建筑存在，R-ECO4）。
- [ ] `SetRally` order 接通：选中生产建筑右键地面 = 设 rally（HUD 画 rally 旗标）。
- [ ] HUD 侧边栏三栏（建筑/步兵/载具）：按钮显示造价 + 队列进度 + 排队数角标；前置未满足置灰 + tooltip 缺什么；无对应生产建筑时整栏置灰。默认路由到 actorId 最小的存活生产建筑。
- [ ] `SyncHash.cpp`：覆盖 queue 内容。
- [ ] `Test_NextRAFixed.cpp`：多槽入队/完成推进/取消中间项退款、前置校验、producedBy 路由拒绝单测。

**验收**：
1. `./gnb build NextRA gkNextUnitTests` 通过，单测绿。
2. `gnb shot --target NextRA --ui`：兵营连排 5 步兵 + 战车厂同时出坦克并行不干扰；取消队列项退款；未造精炼厂时战车厂按钮置灰且强发 order 被 sim 拒。
3. 双跑通过。

**风险**：中。队列升级动 `FProduction` 结构，SyncHash/序列化同步更新；就绪槽 + 队列暂停的组合语义（建造场队首是建筑且就绪槽满 → 暂停）需单测钉死。

---

### E5 — AI 经济适配（完整对抗）⚠️ 双跑硬性

**目标**：AI 走与玩家完全相同的经济/建造/前置规则，形成"§1.3 玩家旅程"的完整对手盘。

任务：
- [ ] AI 脚本状态机（GameInstance 侧，产出 order 经 `injectedAIOrders_` 注入）：build order = 电厂→精炼厂→兵营→电厂→战车厂→炮塔×2，逐步等"资金够 + 前置满足"才下单（design §7）。
- [ ] AI 放置：预 author 相对建造场偏移表 + 被占时确定性螺旋扫描兜底（复用 `CanPlaceBuilding`）。
- [ ] AI 运营：矿车阵亡且有战车厂 → 补造；资金 > 阈值 → 2 步兵:1 火箭兵:1 坦克配比出兵；沿用现有攻击波节奏（AttackMove 玩家基地），间隔提至 ~90s。
- [ ] AI 决策只读 sim 公开状态、零随机（或 `FSimRandom` 显式种子，顺手消化 T5 可选）。
- [ ] 移除旧"免费造兵"AI 路径（经济上线后已失效）。
- [ ] 开局布局切换：双方 = 建造场 + 初始资金（旧预置兵营/单位撤下；E7 终调）。

**验收**：
1. `./gnb build NextRA` 通过。
2. `gnb shot --target NextRA --frames 4000`（或 validate 脚本）：不干预下 AI 依次建出电厂/精炼厂/兵营/战车厂/炮塔，矿车运转，周期攻击波来袭。
3. 玩家打掉 AI 矿车/精炼厂 → AI 出兵明显放缓（经济博弈成立）。
4. **双跑硬性**：整局（含 AI 全部 order）双 World 逐 tick hash 一致。
5. 可玩性 sanity：正常运营下玩家能在 10–15 分钟内打出胜负。

**风险**：中。AI 下单时序依赖 sim 状态读取，注意读的是"上一 tick 已定格"状态且注入 order 走既有 delay 通道（现有机制已如此）；螺旋扫描必须与 sim 校验同函数，避免 AI 反复发无效 order 死循环（连续 N 次失败则跳过该建筑）。

---

### E6 — 战斗表现补强（P1，可裁剪，纯表现层）

**目标**：交战可读性——弹道 tracer、炮口闪光、死亡反馈。**sim 零改动语义**（即时命中不变）。

任务：
- [ ] `SimWorld`：CombatSystem 命中时积累 `FAttackEvent{attacker,target,weapon}`；`ConsumeAttackEvents()`（`ConsumeDestroyedRenderNodeIds` 同模式）。
- [ ] GameInstance/渲染：按事件生成 tracer（细长发光 box 炮口→目标 100–150ms）、炮口 scale 脉冲、死亡缩没反馈；武器类型映射颜色（design §8）。
- [ ] 特效节点池化复用（避免每发 spawn/destroy 节点抖动）。

**验收**：
1. `./gnb build NextRA` 通过。
2. `gnb shot --target NextRA`：混战中弹道连线清晰可读、死亡有反馈。
3. 双跑通过且 SyncHash 值与 E5 末完全相同（事件表不入 hash、不影响 sim 状态——回归钉死"纯表现"承诺）。

**风险**：低。唯一红线：事件消费在渲染帧，不得反向写 sim。

---

### E7 — 平衡 + 开局收尾 + 回归（DoD 收口）

**目标**：把系统拼成"好玩的一局"，收全部验收。

任务：
- [ ] 数值平衡 pass：造价/建造时长/矿量/采集速率/电力值按整局节奏调（目标：首坦克 ~3 分钟、经济压制可翻盘、10–15 分钟分胜负）；**审视 T8**（单 tick 双 combat pass 的隐含 DPS 翻倍——决定保留（改数值兼容）或去除（统一单 pass），写入 journal）。
- [ ] 开局配置终稿：双方建造场 + 2500 资金（可选常量：附赠 2 步兵护卫）；矿区布局对称性复核。
- [ ] 胜负条件复核：沿用基地判负；"全建筑清除判负"留常量开关（默认关）。
- [ ] 地图尺寸评估：48×48 若拥挤则常量升 64×64（A\* 与 grid 均按常量伸缩，改后跑全部单测 + 双跑）。
- [ ] 文档收口：更新 [`architecture.md`](architecture.md) 与本 design/plan 的状态、操作说明和技术债结论；删除实施期文件行号快照。
- [ ] 全量回归：`gkNextUnitTests` 全绿 + 复合场景双跑（采矿+建造+低电+AI 全程）+ `gnb shot` 系列截图 + 一局人机完整对局跑通。

**验收**：即 §4 总体 DoD 全项达成。

**风险**：低。平衡是主观项，以"节奏目标 + 完整对局可跑通"为验收基线，不追求竞技级平衡。

---

## 3. 文件清单（汇总，详见 design §11）

```
src/Application/Game/NextRA/
  NextRAConfig.hpp       [改] +cost/powerDelta/producedBy/prerequisite/harvester 列；
                              +powerPlant/refinery/warFactory/harvester 定义；经济常量
  Net/Order.{h,cpp}      [改] +PlaceBuilding / CancelProduce / SetRally
  Sim/
    SimComponents.h      [改] +FHarvester；FProduction 多槽队列 + readyBuildingTypeId
    SimWorld.{h,cpp}     [改] +players_ / PowerSystem / HarvesterSystem / 就绪与放置 / 低电 /
                              附赠矿车 / AttackEvent / T3 compact / T9 clamp
    ResourceGrid.{h,cpp} [新] 矿藏格子
    Placement.{h,cpp}    [新] CanPlaceBuilding 纯函数
    SyncHash.cpp         [改] 覆盖 players_ / 矿量 / FHarvester / FProduction 队列 / 就绪槽
    Systems/OrderApplySystem.cpp [改] 新 order + 资金/前置/放置校验
  Render/RenderProxySystem.{h,cpp} [改] 矿区档位 / tracer / 死亡反馈
  NextRAGameInstance.{hpp,cpp}     [改] 侧边栏三栏 HUD / 放置模式 / AI 经济脚本 / 开局布局 / 矿区几何
src/Tests/Test_NextRAFixed.cpp     [改] 经济/矿车/放置/前置/低电/队列单测
CMake（NextRA + 测试源集）          [改] ResourceGrid/Placement 入 gkNextUnitTests
```

---

## 4. 总体 Definition of Done（本轮完成判据）

1. `./gnb build NextRA` 与 `./gnb build gkNextUnitTests` 均通过；新旧单测全绿。
2. **红警基础 Playable 旅程跑通**（design §1.3）：开局仅建造场 → 建电厂/精炼厂/兵营/战车厂 → 矿车经济运转 → 资金出兵 + 炮塔围墙防线 → 低电有后果 → AI 走同一循环并进攻 → 摧毁对方基地判胜；一局 10–15 分钟。
3. **经济博弈成立**：打掉对方矿车/精炼厂能显著拖垮其出兵速度。
4. **确定性不破**：E1/E2/E5 双跑硬性全过；复合场景（采矿+建造+低电+AI 整局）逐 tick hash 一致。
5. **红线全守住**：R-ECO1..5 + 继承的 MVP/D1 全部红线；`Sim/` 内 grep 无新增 float。
6. 技术债：T3、T9 消化完成；T5、T8 在架构文档中明确归属或关闭。
7. 文档：design/plan 与 `architecture.md` 状态更新，README 补齐可玩说明。

---

## 5. 本轮之后（明确不在本轮内）

- **D4 联机激活**：GameInstance 接 `LoopbackTransport`、playerCount=2 真双 World lockstep、Replay 接通、序列化去 memcpy——经济系统上线后 order 种类变多，**建议下一轮优先做 D4**，趁玩法还没继续膨胀先把"帧同步"名实相符。
- **D3 操作手感**：编队 Ctrl+数字、Stop/Hold/Patrol、快捷键、Shift 路径点。
- **D5 真策略 AI**：侦察/骚扰/回防/兵种应对（本轮脚本 AI 之上迭代）。
- **D6 表现进阶**：迷雾视野、sim 层投射物与溅射、美术模型替换 proc box、音效、可点击小地图。
- **经济进阶**：出售/维修建筑、矿石再生、宝石、多建造场加速、MCV。
- **数据驱动加载器**：Config 表 → yaml/JSON 运行时加载。

---

*实现遵循 design 文档约束（[`economy-build-design.md`](economy-build-design.md) §10 红线）。每里程碑完成后更新本文与 `architecture.md` 的状态。*
