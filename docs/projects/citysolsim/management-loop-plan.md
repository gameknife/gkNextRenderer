---
title: "CitySolSim —— 经营循环（观察盒 → 模拟经营 Playable）开发计划"
category: plan
status: 待实现
owner: engine
created: 2026-07-18
last_updated: 2026-07-18
---

# CitySolSim —— 经营循环（开发计划）

> 状态：**📝 待实现**。本文是交付给后续 AI agent / 开发者的**分阶段开发计划**：**先读 [`management-loop-design.md`](management-loop-design.md)**（目标 + 架构 + 红线），本文只讲落地顺序、每阶段任务 / 验收 / 验证命令。
>
> **前置必读**：design 全文（尤其 §2 核心决策与 §9 红线）、[`AGENTS.md`](../../../AGENTS.md)（构建 / 验证纪律）。
>
> **迭代北极星**（design §1.2）：一局 20~30 分钟"观察需求 → 建造 / 升级 → 市民迁入、城市可见变化 → 财政增长解锁更多"的正反馈循环，城市 32 → 96 人。

---

## 0. 阅读与执行约定

- **构建纪律**：改 CitySolSim 只构建自己 —— `./gnb build CitySolSim`（Windows：`./gnb.bat build CitySolSim`）；不碰 Engine/Gameplay 公共层就**不要**全量构建。若动到 `Gameplay/Sim/CharacterPool`（M1 可能），加构 `gkNextUnitTests` 与其它使用方（`gnb build CitySolSim gkNextUnitTests`）。
- **每个里程碑独立可验收**：完成即 build 通过 + 该里程碑验收步骤达成 + **smoke 脚本不回归**（`gnb validate --script assets/agentscripts/citysolsim-smoke.agentscript.json` 全绿，design R-CS5）。
- **肉眼验证首选** `gnb shot --target CitySolSim --ui`（含 HUD 截图，不弹窗自动退出）；断言闭环用 `gnb validate`。
- **红线**（违反即返工，见 design §9）：决策量全整数（R-CS1）；单一固定种子 RNG（R-CS2）；model/material 只在 `BeforeSceneRebuild` 注入（R-CS3）；资金校验单点（R-CS4）。
- **scad 资产坑**：改 `habor_city_v2.scad` 后必须同步拷贝到 `out/build/<preset>/assets/scad/`，否则跑的还是旧城。

---

## 1. 里程碑总览

| 里程碑 | 主题 | 产出 | 验收一句话 | 依赖 | 风险 |
| --- | --- | --- | --- | --- | --- |
| **M0** | 财政地基 + HUD | `CityEconomySystem`、整数税收结算、HUD 资金/人口行、`FCityRng`、新 agent queries | 挂机 16x 一天，资金随就业市民增长且可被脚本断言 | — | 低 |
| **M1** | Zone 运行时化 + 人口动态 | `CityZoneSystem`、市民 zone id 化、满意度 v1、迁入/迁出、pool 96、需求条 | 满意度高则人口从 32 上涨、住房占满则停涨；两次运行 query 序列一致 | M0 | **高**（本轮最大重构） |
| **M2** | 建造系统 | scad 空地改造、建筑库预注入、建造菜单 + 拾取、施工→竣工、agent 指令通路 | 造一栋公寓：资金减、床位增、市民迁入新楼、夜里新楼亮灯 | M1 | 中高 |
| **M3** | 升级 + 服务建筑 | 建筑升级（等级/视觉/容量）、变电站 + 诊所覆盖半径、满意度第三因子、维护费 | 造变电站前后：覆盖区工作效率与满意度可见提升，维护费入账 | M2 | 中 |
| **M4** | 事件 + 里程碑目标（**可裁剪**） | `CityEventSystem`、停电/港湾节、toast 通知、人口里程碑解锁 + 结算面板 | 停电出现→花钱抢修→恢复全程可玩；96 人弹结算 | M3 | 低 |
| **M5** | 平衡 + 回归收尾 | 数值平衡（20~30 分钟标准局）、management agentscript 全流程回归、截图/文档更新 | 一局完整跑通达成北极星；两个 agentscript 全绿 | M0–M4 | 低 |

> 依赖链是严格串行 M0→M1→M2→M3→M4→M5；**M4 整体可裁剪**（LOC 超预算或时间不够时先砍）。**M1 是本轮最高风险里程碑**：Zone 重构动市民系统全部绑定关系，且 96 ScadRig 性能未知——M1 第一天先做性能实测（任务 1.1），不达标立即降级到 64 并回写 design。

