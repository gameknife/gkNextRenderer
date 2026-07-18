---
title: "CitySolSim —— 经营循环（观察盒 → 模拟经营 Playable）架构设计"
category: design
status: 待实现
owner: engine
created: 2026-07-18
last_updated: 2026-07-18
---

# CitySolSim —— 经营循环（架构设计）

> 状态：**📝 待实现**。本文定义 CitySolSim 下一轮迭代的**目标与目标架构**，配套的分阶段落地顺序见 [`management-loop-plan.md`](management-loop-plan.md)。
>
> 相关代码：`src/Application/Game/CitySolSim/`（当前 ~1.5k 行）；场景资产 `assets/scad/habor_city_v2.scad`；市民角色走 `Gameplay/Sim/CharacterPool`（NextGameplay 共享层）。

---

## 1. 背景：现状与本轮定位

### 1.1 起点（f25d5587 已交付的观察盒）

当前 CitySolSim 是一个**纯观察**的城市模拟展示：

- 固定 SCAD 港湾城（12×6 街块网格，锚点硬编码在 `CitySolSimConfig.hpp`）；
- 36 辆车按程序生成的矩形环线永续行驶（`TrafficSystem`）；
- 32 个 ScadRig 市民按硬编码作息表（在家 / 上班 / 午餐 / 休闲）在 12 住宅 / 12 工作 / 10 休闲锚点间通勤（`CitizenSystem` + `FCharacterPool` NavGrid 寻路）；
- 昼夜循环 + 路灯 + 车灯（`CityTimeSystem`），时间控制 1x/4x/16x/暂停/+1h；
- 镜头平移 / 缩放 / 点击跟随车辆与市民，HUD 显示时间与统计。

**玩家能做的只有"看"**。模拟经营的三要素全部缺失：**资源**（没有钱）、**决策**（不能建造/升级）、**反馈**（城市永远不变）。

### 1.2 本轮北极星

> 一局 20~30 分钟的"市长体验"：**观察需求 → 花钱建造 / 升级 → 市民迁入、城市肉眼可见地变化 → 财政增长解锁更多建造**的正反馈循环。城市从 32 人小城成长到 96 人：新建筑拔地而起、车流变密、夜景灯光变多。

判定"这轮成了"的一句话：**关掉 HUD 截两张图（第 1 天 vs 第 10 天），任何人都能看出城市长大了。**

### 1.3 In Scope / Out of Scope

**In Scope（本轮做）**
- 城市财政（整数资金、税收、维护费、建造扣款）；
- Zone 运行时化（住宅 / 工作 / 休闲从 constexpr 数组变为带容量 / 等级 / 入住数的运行时对象）；
- 人口动态（迁入 / 迁出，市民池 32 → 96）；
- 满意度模型（通勤、休闲可达、服务覆盖三因子）与需求面板（RCI 式）；
- 建造系统（预留空地 + 建造菜单 + 施工过程 + 运行时入场景）；
- 建筑升级 + 2 种服务建筑（覆盖半径影响满意度 / 收入）;
- 确定性事件（停电 / 节日）与人口里程碑目标；
- 全流程 agent 可验证（新增 `game.city.*` 查询 + 调试 CVar 驱动通道 + management agentscript）。

**Out of Scope（明确不做）**
- 道路网络编辑 / 自由摆放（建筑只能落在预留空地锚点上）；
- 真实交通仿真（车辆仍走装饰环线，数量随人口缩放即可）；
- 存档 / 读档（一局制；留接口不实现）；
- 多城市 / 多地图、灾难链、政策税率滑条等深度经营系统；
- LLM / NextAI 接入（StudioSim 已覆盖该方向，本轮不掺和）。

---

## 2. 核心决策（先定调，后面章节展开）

