---
title: "NextDayz PVE 生存循环产品化开发计划"
category: project
status: 待实施
owner: NextDayz
created: 2026-07-31
last_updated: 2026-07-31
---

# NextDayz PVE 生存循环产品化开发计划

本计划实现 [NextDayz PVE 生存循环产品化设计](nextdayz-productization-design.md)。估算按一名熟悉本仓库的工程师计算，为 30～40 人日；资产精修、音频和额外平衡轮次不在估算内。阶段必须按依赖顺序交付，不允许先做刷新数量再补稳定 handle、事务和确定性。

## 1. 执行原则

1. 每个阶段保持 `NextDayz` 可运行；业务代码不以 HUD 是否存在作为成功条件。
2. 先做纯数据/纯规则测试，再接场景、渲染和输入。
3. 所有随机行为从 `nextdayz.seed` 派生；Agent 验收不依赖真实时间和偶然掉落。
4. 感染者、物资和 UI 都不得绕过 Inventory/Action/Combat 的公共事务入口。
5. 只在改到公共 Gameplay/Engine 层时构建 `gkNextRenderer + gkNextUnitTests`；应用侧阶段优先构建 `NextDayz` 和相关测试。
6. 每阶段都补 `game.*` 可观察性；不接受只靠肉眼判断状态机正确。

## 2. 阶段依赖

```mermaid
flowchart LR
    P0["D0 基线与技术尖刀"] --> P1["D1 物品与容量"]
    P1 --> P2["D2 生存与消耗品"]
    P0 --> P3["D3 世界语义与 LootDirector"]
    P3 --> P4["D4 感染者池与 AI"]
    P2 --> P5["D5 战斗闭环"]
    P4 --> P5
    P5 --> P6["D6 产品 UI 与局流程"]
    P6 --> P7["D7 平衡、性能与收口"]
```

D1/D2 与 D3 的纯规则部分可分别开发，但共享 Windows build tree 仍需串行构建；D4 以后必须合流。

## 3. D0 — 固化基线与技术尖刀（2～3 人日）

### 工作

- 记录当前默认场景、59 个 loot、开局 AK/60 备弹和现有 agentscript 结果。
- 新增 `nextdayz.seed` 与集中 RNG service，先不改变当前玩法。
- 验证运行中移动的 ScadRig render node 是否能被 CPU acceleration structure 射线稳定命中并返回正确 instance ID。
- 用 1.5 m、2.0 m 两档 NavGrid 对 Riverland 实测构建时间、内存、桥/河和建筑入口可达性。
- 把 `NextDayzGameInstance` 中“业务更新 → 表现更新 → HUD snapshot”顺序写成注释和测试契约。
- 为后续测试新增最小 fixture：纯规则测试不创建 Vulkan；场景集成继续用 `EngineTestFixture` 或 agentscript。

### 预期文件

- `NextDayzConfig.hpp`
- `NextDayzGameInstance.{hpp,cpp}`
- `Combat/CombatEvents.hpp`
- `World/WorldAnchorRegistry.{hpp,cpp}`（只有扫描/统计骨架）
- `src/Tests/Test_NextDayzWorld.cpp`

### 验收

- 同 seed 连续两次得到相同随机序列。
- 技术记录明确选择“动态 render node”或“kinematic capsule”命中代理，不允许留双实现悬而未决。
- NavGrid 档位和 active bubble 策略有实测数据；水面不可走、桥面可走。
- 原有五条 NextDayz agentscript 结果不退化。

## 4. D1 — 物品实例、容器与容量事务（4～5 人日）

### 工作

1. 建立 `FItemDef/FItemInstance/FInventoryContainer`，把容量、堆叠、装备槽和消耗效果集中到定义表。
2. 实现原子事务：添加、移动、拆栈、合栈、装备、脱下、丢弃、消费。
3. 建立基础上衣、裤子、夹克、背包、武器肩带容器；禁止嵌套。
4. 把当前 `Inventory::Add/Consume/CountOf` 包装为过渡适配，逐步迁移 Loot 和 WeaponSystem。
5. 把枪械弹匣状态迁入枪械实例，确保切槽、换弹、丢枪不复制弹药。
6. 修改 Loot 提交：容量预检失败时世界物品不隐藏，reservation 被释放。
7. 暂以点击按钮操作新库存；本阶段不做最终 drag/drop 视觉。

### 单元测试

- 栈合并/拆分数量守恒；
- 容量边界恰好可放、差一格拒绝；
- 脱下装有物品的背包在空间不足时原子失败；
- 枪械实例切槽、丢弃、拾回后弹匣数量保持；
- Loot commit 容量失败后仍可再次拾取；
- scene generation 变化后旧 loot handle 不能提交。