---

## 2. 里程碑详情

### M0 — 财政地基 + HUD（低风险）

**目标**：建立整数财政与确定性随机的地基，打通"游戏状态 → HUD → agent query"三层，为后续所有里程碑供电。

任务：
- [ ] `CityEconomySystem.{h,cpp}`：`FCityEconomy` 数据（design §3.1）+ 每游戏小时整点结算（收入 = Work 状态市民 × `taxPerWorkerHour`；本阶段无支出）。
- [ ] `FCityRng`（可放 `CitySolSimTypes.h`）：`std::mt19937` 封装，agent-validation 模式固定种子；后续所有随机决策必须走它（R-CS2）。
- [ ] `CitySolSimGameInstance`：接线 economy tick（挂 `OnTick`，尊重暂停 / 时间倍率）；`RegisterAgentQueries` 追加 `city.treasury`、`city.population`、`city.workingCount`、`city.incomeTotal`。
- [ ] HUD：控制台窗口追加"资金 / 人口 / 就业"一行（人口本阶段即市民数）。
- [ ] `citysolsim-smoke.agentscript.json`：追加两步 —— 快进一游戏日后 `assert city.treasury gt 初始值`；确认既有步骤不动。

**验收**：
1. `./gnb build CitySolSim` 通过。
2. `gnb shot --target CitySolSim --ui`：HUD 出现资金行且非零。
3. `gnb validate --script assets/agentscripts/citysolsim-smoke.agentscript.json` 全绿（含新增断言）。

**风险**：低。注意结算挂 `GameMinutes` 整点而不是真实时间，16x 快进时一小时结算不能漏也不能重。

---

### M1 — Zone 运行时化 + 人口动态 ⚠️ 高风险

**目标**：把住宅 / 工作 / 休闲从 constexpr 数组重构为运行时 `FZone` 对象（容量 / 入住 / 等级），市民与 zone 显式绑定；在此之上跑通满意度 → 迁入迁出的人口动态。**这是本轮地基，宁可慢不可歪。**

任务：
- [ ] **1.1 性能实测（第一天做）**：临时把 `poolCapacity` 与 `kCitizenCount` 拉到 96 跑 `gnb shot`，用 `engine.frameRate` query 记录帧率。达标（≥60）则定 96，否则定 64 并回写 design §2 D6。
- [ ] `CityZoneSystem.{h,cpp}`：`FZone` 向量（design §4.1）+ 开局从 `kHomes/kWorkplaces/kLeisure` 种子灌入（容量按 32 人现状反推 + 每类留 ~20% 空位）+ `AllocateHome/AllocateJob/Release*` 分配接口 + 需求统计（design §4.2）。
- [ ] `CitizenSystem` 重构：`FCitizen` 的 `homeIndex/workIndex/leisureIndex` 改为 zone id；目的地 / 寻路目标从 `CityZoneSystem` 取锚点；无业市民状态（不上班只休闲，不产税）。
- [ ] 满意度 v1：通勤（40）+ 休闲可达（30）+ 服务保底分（30 档先恒给 15），整数，每游戏小时重算；`CitizenSystem` 暴露城市平均值。
- [ ] 迁入 / 迁出：每游戏日 06:00 结算（design §5.2），迁入走 `FCharacterPool::Acquire` + zone 分配，迁出走 `Release`；名字池扩到 96 个（config）。
- [ ] HUD：满意度行 + 三条需求条（住房 / 就业 / 休闲，0~100 进度条）。
- [ ] agent queries 追加：`city.satisfaction`、`city.housingFree`、`city.jobsFree`、`city.unemployed`。
- [ ] 确定性自查：agent-validation 模式下同一 build 连跑两次 smoke 脚本，两次的 `city.population`/`city.treasury` 断言路径一致。

**验收**：
1. `./gnb build CitySolSim`（若动了 `Gameplay/Sim`：加 `gkNextUnitTests`）通过。
2. `gnb shot --target CitySolSim --ui`：HUD 三条需求条 + 满意度可见。
3. `gnb validate`：快进 3 游戏日后 `city.population > 32`；继续快进至住房占满后人口停涨（`city.housingFree == 0` 且 population 不再变）。
4. smoke 脚本原有断言不回归（`citizenCount == 32` 这类写死数值的断言按新初值更新，属预期改动，需在 journal 里注明）。