| # | 决策 | 取向 | 理由 |
| --- | --- | --- | --- |
| D1 | 经济数值制 | **全整数**（资金 / 价格 / 收支均 `int64`），决策逻辑不用 float 累积 | agent-validation 模式下确定性可断言；学 NextRA 的纪律红利 |
| D2 | Zone 数据 | config constexpr 数组降级为**种子**，运行时由 `CityZoneSystem` 持 `std::vector<FZone>` | 建造 = 追加 zone；不重构这里一切免谈（本轮最大重构） |
| D3 | 建造的资产通路 | **模型 / 材质在 `BeforeSceneRebuild` 一次性预注入建筑库**；运行时建造只 spawn node 引用既有 model/material id | 运行时注入 model/material 会触发 GPU 场景重建且有共享 model 崩溃前科（AirportSim 教训）；spawn node 是 MagicaLego 验证过的路径 |
| D4 | 空地来源 | 修改 `habor_city_v2.scad` 腾出 8~10 块带地基板视觉的空地，锚点登记进 config | 比"拆除重建"简单一个量级；地基板给玩家明确的"这里能建"提示 |
| D5 | 新建筑视觉 | 首版用 FProcModel 盒体组合（体块 + 窗光 emissive 分层），不做 scad 模块实例化 | 施工两阶段（脚手架 → 成品）用盒体好做；scad 实例化留 P1 |
| D6 | 人口上限 | CharacterPool `poolCapacity` 32 → **96** | 96 个 ScadRig 是性能未知数，M1 首日实测帧率，不达标就降 64 |
| D7 | 玩家驱动通路 | UI 点击为主；同时注册**调试 CVar `city.agent.order`**（字符串指令，游戏每帧消费后清空） | agentscript 的 `cvar` / `exec` 步骤只能到 CVar 层，这是 agent 驱动建造的最短通路 |

---

## 3. 财政系统（CityEconomySystem）

### 3.1 数据模型

```cpp
struct FCityEconomy
{
    int64_t treasury = 6000;        // 初始资金：够前 2 天造 1~2 栋
    int64_t taxPerWorkerHour = 6;   // 每就业市民每游戏小时税收
    int64_t incomeTotal = 0;        // 累计收入（HUD/复盘用）
    int64_t expenseTotal = 0;       // 累计支出
    double  nextSettleAt = 0.0;     // 下次结算的 gameMinutes
};
```

### 3.2 结算语义

- **收入**：每游戏小时结算一次（整点触发，挂 `CityTimeSystem::GameMinutes()`），`收入 = 处于 Work 状态的市民数 × taxPerWorkerHour × 建筑效率系数`（效率系数见 §6 电力覆盖，整数百分比）。
- **支出**：服务建筑维护费随同一次结算扣除。
- **建造 / 升级**：一次性扣款，**扣款校验在 CityEconomySystem 单点做**；UI 按钮置灰只是显示层，重复点击 / agent 指令并发时以系统校验为准。
- 资金不足：拒绝下单，不产生负债（本轮无贷款系统）。

---

## 4. Zone 运行时化（CityZoneSystem）—— 本轮地基

### 4.1 数据模型

```cpp
enum class EZoneType { Home, Work, Leisure, Service };

struct FZone
{
    int id = -1;
    EZoneType type;
    glm::vec3 anchor;            // 门前锚点（市民寻路目标）
    std::string label;           // 中文名（新建筑从名字池取）
    int level = 1;               // 1..3
    int capacity = 0;            // 住宅=床位 / 工作=岗位 / 休闲=品质分
    int occupants = 0;           // 当前入住 / 就业人数
    std::shared_ptr<Assets::Node> buildingRoot;  // 空 = config 种子建筑（scad 城原生）
};
```

- 开局把 `kHomes` / `kWorkplaces` / `kLeisure` 灌成种子 zone（容量按现有 32 人分布反推，留少量空位）；
- `FCitizen` 的 `homeIndex/workIndex/leisureIndex` 改为 **zone id**，休闲目的地每次从休闲 zone 集合里选（保持现有轮换逻辑）；
- 迁入分配 / 迁出释放走 `occupants` 计数，市民与住宅 / 岗位是显式绑定关系。

### 4.2 需求统计（HUD 的 RCI 条）

- 住房需求 = 待迁入压力（满意度派生，见 §5）vs 空床位；
- 就业需求 = 无业市民数 vs 空岗位；
- 休闲需求 = 人均休闲品质分 vs 阈值。
三条 0~100 的需求条，直接告诉玩家"下一栋该建什么"。