### 验收

- HUD 可显示每个容器 `used/capacity`。
- 装备中型背包后总容量增加，脱下规则正确。
- 原有射击、换弹、切枪 agentscript 仍通过。

## 5. D2 — 饥饿、口渴、生命与物品使用（3～4 人日）

### 工作

- 新增 `SurvivalSystem` 和 `EPlayerLifeState`，实现三项状态、阈值、归零伤害和死亡事件。
- 代谢使用 simulation dt，不读取昼夜 `TimeScale`。
- 扩展 `PlayerActionController` 支持 Eat/Drink/Heal/DrinkFromWell/FillBottle。
- 新增饮水瓶、空瓶、夹克/裤子等 item defs；为地图增加最低保底饮水点。
- 扫描 `cw_prop_well` 为世界交互点；燃油与饮水严格分开。
- 把动作取消、提交和动画 fallback 接到现有 presentation state。

### 单元测试

- 静止/冲刺代谢比例；
- 昼夜快进不影响代谢；
- 食物、水、治疗效果 clamp 在 0..100；
- 提交前取消不消费，提交后恰好消费一次；
- 饥渴归零按速率扣血并最终进入 Dead。

### 验收

- agentscript 可把 hydration 降到阈值，使用水瓶后回升；
- 水井饮用和装水都需要完整动作；
- 死亡后不能移动、开枪或拾取。

## 6. D3 — 地图语义标记与物资刷新（3～4 人日）

### 工作

- 在 `kit_coldwar.scad` 或 NextDayz 专用 kit 中增加语义标记模块，在 Riverland 各 POI 放置 player/zombie/loot anchors。
- `WorldAnchorRegistry` 扫描节点名、变换和 generation，隐藏标记并生成稳定 runtime 记录。
- 把现有拾取点迁移成 `Available/Reserved/Cooldown` 状态机。
- 新增 profile 加权表、分类/POI/全局上限、离屏/距离/冷却检查和保底刷新。
- 保留首轮 59 个摆放物，不在迁移时改变基础地图平衡。
- 增加 F5 overlay：profile、状态、下次可刷时间和拒绝原因。

### 测试

- 同 seed 的 roll 顺序一致，不同 seed 有差异；
- 玩家过近、在视线内、category 达上限时不刷新；
- 玩家离开且冷却到期后刷新；
- 重开清空 cooldown 和 reservation；
- SCAD catalog/scene load 无警告，marker 不进入正常截图。

### 验收

- 拾取食物后，导演显示 Cooldown；满足条件后重新出现。
- 枪械刷新显著慢于食物/水，军事 profile 不在普通村庄大量出现。

## 7. D4 — 感染者资产、对象池、导航与 AI（6～8 人日）

### 工作

1. 制作并测试 `nextdayz_infected.scad` 的 6 个必需 clip；共享 mesh，不在刷新时加载资产。
2. 实现固定容量 `ZombieVisualPool` 与 generation handle，注册命中代理。
3. 建立 `ZombieSystem` 运行时状态与 `ZombieSpawnDirector`，实现距离、视线、POI 和预算规则。
4. 实现巡逻、调查、追击、攻击、丢失和死亡/回收状态机。
5. 使用分桶感知和 repath；增加卡住检测、有限重试与远离玩家后的安全回收。
6. 接入 `NoiseSystem`，本阶段可用测试按钮/脚步噪声驱动，枪械伤害留 D5。
7. 添加平民/工业/军事三个 profile，只改变基础数值与 tint。

### 单元/集成测试

- 状态迁移和迟滞；
- 视野角、距离、遮挡和噪声优先级；
- spawn 候选过滤、全局/局部 cap；
- handle 回收后旧命中事件无效；
- 攻击提交时重新校验距离；
- 河流不可穿、桥可达，卡住可恢复；
- 32 只压力场景无每帧无界分配。

### 验收

- 玩家在村庄能观察到低密度巡逻；开枪测试噪声后附近感染者转入 Investigate/Chase。
- 生成和回收全过程不出现镜头内 pop、不遗留可见 parked rig。

## 8. D5 — 枪械/近战/感染者/玩家伤害闭环（4～5 人日）

### 工作

- 扩展 `FWeaponDef`：基础伤害、pellet、距离衰减、噪声、hit-zone multiplier 输入。
- WeaponSystem 产出 trace/hit/shot event，`CombatSystem` 成为唯一伤害入口。
- 实现感染者受击、硬直、死亡、击杀统计和命中反馈。
- 增加至少一种近战武器、对应 loot/视觉/攻击动作和单次命中集合。
- 感染者攻击接入玩家生命；受击时取消未提交的搜刮/使用动作。
- 调整默认开局为低配，移除 AK/60 发测试赠送；测试装备通过 agent fixture 注入。