**风险**：**高**。①重构半径大——市民所有目的地逻辑都过一遍，建议先改数据结构编译通过、行为等价（人口锁 32、无迁入迁出）截图对比无变化，再开人口动态；②96 ScadRig 性能未知（任务 1.1 前置探雷）；③迁出时市民可能正在通勤中——`Release` 前先中断寻路，复用 AirportSim 处理过的 pool 回收路径。

---

### M2 — 建造系统（中高风险）

**目标**：玩家第一次真正"花钱改变城市"：点空地 → 选建筑 → 扣款 → 施工 → 竣工入住。

任务：
- [ ] `habor_city_v2.scad` 改造：腾出 8~10 个街块放地基板 + 角桩视觉；锚点登记 `CitySolSimConfig.hpp::kLots`。**改完同步拷贝到 build assets，并 `gnb shot` 确认空地在画面里**。
- [ ] 建筑表（design §6.2 六种）落 config：constexpr 定义表（key / 造价 / 容量 / 维护费 / 解锁人口 / 视觉参数）。
- [ ] 建筑库预注入（R-CS3）：`CityBuildSystem::InjectAssets` 在 `BeforeSceneRebuild` 注入所有建筑的分层盒体 model + 材质（含窗光 emissive、脚手架材质）；运行时建造仅 spawn node 组 + 可见性切换 + `scene.MarkDirty()`。
- [ ] `CityBuildSystem::TryBuild(lotId, buildingKey)` 单入口：解锁校验 → 资金校验（调 economy，R-CS4）→ 占用 lot → 进入施工态（游戏时 6h）→ 竣工替换节点组 → 注册 `CityZoneSystem`。
- [ ] 拾取与 UI：空地点击拾取（复用 `ProjectForPick`，锚点投影 + 放大阈值）→ 建造菜单（价格 / 置灰 / 未解锁隐藏）；竣工 toast（简版，右下角文字即可，完整 toast 队列在 M4）。
- [ ] Agent 指令通路（design §6.4）：`ConfigureCVars` 注册 `city.agent.order`（string）；每帧消费解析 `build <lotId> <key>`，与 UI 收敛到 `TryBuild`。
- [ ] agent queries 追加：`city.lotFree`、`city.underConstruction`、`city.builtCount`。
- [ ] `citysolsim-management.agentscript.json` 首版：cvar 下建造指令 → assert 资金减少 → wait-until 竣工（`city.underConstruction == 0`）→ 快进数日 assert `city.population` 超过 M1 上限（新床位被住上）→ 夜景截图。

**验收**：
1. `./gnb build CitySolSim` 通过。
2. `gnb shot --target CitySolSim --ui`：画面里能看到空地地基板。
3. `gnb validate --script assets/agentscripts/citysolsim-management.agentscript.json` 全绿。
4. 肉眼流程：`gnb run CitySolSim` 手动点空地造公寓，施工脚手架 → 竣工 → 夜里新楼窗光亮（跟路灯同一开关时机）。
5. smoke 脚本不回归。

**风险**：中高。①运行时 spawn node 的具体引擎 API 以 MagicaLego 现行做法为准（动工前先读它的 block 放置代码）；②scad 改造可能碰到既有种子建筑锚点——空地优先选目前没有 zone 锚点的街块；③施工 6h 结转要挂游戏时间而非真实时间（暂停时不施工）。

---

### M3 — 升级 + 服务建筑（中风险）

**目标**：给钱一个"花在存量上"的去处（升级），并引入第一层空间策略（服务覆盖半径选址）。

任务：
- [ ] 升级：`CityBuildSystem::TryUpgrade(zoneId)`（等级 1→3，造价 = 基础 × 等级，容量按表增长）；视觉 = 预注入的分层盒体逐层显现（design §6.3）；仅玩家所建建筑可升级（种子建筑不动）。
- [ ] 建筑信息面板：点击玩家建筑 → 右上窗口显示 等级 / 容量 / 入住 / 升级按钮（复用"观察对象"窗口位与样式）。
- [ ] 服务建筑：变电站 + 诊所落地（表已在 M2 注入）——`CityZoneSystem` 增加覆盖查询（zone 锚点到服务建筑距离 ≤ 半径）；变电站覆盖 → 覆盖区工作 zone 效率 100%（未覆盖 70%，进税收结算）+ 覆盖区新建筑夜灯；诊所覆盖 → 该区市民服务分满额（替换 M1 的保底 15）。
- [ ] 维护费：服务建筑与休闲建筑维护费进每日结算（economy 支出侧）。
- [ ] agent 指令追加：`upgrade <zoneId>`；queries 追加 `city.serviceCoverage`（覆盖人口百分比）。
- [ ] management 脚本追加：造变电站 → assert `city.serviceCoverage` 上升 + 下一结算收入高于上一结算。