---

## 5. 人口与满意度（CitizenSystem 扩展）

### 5.1 满意度（每市民 0~100，整数）

| 因子 | 权重 | 计算 |
| --- | --- | --- |
| 通勤 | 40 | 家↔工作锚点直线距离分段映射（近=满分，>250m 快速衰减） |
| 休闲可达 | 30 | 距最近休闲 zone 的距离 + 该 zone 品质分 |
| 服务覆盖 | 30 | 是否在服务建筑覆盖半径内（M3 前恒给保底分 15） |

城市满意度 = 全体市民平均，HUD 常驻显示。

### 5.2 迁入 / 迁出

- 每游戏日 06:00 结算一次：`满意度 ≥ 65 且有空床位` → 迁入 `1 + 空床位/8` 人（上限受 pool 容量）；`满意度 < 40` → 随机（固定种子）迁出 1~2 人；
- 迁入的市民从名字池取名、分配住宅 + 就业岗位（无岗位则挂"无业"状态，只休闲 / 在家，不产税）；
- 迁出走 `FCharacterPool::Release`，节点回池（`parkedPosition` 机制已有）。

### 5.3 交通耦合（表现层，P1 可裁剪）

- 活跃车辆数 = `clamp(人口 × 0.9, 24, 72)`，随人口增减启停既有环线车辆；
- 建造开工时派一辆物流车驶向工地打转（纯装饰，不进任何决策逻辑）。

---

## 6. 建造系统（CityBuildSystem）

### 6.1 空地（Lot）

- `habor_city_v2.scad` 改造：腾出 8~10 个街块，放浅色地基板 + 角桩视觉；
- `CitySolSimConfig.hpp` 登记 `kLots`（锚点 + 朝向）；
- **注意 scad 资产双份**：源文件改完必须同步到 build assets（`out/build/<preset>/assets/scad/`），否则运行的还是旧城（已知坑）。

### 6.2 可建建筑表（首版 6 种）

| 建筑 | 类型 | 造价 | 容量/效果 | 维护费/天 | 解锁 |
| --- | --- | --- | --- | --- | --- |
| 公寓楼 | Home | 2400 | 床位 8 | 0 | 开局 |
| 写字楼 | Work | 3200 | 岗位 10 | 0 | 开局 |
| 街心公园 | Leisure | 1500 | 品质 20 | 60 | 开局 |
| 变电站 | Service | 4000 | 覆盖半径 90m：夜灯 + 工作效率 100%（未覆盖 70%） | 120 | 人口 48 |
| 社区诊所 | Service | 4500 | 覆盖半径 110m：满意度服务分满额 | 150 | 人口 64 |
| 滨海塔楼 | Home | 6000 | 床位 16（3 级视觉） | 90 | 人口 80 |

数值是初版基线，M5 平衡时统一调；表本身放 `CitySolSimConfig.hpp`（constexpr 定义表，运行时状态在 zone 里）。

### 6.3 建造流程

```
点击空地(拾取) → 建造菜单(可负担项亮起) → 确认 → 扣款
  → 施工态(脚手架盒体, 游戏时 6h) → 竣工(替换为成品节点组)
  → 注册进 CityZoneSystem → 市民下个决策周期即可入住/就业
```

- 施工态 / 成品的 model & material 全部在 `BeforeSceneRebuild` 预注入（D3），竣工只是 node 可见性 / 替换操作 + `scene.MarkDirty()`；
- 拾取沿用现有 `ProjectForPick` 屏幕投影方案（空地锚点当投影点，阈值放大）；
- 升级（M3）：同一流程，等级 +1 → 容量 ↑ + 视觉长高一层（预注入的分层盒体逐层显示）。

### 6.4 Agent 驱动通路（D7）

- `ConfigureCVars` 注册 `city.agent.order`（string，默认空）：游戏每帧读取，非空则解析执行并清空。指令集：`build <lotId> <buildingKey>` / `upgrade <zoneId>` / `demolish <zoneId>`（demolish 仅对玩家所建）；
- agentscript 用 `cvar` 步骤写指令 + `assert` `game.city.*` 查询闭环验证；
- 该通路与 UI 点击收敛到同一个 `CityBuildSystem::TryBuild(...)` 入口（校验单点，见 §3.2）。