### 测试

- 头/躯干/肢体倍率与距离衰减；
- 霰弹同目标伤害上限；
- 一次近战挥击不跨帧重复伤害；
- 枪声半径和感染者响应；
- 玩家受击保护、动作取消、死亡门控；
- 旧 generation 的 hit 不伤害复用后的新感染者。

### 验收

- 一条脚本从空手开局拾取武器和匹配弹药，击杀指定感染者；弹药、生命、击杀和 AI 状态断言均成立。

## 9. D6 — 产品 HUD、库存与局流程（4～5 人日）

### 工作

- 把现有开发面板移动到 F5，发布默认隐藏。
- 实现生命/饥饿/口渴、武器、交互、警告和软目标 HUD。
- 实现 Nearby/Equipment/Containers 三栏库存、容量条、点击移动和 drag/drop。
- 所有 UI 命令调用 D1 事务，不直接编辑 vector 或装备字符串。
- 新增开始、暂停、死亡总结、重新开始和基础设置界面。
- 重开实现完整 session reset：RNG、玩家、感染者、loot、动作、库存、HUD transient 全部复位。
- 处理 ImGui input capture，库存点击不能同时开枪或转动相机。

### 验收

- 1600×900、1920×1080 和至少一档高 DPI 下无重叠/裁切。
- 不打开 F5 也能理解生命状态、容量不足、交互和死亡原因。
- 同一进程连续重开三次没有旧 handle、重复物资或 parked visual 泄漏。

## 10. D7 — 平衡、性能、自动验收与文档收口（4～6 人日）

### 平衡目标

- 首件补给 < 60 秒，首次感染者遭遇 3～5 分钟；
- 普通感染者不构成单次必死，但两只以上会迫使走位或逃跑；
- 食物/水不会在出生区无限堆积，也不会因坏随机形成不可恢复软锁；
- 枪械是强力但高噪声选择，近战保留风险收益；
- 背包能明显扩容，但不让玩家无脑带走整个 POI。

### 性能与稳定性

- 记录 0/12/24/32 感染者下的 CPU frame、AI、A*、proxy 数和加载峰值；
- 检查 20 分钟 soak：pool、scene nodes、loot entries、事件 vector 容量稳定；
- 对玩家死亡、重开、场景卸载和应用退出做资源生命周期检查。

### 新增自动验证

建议拆为：

- `nextdayz-survival.agentscript.json`：代谢、饮食、水井、死亡；
- `nextdayz-capacity.agentscript.json`：拾取拒绝、背包扩容、脱包失败；
- `nextdayz-zombie-combat.agentscript.json`：刷新、听声、追击、击杀、玩家受伤；
- `nextdayz-loot-respawn.agentscript.json`：cooldown、离屏刷新和 seed 重放；
- `nextdayz-product-loop.agentscript.json`：完整纵切。

### 最终命令

```powershell
gnb.bat build NextDayz gkNextUnitTests
out\build\windows\bin\gkNextUnitTests.exe "[NextDayz]"
gnb.bat shot --target NextDayz --scene assets/scad/proc/coldwar/riverland_1km.scad --ui
gnb.bat validate --script assets/agentscripts/nextdayz-product-loop.agentscript.json
```

若 D0～D7 只改 NextDayz/NextGameplay 定向消费面，无需 `build --all`。

## 11. 交付清单

- 业务：容量库存、生存值、消耗品、感染者、伤害、刷新、死亡/重开；
- 数据：item/weapon/zombie/world profile 和稳定 seed；
- 资产：感染者 rig、近战/饮水/衣物所需最低资产、世界 markers；
- UI：产品 HUD、库存、局流程，调试 UI 默认隐藏；
- 测试：纯规则单测、场景集成、五条新增 agentscript；
- 文档：实现后把设计改为“现行架构”，把本计划移出 `docs/README.md` 的现行计划入口，并更新实际性能/平衡数据。

## 12. 阶段退出门

任何阶段出现下列情况不得进入下一阶段：

- 业务成功依赖动画 clip 或 HUD callback；
- 容量/拾取操作存在部分提交；
- 新随机逻辑无法用 seed 重放；
- 感染者生成可能发生在玩家视野内；
- agent query 无法区分“没有发生”与“发生后又恢复”；
- 场景重载后旧 handle 仍可访问新对象。