**验收**：
1. build 通过；两个 agentscript 全绿。
2. `gnb shot --ui`：升级后的建筑肉眼可见变高；夜景图中变电站覆盖区窗光更亮/更多。
3. 数值可解释：HUD 收入在造变电站后一结算周期内上升，维护费出现在支出侧。

**风险**：中。覆盖半径判定注意用整数化距离比较（R-CS1：半径比较用平方距离整数运算即可）。

---

### M4 — 事件 + 里程碑目标（低风险，**可裁剪**）

**目标**：给一局注入节奏与终点：突发事件要玩家响应，人口里程碑给玩家阶段感，96 人收束一局。

任务：
- [ ] `CityEventSystem.{h,cpp}`：固定种子日历预排（R-CS2）；停电（design §7：变电站离线 12 游戏时，花 800 抢修）+ 港湾节（每 5 天，休闲市民税 +50%）。
- [ ] Toast 通知队列：右下角 3 秒淡出（事件 / 里程碑 / 竣工统一走这里，替换 M2 简版）。
- [ ] 人口里程碑：48 / 64 / 80 解锁建筑（M2 表中 `解锁人口` 字段生效）+ 一次性奖金；96 人弹结算面板（天数 / 累计收支 / 平均满意度 / 继续观察按钮）。
- [ ] agent 指令追加：`repair <zoneId>`；queries 追加 `city.activeEvent`（0=无，枚举值）。
- [ ] management 脚本追加：wait-until 停电事件 → cvar 抢修 → assert 恢复。

**验收**：build 通过；两个 agentscript 全绿；`gnb run` 手动玩到一次停电 + 一次节日，toast 与响应闭环顺畅。

**风险**：低。裁剪原则：LOC 超预算（design §10）或进度吃紧时整体砍掉，M5 直接收束——砍掉后建筑解锁改为全部开局可用，并在 journal 注明。

---

### M5 — 平衡 + 回归收尾（低风险）

**目标**：把系统堆成"一局好玩的游戏"，并把回归资产固化。

任务：
- [ ] 数值平衡：以 16x 挡位跑标准局，目标 20~30 分钟（真实时间）从 32 → 96 人；调初始资金 / 税率 / 造价 / 迁入速率，数值全部收敛回 config 一处。
- [ ] 北极星验收图：第 1 天 vs 第 10 天同机位无 HUD 对比截图（`gnb shot`），进 `docs/projects/citysolsim/`。
- [ ] `citysolsim-management.agentscript.json` 终版：从开局到 64 人（脚本时长控制在几分钟内，不必打满 96）的全流程断言链，作为常驻回归。
- [ ] smoke 脚本终版核对：观察盒行为（跟随 / 昼夜 / 快进）全部保留。
- [ ] 文档收尾:本 plan 状态改"已完成"+ 各里程碑勾选；design 里与实现有出入处回写；`AGENT_GUIDE` 若增补 CitySolSim 条目在此时做。
- [ ] LOC 核对：`gnb loc` 确认增量在 +2.0k 预算内。

**验收**：
1. `./gnb build CitySolSim` + 两个 agentscript 全绿。
2. 一局手动完整体验达成北极星（design §1.2 的"两张截图看出城市长大"）。
3. 文档 / 截图 / 回归脚本全部就位。

---

## 3. 执行提示（给接手的 agent）

- 动工顺序内不要跨里程碑抢跑：M1 的 zone 重构没验收前，M2 的任何代码都别写。
- 每个里程碑完成后按仓库惯例提交一次（`feat(citysolsim): M<N> ...`），保持可回退粒度。
- 改到 `Gameplay/Sim/CharacterPool` 公共层时（M1 可能扩 pool 行为），记得它还有别的使用方（AirportSim / CharacterDemo 等），构建面按 `AGENTS.md` targeted-build 规则扩大。
- 拿不准引擎运行时 API 时，参考对象：运行时 spawn node 看 MagicaLego，pool 回收看 AirportSim，agent 验证脚本语法看 `assets/agentscripts/` 现有脚本。