---

## 7. 事件与目标（CityEventSystem，M4）

- **确定性调度**：固定种子 RNG，事件在游戏日历上预排（agent-validation 模式下每局相同）；
- 首版 2 个事件：
  - **停电**（第 4 天起可能触发）：随机一座变电站离线 12 游戏时，覆盖区夜灯灭、工作效率降、满意度掉；花 800 立即抢修或等自然恢复；
  - **港湾节**（每 5 天）：休闲需求 spike，节日期间休闲中的市民额外产税 +50%；
- **人口里程碑**：48 / 64 / 80 解锁建筑（§6.2 表）并发一次性奖金，HUD toast 通知；96 人视为"本局达成"，弹结算面板（天数 / 累计收支 / 平均满意度）。

---

## 8. UI 扩展（CitySolSimUI）

- **左上控制台**（现有窗口扩展）：追加 资金 / 人口 / 满意度 行 + 三条需求条；
- **建造菜单**：点击空地弹出（列表 + 价格 + 负担不起置灰 + 未解锁隐藏）；
- **建筑信息面板**：点击玩家所建建筑 → 等级 / 容量 / 入住 / 升级按钮（复用右上"观察对象"窗口位）；
- **Toast 通知**：右下角队列（事件 / 里程碑 / 竣工），3 秒淡出；
- 全部中文文案，沿用现有 ImGui 直排风格，不引入 RmlUi。

---

## 9. 确定性与验证红线

1. **R-CS1**：财政 / 人口 / 满意度 / 事件的所有决策量为整数或定点；float 只允许出现在表现层（位置 / 动画 / 相机）。
2. **R-CS2**：所有随机性走单一 `FCityRng`（固定种子，agent-validation 模式种子恒定）；禁止 `rand()` / 时钟播种。
3. **R-CS3**：model / material 只在 `BeforeSceneRebuild` 注入；运行时只允许 node 级操作（spawn / 可见性 / 变换）。
4. **R-CS4**：建造 / 升级 / 抢修的资金校验只在系统入口做一次；UI 置灰与 agent 指令都不可绕过也不可重复扣款。
5. **R-CS5**：每个里程碑交付时 `gnb validate --script assets/agentscripts/citysolsim-smoke.agentscript.json` 必须仍然全绿（观察盒行为不许回归）。

验证工具链：`./gnb build CitySolSim` → `gnb shot --target CitySolSim --ui` 肉眼验证 → `gnb validate` 断言闭环（详见 plan 各里程碑）。

---

## 10. 文件清单（预估）

**新增**（`src/Application/Game/CitySolSim/`）：
- `CityEconomySystem.{h,cpp}` — 财政（§3）
- `CityZoneSystem.{h,cpp}` — zone 运行时对象 + 需求统计（§4）
- `CityBuildSystem.{h,cpp}` — 空地 / 建筑表 / 建造流程 / agent 指令解析（§6）
- `CityEventSystem.{h,cpp}` — 事件 + 里程碑（§7）
- `assets/agentscripts/citysolsim-management.agentscript.json` — 经营全流程回归脚本

**改动**：
- `CitySolSimConfig.hpp` — zone 种子 / lot 表 / 建筑表 / 数值常量
- `CitySolSimTypes.h` — FCitizen zone id 化、新枚举
- `CitizenSystem.{h,cpp}` — 满意度 / 迁入迁出 / zone id 绑定（§5）
- `TrafficSystem.{h,cpp}` — 车辆数随人口缩放（P1）
- `CitySolSimUI.{h,cpp}` — §8 全部
- `CitySolSimGameInstance.{hpp,cpp}` — 系统接线 / CVar / agent queries
- `assets/scad/habor_city_v2.scad` — 空地改造（§6.1）

**LOC 预算**：现有 ~1.5k，本轮增量控制在 **+2.0k 以内**（游戏侧 demo，超预算先砍 M4）。
