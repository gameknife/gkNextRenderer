---
title: "NextTotalwar 战斗 MVP 设计（近战结算 / 士气溃逃 / 指挥官 AI）"
category: project
status: 计划中
owner: NextTotalwar
created: 2026-07-30
last_updated: 2026-07-30
---

# NextTotalwar — 战斗 MVP 设计

> **前置**：行军 MVP 已实现（选择、定向命令、部队级 A\*、阵型槽位跟随、地形贴合），
> 见 [nexttotalwar-mvp-design.md](nexttotalwar-mvp-design.md) 与
> [AGENT_GUIDE/NextTotalwar.md](../../../AGENT_GUIDE/NextTotalwar.md)。
> 本文只设计**在此之上新增的战斗层**，面向后续实现的 AGENT：所有引擎事实都标了核实位置，
> 所有里程碑都给了可执行验收命令。
>
> **本期目标**：两支敌对部队接触后士兵自动交战；被命中的士兵闪白；阵亡后倒地成为尸体散落战场；
> 部队实时减员；士气崩溃会溃逃；一方（默认红方，可选双方）由简单战术 AI 指挥。

---

## 0. 范围界定

**要做：**

- **接战判定**：敌对 regiment 阵型框接触即进入交战，anchor 停止推进。
- **士兵配对与近战结算**：前排士兵各自锁定敌兵，按攻击间隔做命中/伤害判定，血量归零即阵亡。
- **冲锋（charge）**：行军状态下撞进敌阵的部队获得数秒冲锋加成，并对敌方士气造成打击。
- **侧背攻击**：从敌阵侧面/背面攻击有命中加成与额外士气惩罚（全战最重要的战术杠杆）。
- **减员与尸体**：阵亡士兵播放一次性倒地 clip 后**冻结**成尸体留在原地；部队 strength 递减；归零即被歼灭。
- **士气与溃逃**：伤亡/被侧背/寡不敌众/友军溃逃拉低士气；低于阈值转入 Routing 向本方后方逃跑，脱战后可集结。
- **表现**：受击闪白、地面血迹（池化）、部队血条/士气条 HUD、胜负横幅。
- **指挥官 AI**：按 Line / Flank / Reserve 三种角色执行推进、冲锋、迂回、预备队投入；默认只接管红方，可一键接管双方（自动战斗，方便截图与验证）。
- **确定性**：固定 tick + 种子化 RNG，agentscript 可断言战果。

**不做（明确排除，别顺手加）：**

- ❌ 远程/弓箭抛射与箭矢实体（archers 本期按弱近战兵参战，接口预留见 §12）。
- ❌ 疲劳、将领光环、经验等级、兵种克制矩阵（枪对骑）等全战进阶数值层。
- ❌ 骑兵、冲击碾压、单位间刚体推挤/挤压（士兵之间仍不做碰撞）。
- ❌ 攻城、可破坏物、战场增援、战役层结算。
- ❌ 音频、命中特效粒子系统、伤害飘字。
- ❌ 网络同步（确定性设计只服务于本地回归验证，不承诺 lockstep）。

---

## 1. 设计支柱

1. **战斗是"部队级状态 + 士兵级结算"的两层结构。**
   接战判定、士气、命令、AI 决策全部在 regiment 粒度（24 个）；命中/伤害/死亡在 soldier 粒度（2400 个），
   但只有**处于接战对中的士兵**才进入配对与结算。这条决定了 CPU 预算与"看起来像全面战争"。
2. **战斗仿真层不碰 Node。**
   `Battle/` 下所有系统只读写 `FRegiment`/`FSoldier` 的纯数据字段与事件缓冲，**不允许**解引用
   `Assets::Node`、不允许调 animator。所有视觉副作用由 `Render/CombatFx` 消费事件完成。
   好处：整场战斗可以在平地 lambda 上无渲染跑单测（§10 C0）。
3. **固定 tick + 种子 RNG = 可断言。**
   战斗以 20 Hz 固定步长推进，RNG 是每场战斗一个种子的确定性发生器，消费顺序固定为
   (regiment id, slot index)。agent validation 下退化为"每渲染帧恰好一个 tick"，使脚本断言稳定复现。
4. **表现优先用"零新增预算"的手段。**
   闪白 = 逐节点材质切换（proxy 每帧全量重建，改材质本来就免费，§2.2）；
   尸体 = 复用士兵原有节点冻结姿势（proxy 总数恒定不增长）。
   只有血迹池是新增节点，且是固定 256 个。
5. **AI 与玩家共用同一条下令通道。**
   AI 不得直接改 `regiment.path/state`，必须调用与右键命令相同的 `IssueMove/IssueCharge` API。
   这样"AI 能跑"等价于"玩家命令链路正确"，也避免两套行为漂移。

---

## 2. 已核实的引擎与现状事实

> 以下均在当前分支核实，实现时可直接依赖；与旧文档冲突时以本节为准。

### 2.1 当前运行规模（文档有漂移，以代码为准）

`NextTotalwarGameInstance.cpp:35-38`：`regimentCountPerFaction = 12`、`soldiersPerRegiment = 100`
→ **24 个 regiment / 2400 名士兵**。`assets/agentscripts/nexttotalwar-march.agentscript.json:15`
断言 `game.soldierCount == 2400`、`game.renderProxyCount >= 14000`（≈ 6 proxy/兵），
Massive 上限 262,140，占用不到 6%。
`AGENT_GUIDE/NextTotalwar.md` 里"每队 512 人 / 12,288 人 / 73,728 proxy"是旧扩军实验的残留描述，别照抄。

每名士兵 = 1 个 world node + rig（7 bone node + 6 part render node），
rig 结构见 `assets/scad/characters/tw_spearman.scad:4-20`。

### 2.2 逐节点材质切换是免费的

`OnTick` 每帧调 `Scene::MarkTransformDirty()`（`NextTotalwarGameInstance.cpp:417`），
`MarkTransformDirty` 置 `sceneDirty_ = true`（`Scene.cpp:1092-1096`），
`Scene::UpdateNodesGpuDriven()`（`Scene.Update.cpp:447+`）因此**每帧遍历全部节点重建并上传 nodeProxy**。
`Node::GetNodeProxy()`（`Node.cpp:222-241`）把 `RenderComponent` 的 16 槽材质数组整体拷进 proxy。

结论：**改 `RenderComponent::SetMaterials` 当帧即生效，且不产生额外脏标记成本**。闪白按这条实现。

反例（不要用）：`RenderOutlineFlags::danger` 只有 `Core.SwModernNoAmbient.comp.slang:206-208` 会解读，
在当前默认渲染器下不可见，不能拿来做受击反馈。

### 2.3 当前默认渲染器是 PathTracing

`ConfigureCVars` 实际设置 `r.rendererType = 0`（`NextTotalwarGameInstance.cpp:125`），
尽管上方注释写的是 SoftwareModern。实现闪白时必须在**当前默认渲染器下**肉眼验证：

- 首选闪白材质：`SceneBuilder::AddLambertianMaterial(materials, vec3(0.95))` 近白漫反射，安全、无噪点。
- 备选：`AddDiffuseLightMaterial`（`SceneBuilder.hpp:13`）自发光。**风险**：PathTracing 下同时存在
  几百个自发光实例会增加 NEE 采样负担并可能产生萤火虫噪点，只在近白漫反射不够醒目时再试，并记录帧率对比。

### 2.4 ScadRig 支持一次性 clip（尸体用）

`FRigClip::loop`（`RigAsset.hpp`）+ `FRigAnimator::Advance` 对非循环 clip 做
`clamp(time, 0, duration)`（`RigInstance.cpp:120-131`）→ **播完自动停在最后一帧**。
SCAD 侧写法是 clip 数据首项 `["loop", false]`，参考 `assets/scad/lib/kit_char.scad:317` 的 `ch_clip_sit()`。

`FRigAnimator::Play` 对"已经是当前 clip"是 no-op（`RigInstance.h:52`），默认 0.15 s 交叉淡入，
可以每帧无脑调用（现有 `TickSoldiers` 就是这么用的）。

### 2.5 池化特效节点的既有范式

`Brotato3DDebrisSystem.cpp:96-175`：`BeforeSceneRebuild` 里
`FProcModel::CreateBox`（`FProcModel.hpp:11`）造 model → `SceneBuilder::CreateRenderNode`
（`SceneBuilder.hpp:17`）批量建节点 → 初始 `Assets::NodeUtils::SetVisible(node, false)` 藏起来 →
运行时环形复用。血迹池照抄这套。

### 2.6 游戏可注册自己的 CVar

`FCVarSystem::RegisterFloat/Bool/Int`（`CVarSystem.hpp:90-110`），
游戏侧范例 `TruckerDemoGameInstance.cpp:35`、`KongLie3DGameInstance.cpp:348`。
战斗调参全部走 `tw.*` cvar，改数值不需要重编译，agentscript 也能用 `cvar` step 设置。

### 2.7 Agent 验证通道

`FAgentQueryRegistry::Add` 支持 `bool / int64 / double / std::string`
（`AgentQueries.hpp:11`），因此胜负结果可以直接暴露成字符串。
agentscript 支持 `cvar`、`exec`（走控制台一行命令）、`wait-until`、`assert`、`screenshot`
（`tools/gnb/internal/validate/validate.go:260-272`）。

---

## 3. 代码结构与文件地图

`NextTotalwarGameInstance.cpp` 已经 1327 行，战斗层**不要继续往里堆**。本期同时完成一次轻量拆分：

```text
src/Application/Game/NextTotalwar/
├── NextTotalwarGameInstance.{hpp,cpp}   # 只保留：入口、输入、相机、选择/命令、HUD、系统编排
├── NextTotalwarTypes.h                  # 扩展 FSoldier / FRegiment 战斗字段
├── NextTotalwarCombatConfig.hpp         # 兵种战斗属性表 + 调参默认值（cvar 绑定目标）
├── Battle/
│   ├── FormationLayout.{h,cpp}          # 既有，扩展 RepackSlots
│   ├── BattleState.h                    # FBattleState / FBattleContext / FCombatEvent 缓冲
│   ├── CombatModel.{h,cpp}              # 纯函数：命中率、伤害、攻击弧、士气目标值（单测主战场）
│   ├── CombatGrid.{h,cpp}               # 士兵空间哈希（纯数据，单测）
│   ├── CombatSystem.{h,cpp}             # 接战判定、配对、攻击结算、死亡事件
│   ├── MoraleSystem.{h,cpp}             # 士气推进、溃逃/集结、歼灭与胜负判定
│   └── CommanderAI.{h,cpp}              # 指挥官 AI：感知快照 → 角色行为 → 下令请求
└── Render/
    ├── BattleCamera.{h,cpp}             # 既有
    └── CombatFx.{h,cpp}                 # 唯一触碰 Node 的战斗代码：闪白、死亡 clip、血迹池
```

**分层硬约束**（review 时按这条卡）：`Battle/` 下任何 `.cpp` 都不许 include
`Engine/Assets/Core/Node.hpp`、`RenderComponent.hpp` 或 `RigInstance.h`。
士兵的 `worldNode / renderNodes / animator` 字段对 `Battle/` 不可见——通过把它们放进
`FSoldierVisual`（独立数组，索引与 soldier 对齐）实现，或者退一步：允许字段留在 `FSoldier` 里，
但 `Battle/` 代码不得访问，由单测的 `-Wunused` 与 code review 保证。**推荐前者**。

系统需要的世界查询通过上下文注入，保持可测：

```cpp
// BattleState.h
struct FBattleContext
{
    std::function<float(float, float)> SampleGround;   // 游戏侧传 GroundHeight
    const NextGameplay::FNavGrid* navGrid = nullptr;   // AI 迂回路径可用，可为空
    float worldHalfExtent = 200.0f;
};
```

---

## 4. 数据模型扩展

```cpp
// NextTotalwarCombatConfig.hpp —— 兵种战斗属性（只读原型）
struct FUnitCombatDef
{
    int   maxHealth      = 30;     // 士兵血量
    int   attack         = 10;     // 攻击值
    int   defense        = 10;     // 防御值（含盾/甲的抽象）
    int   damage         = 12;     // 每次命中伤害
    float attackInterval = 1.05f;  // 攻击间隔 秒
    float weaponReach    = 1.20f;  // 有效攻击距离 m
    int   chargeBonus    = 8;      // 冲锋窗口内的攻击加成
    float baseMorale     = 70.0f;  // 士气基准 0..100
};

// NextTotalwarTypes.h 扩展
enum class ESoldierState : uint8_t { Formation, Fighting, Dying, Dead };

struct FSoldier            // 新增字段
{
    // ... 既有 position / yaw / slotIndex / phaseOffset ...
    ESoldierState combatState = ESoldierState::Formation;
    int16_t health        = 0;
    int16_t targetRegiment = -1;      // 敌方 regiment 下标
    int16_t targetSoldier  = -1;      // 敌方 soldier 下标
    float   attackTimer   = 0.0f;     // 距下次挥击的秒数（初值用相位错开）
    float   flashTimer    = 0.0f;     // > 0 表示正在闪白
    float   deathTimer    = 0.0f;     // 倒地 clip 剩余时间，归零后冻结
};

enum class ERegimentState : uint8_t
{
    Idle, Marching, Reforming,        // 既有
    Engaged,                          // 接战：anchor 冻结，前排交战
    Charging,                         // 冲锋中（Marching 的加速变体）
    Routing,                          // 溃逃：不可下令
    Destroyed,                        // 全灭
};

struct FRegiment           // 新增字段
{
    // ... 既有 ...
    int   strength      = 0;          // 存活人数（= 初始人数 - 阵亡）
    int   startStrength = 0;
    int   kills         = 0;
    float morale        = 70.0f;
    float chargeTimer   = 0.0f;       // > 0 表示冲锋加成生效中
    float outOfContact  = 0.0f;       // 脱战秒数（集结判定用）
    float orderLock     = 0.0f;       // 溃逃/AI 冷却期间不接受新命令
    std::vector<int16_t> engagedWith; // 当前接触的敌方 regiment 下标（通常 0..3 个）
};
```

**事件缓冲**（仿真 → 表现的唯一通道）：

```cpp
// BattleState.h
enum class ECombatEventType : uint8_t { Hit, Death, Rout, Rally, RegimentDestroyed };

struct FCombatEvent
{
    ECombatEventType type;
    int16_t regiment;      // 受影响方
    int16_t soldier;       // Hit / Death 才有效
    glm::vec3 worldPos;
    float yaw;             // Death：倒地朝向
};
```

每 tick 追加，`CombatFx::Consume(events)` 后 `clear()`。事件缓冲预留容量 4096，禁止每 tick 重新分配。

---

## 5. 战斗管线

### 5.1 一帧的编排（改造后的 `OnTick`）

```text
1. camera_.Tick(dt)
2. commanderAI_.Tick(dt)          // 1 Hz 分桶；产出 order 请求，走玩家同款 API 下发
3. TickRegiments(dt)              // 既有：Marching/Charging 推进 anchor、Reforming 收敛
4. combatAccumulator_ += dt;      // 固定 20 Hz
   while (accumulator >= 0.05 && substeps < 3):
       CombatSystem::Tick(0.05)   // 接战判定 → 配对 → 攻击结算 → 死亡事件
       MoraleSystem::Tick(0.05)   // 士气推进 → 溃逃/集结 → 歼灭/胜负
5. TickSoldiers(dt)               // 运动：Dead 跳过 / Fighting 走战斗运动 / 其余走阵型槽位
6. CombatFx::Tick(dt, events)     // 闪白开关、倒地 clip、血迹池；消费并清空事件
7. MarkTransformDirty()
```

`tw.determinism`（默认跟随 `GOption->AgentValidation`）为真时，步骤 4 改为**每渲染帧恰好一个 tick**，
使 `wait-frames` 型断言可复现。

### 5.2 接战判定（regiment broadphase）

每个战斗 tick：

1. 对每个 regiment 算阵面 OBB：中心 = anchor，半长 = `Formation::FormationHalfExtent(strength, ranks, ...)`
   加 `tw.combat.engageMargin`（默认 1.6 m），朝向 = facing。
2. 只测**敌对** regiment 对（12×12 = 144 对），先用 anchor 距离剪枝（> 半径和 + margin 直接跳过），
   再做 OBB-OBB 分离轴测试。
3. 结果写进双方 `engagedWith`。
   - 从空 → 非空：`Marching/Charging → Engaged`；若来源是 `Charging`，给
     `chargeTimer = tw.combat.chargeWindow`（默认 4 s），并给被撞方压一个冲锋士气事件。
   - 从非空 → 空且状态是 `Engaged`：转 `Reforming`（原地重整），`outOfContact` 开始累计。
4. `Engaged` 的 regiment **anchor 冻结**（不推进、不转向），玩家仍可下新命令强行脱离
   （命令直接把状态改回 `Marching`，MVP 不给脱离惩罚）。

144 对/tick × 20 Hz 的成本可以忽略；不需要 regiment 级空间结构。

### 5.3 士兵配对（narrowphase）

只处理"至少有一个 `engagedWith` 的 regiment"里的**存活**士兵。

- `FCombatGrid`：cell 2.0 m 的均匀网格哈希（`head/next` 两个扁平数组，容量按总兵数预留，
  每 tick 清空重填，**零分配**）。只插入参战方存活士兵。
- 每个未持有有效目标的士兵，查询 3×3 邻域，取满足条件的最近敌兵：
  - 是敌对阵营、存活；
  - 距离 ≤ `tw.combat.searchRadius`（默认 2.4 m）；
  - 该目标当前被锁定人数 < `tw.combat.maxAttackersPerTarget`（默认 3）。
- **黏性目标**：一旦锁定就不换，直到目标死亡或距离 > `searchRadius * 1.5`（滞回，防抖）。
- 锁定成功 → `combatState = Fighting`；找不到目标 → 回 `Formation`（继续站槽位）。

"每个目标最多 3 个攻击者"是复刻全战观感的关键：它让后排挤不进战线，形成"前排绞肉、后排等待"的画面。
实现上用一个与士兵对齐的 `attackerCount` 数组，每 tick 清零后在配对阶段累加。

### 5.4 近战结算

对每个 `Fighting` 且目标有效的士兵：

```text
attackTimer -= dt
if attackTimer > 0: continue
if distance(self, target) > weaponReach + 0.25: continue   // 还没贴上，等运动追上去
attackTimer = attackInterval * (1 + rng.Jitter(±0.15))

arc      = 目标朝向与 (self - target) 的夹角
flankBonus = arc > 120° ? tw.combat.rearBonus (默认 6)
           : arc > 60°  ? tw.combat.flankBonus (默认 3) : 0
chargeAdd  = chargeTimer > 0 ? def.chargeBonus : 0
moralePenalty = 部队 Wavering 时 -tw.combat.waveringPenalty（默认 3）

hitChance = clamp(base + (attack + flankBonus + chargeAdd + moralePenalty - targetDefense) * k,
                  minChance, maxChance)
            // base = tw.combat.hitBase (0.45), k = tw.combat.hitScale (0.03)
            // clamp = [0.08, 0.92]

if rng.Chance(hitChance):
    target.health -= damage
    events.push(Hit{target, ...})
    if target.health <= 0: → 死亡流程（§5.6）
else:
    events.push(Hit{target, blocked})   // 也闪白，但用更弱的闪（可选）
```

**数值目标**（调参基线，M 阶段必须实测校准）：
10 列宽的两个 100 人方阵正面对撞，应在 **45–75 秒**内分出胜负；
侧背突击应把这个时间压到一半以下。若实测偏离，优先调 `hitBase` 与 `maxHealth`，不要动结构。

| 兵种 | maxHealth | attack | defense | damage | attackInterval | weaponReach | chargeBonus | baseMorale |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| Spearman  | 32 | 9  | 13 | 12 | 1.15 | 1.55 | 6  | 72 |
| Swordsman | 30 | 13 | 10 | 14 | 0.95 | 1.15 | 10 | 76 |
| Archer    | 24 | 6  | 5  | 8  | 1.20 | 1.05 | 2  | 55 |

（矛兵高防长反应慢、剑兵高攻贴脸快、弓兵近战弱——本期不做兵种克制矩阵，克制感由这三组数值自然产生。）

### 5.5 战斗中的运动与阵型

`TickSoldiers` 按 `combatState` 分三支：

- `Dead`：**完全跳过**（不算槽位、不写 transform、不更新 animator）。这是本期最大的性能红利。
- `Fighting`：目标方向 seek，直到距离 ≤ `weaponReach * 0.85` 后停住并把 yaw 转向目标；
  但位移不得超过自身槽位 `tw.combat.maxBreakDistance`（默认 2.5 m），超出则拉回槽位方向。
  这条约束让阵型在绞肉时"变形但不散架"。
- `Formation`：既有逻辑（槽位 seek + 地形贴合）。

**阵型重整（RepackSlots）**：`Formation::RepackSlots(regiment)` 把存活士兵按当前 (row, col) 次序
重新编号为 `0..strength-1`，之后所有 `SlotLocalOffset` 调用改用 `regiment.strength` 而不是
`soldiers.size()`。**只在 `Engaged` 之外调用**（下达新命令、Reforming 开始、集结完成时），
交战中让战线自然变稀疏——这正是全战的观感。

**必须一并改掉"把死人算进去"的现有调用点**（漏一处就会看到选择框横跨半张地图）：

| 位置 | 现状 | 改法 |
|---|---|---|
| `NextTotalwarGameInstance.cpp:455` Reforming 误差循环 | 遍历全部 soldiers | 跳过 Dead |
| `:483` TickSoldiers 主循环 | 遍历全部 | 按 combatState 分支 |
| `:581` TryProjectRegimentBounds | 用全部士兵位置求包围盒 | 只用存活 |
| `:1197` DrawWorldOverlay 选中框 | 同上 | 只用存活 |
| `:996` HUD `totalSoldierCount` | 常量 | 改存活合计 |
| `:1045` 部队条人数 | `soldiers.size()` | `strength / startStrength` |
| `:1262` `game.soldierCount` | 累加 size | 保留，另加 `game.aliveSoldiers` |

### 5.6 死亡、尸体与减员

死亡流程（仿真侧）：

1. `combatState = Dying`，`deathTimer = tw.fx.deathClipSeconds`（默认 0.8 s）。
2. `regiment.strength--`；攻击方 `regiment.kills++`。
3. 从战斗网格与所有锁定它的士兵目标里摘除（被锁定者下 tick 重新配对）。
4. `events.push(Death{...})`，`worldPos` = 死亡点（吸附地形高度），`yaw` = 当前朝向 + 种子抖动 ±25°。
5. `strength == 0` → `state = Destroyed`，从选择集合与 AI 可用列表移除，
   `events.push(RegimentDestroyed)`。

表现侧（`CombatFx`）：

1. 收到 `Death` → `animator.Play("die")`（非循环 clip，§2.4），世界节点 yaw 加抖动。
2. `deathTimer` 归零 → `combatState = Dead`，**此后永不再更新 animator、不再写 transform**。
   尸体就地留存，节点与 proxy 数量恒定不变（不新增、不回收）。
3. 可选：`SetCastShadows(false)` 降低远景阴影开销；只有在实测阴影 pass 有压力时才做。

**为什么不做尸体池/合批**：士兵总数上限固定（2400），尸体数 ≤ 2400 且**不新增节点**，
proxy 峰值等于开局值。引入烘焙合并模型只有在把兵力扩到万级时才有意义（记进 §12）。

---

## 6. 士气与溃逃

每个战斗 tick 为每个非 `Destroyed` 的 regiment 求一个**目标士气**，再限速逼近，避免数值抖动：

```text
lossFraction = 1 - strength / startStrength

target = def.baseMorale
       - tw.morale.lossWeight * 100 * lossFraction      // lossWeight 默认 0.70
       - (被侧翼攻击中 ? 18 : 0)
       - (被背后攻击中 ? 26 : 0)
       - (被冲锋命中窗口内 ? 14 : 0)
       - (局部以少打多 ≥1.5:1 ? 12 : 0)
       - min(24, 8 * 40m 内溃逃的友军数)
       + (近 8 s 杀敌 > losses ? 10 : 0)
       + (接触中的敌人正在溃逃 ? 15 : 0)

morale → target，下行限速 tw.morale.fallRate（10/s），上行 tw.morale.riseRate（5/s）
```

状态机：

| 条件 | 状态 | 行为 |
|---|---|---|
| morale ≥ 40 | Steady | 正常 |
| 20 ≤ morale < 40 | Wavering | 命中判定 -3 攻击等效；禁止发起冲锋；HUD 黄色闪烁 |
| morale < 20 | **Routing** | 立即脱战，`orderLock = tw.morale.routeLock`（8 s）内不可被下令；以 `runSpeed × 1.15` 向本方战场边缘（faction 0 → x = −190，faction 1 → x = +190）跑；阵型间距 ×1.6（松散逃散） |
| Routing 且脱战 ≥ 6 s 且 morale ≥ 45 | Rally | `RepackSlots` 后转 `Reforming`，恢复可控 |

**歼灭与胜负**：某阵营所有 regiment 均为 `Destroyed`，或持续 `Routing` 超过
`tw.morale.defeatSeconds`（默认 12 s）→ 对方获胜。
`battleOutcome` 取值 `"none" | "faction0" | "faction1" | "draw"`（draw 用于 `tw.battle.timeLimit` 超时）。
胜负产生后停止 AI 下令，HUD 顶部显示横幅，仿真继续跑（方便截图观察战场残留）。

---

## 7. 表现层（`Render/CombatFx`）

### 7.1 受击闪白

- 初始化时缓存每个士兵的**基准材质数组**（`std::array<uint32_t,16>` × 6 part），
  它已经在 `CreateSoldierVisuals`（`NextTotalwarGameInstance.cpp:313-328`）里算好，顺手存进 `FSoldierVisual`。
- 预注册一个 `flashMaterialId`（近白漫反射，§2.3）与其对应的"全槽位白"数组。
- 收到 `Hit` 事件：`flashTimer = tw.fx.flashSeconds`（默认 0.12 s），
  **仅在状态跳变时**写材质（进入闪白写白数组，退出写回缓存数组），不要每帧重写。
- 阵亡的士兵在写倒地姿势前**必须先恢复基准材质**，否则会留下一具白色尸体（这是最容易出的 bug，
  单测无法覆盖，验收截图要专门看）。

同时可选（默认开）：命中方在挥击瞬间也闪一个更短的白（`tw.fx.attackerFlash`，默认 0.06 s），
让接触线像参考图那样"闪成一片"。

### 7.2 血迹

- `BeforeSceneRebuild` 里 `FProcModel::CreateBox({-0.45, -0.01, -0.45}, {0.45, 0.01, 0.45})` 造一块薄板，
  暗红 Lambertian 材质，`SceneBuilder::CreateRenderNode` 建 `tw.fx.bloodPoolSize`（默认 256）个隐藏节点。
- 收到 `Death` → 环形取一个 slot，放到死亡点（`y = SampleGround + 0.02`），随机 yaw、缩放 0.8–1.4，
  `NodeUtils::SetVisible(node, true)`。超出池容量就覆盖最旧的。
- 成本：固定 256 proxy。**这是本期唯一新增的渲染预算**。

### 7.3 HUD

- 底部部队条每格加：血条（`strength / startStrength`，绿→黄→红）、士气条（细条，蓝→橙），
  `Destroyed` 置灰，`Routing` 描红边并显示 "ROUT"。
- 选中面板加：击杀数、当前接战的敌方部队、士气数值、状态名。
- F1 调试面板加：`engaged / routing / destroyed` 计数、本帧战斗 tick 数、
  战斗系统 CPU 毫秒（`SCOPED_CPU_TIMER` 或简单计时）、事件数、活跃闪白数、血迹池占用。
- 顶部：胜负横幅 + 双方总兵力对比条。

---

## 8. 指挥官 AI（`Battle/CommanderAI`）

### 8.1 接管范围

`tw.ai.factions` 位掩码，默认 `2`（只接管 faction 1 = 红方）。
键 `T` 切换 bit0，即"玩家方托管/自动战斗"——这条是本期演示与截图的主要入口，
`gnb shot` 与 agentscript 都靠它把战斗自动跑起来。HUD 显示当前接管方。

### 8.2 决策循环

- 频率 1 Hz，按 `regimentId % 4` 分 4 桶错峰（实际每 0.25 s 处理一桶）。
- 每次决策前构建一份**感知快照**（纯数据，可单测）：

```cpp
struct FAIRegimentView
{
    int  id, faction, strength, startStrength;
    glm::vec3 anchor; float facing, morale;
    ERegimentState state;
    int  nearestEnemy;         // -1 表示没有
    float nearestEnemyDist;
    float localStrengthRatio;  // 己方接战兵力 / 敌方接战兵力
};
struct FAISnapshot { std::vector<FAIRegimentView> all; glm::vec3 friendlyCenter, enemyCenter; };
```

### 8.3 角色分配（开局一次，溃逃集结后重分配）

按 anchor 的横向次序排序己方 regiment：

- **Flank**：最外侧各 1 支（优先选 Swordsman）——共 2 支。
- **Reserve**：靠中后的 2 支（优先 Archer 之外的兵种）。
- **Line**：其余（约 8 支）。

### 8.4 行为

| 角色 | 行为 |
|---|---|
| **Line** | 维持一条与敌方战线平行的横队，向"最近敌方 Line 部队前方 `contactOffset`（默认 6 m）"推进；进入 `tw.ai.chargeRange`（默认 30 m）且朝向偏差 < 35° 时改下 **charge 命令**（run + 冲锋标记）。接战后不再下令。 |
| **Flank** | 目标点 = 敌方战线最外侧 regiment 的侧后方（沿敌方 facing 的反方向 `20 m`、横向外扩 `25 m`），到位后朝敌方**背面**冲锋。抵达前若被拦截接战，就地转 Line 行为。 |
| **Reserve** | 停在己方战线后方 `tw.ai.reserveDepth`（默认 55 m）。投入条件（任一满足）：任何己方 Line 部队 strength < 55%；有敌方部队进入己方战线后方 45 m；己方总兵力优势 > 1.3。投入后角色改 Line。 |
| **Routing** | 不下令（`orderLock` 期间物理上也拒绝命令）；集结完成后回到 Reserve。 |

### 8.5 约束（避免 AI 抽搐）

- 同一 regiment 两次下令间隔 ≥ `tw.ai.orderCooldown`（默认 2.5 s）。
- `Engaged / Charging / Routing` 状态一律不接受新命令（全战式"接战即锁定"）。
- 目标点变化 < 8 m 时不重新下令（去抖）。
- AI 只能调 `IssueMoveOrder(regiment, target, facing, bRun)` / `IssueChargeOrder(...)`；
  为此需要把现有 `IssueMoveOrders`（作用于"当前选中集合"，`:797`）拆成
  "对单个 regiment 下令"的底层函数 + "对选中集合下令"的上层封装。玩家右键仍走上层。

---

## 9. CVar 调参表（全部 `ECVarFlags::None`，运行时可改）

| CVar | 默认 | 含义 |
|---|---|---|
| `tw.combat.enabled` | 1 | 战斗总开关（关掉退回纯行军 MVP，用于隔离回归） |
| `tw.combat.tickRate` | 20 | 战斗固定 tick 频率 |
| `tw.combat.engageMargin` | 1.6 | 接战判定的阵型框外扩 m |
| `tw.combat.searchRadius` | 2.4 | 士兵找目标半径 m |
| `tw.combat.maxAttackersPerTarget` | 3 | 单个目标最多被几人围攻 |
| `tw.combat.maxBreakDistance` | 2.5 | 士兵可离开槽位的最大距离 m |
| `tw.combat.hitBase` / `hitScale` | 0.45 / 0.03 | 命中率公式常数 |
| `tw.combat.chargeWindow` | 4.0 | 冲锋加成持续秒 |
| `tw.combat.flankBonus` / `rearBonus` | 3 / 6 | 侧、背攻击加成 |
| `tw.morale.enabled` | 1 | 士气总开关 |
| `tw.morale.lossWeight` | 0.70 | 伤亡对士气的权重 |
| `tw.morale.fallRate` / `riseRate` | 10 / 5 | 士气升降限速（点/秒） |
| `tw.morale.routeLock` | 8.0 | 溃逃后不可下令秒数 |
| `tw.morale.defeatSeconds` | 12.0 | 全军溃逃多久判负 |
| `tw.fx.flashSeconds` | 0.12 | 受击闪白时长 |
| `tw.fx.attackerFlash` | 0.06 | 攻击方闪白时长（0 = 关） |
| `tw.fx.deathClipSeconds` | 0.8 | 倒地 clip 时长 |
| `tw.fx.bloodPoolSize` | 256 | 血迹池容量（StartupOnly） |
| `tw.ai.factions` | 2 | AI 接管的阵营位掩码 |
| `tw.ai.orderCooldown` | 2.5 | AI 下令冷却 |
| `tw.ai.chargeRange` | 30 | 冲锋触发距离 |
| `tw.ai.reserveDepth` | 55 | 预备队后置距离 |
| `tw.battle.seed` | 1337 | 战斗 RNG 种子（StartupOnly） |
| `tw.battle.deployDistance` | 125 | 双方部署 x 距离（调小可快速开战，截图/验证用） |
| `tw.determinism` | 跟随 AgentValidation | 每帧恰好一个战斗 tick |

---

## 10. 里程碑与验收

每个里程碑都以"可截图 / 可断言"收尾，不允许攒到最后一次性验证。

### C0 — 纯仿真骨架（无渲染、可单测）

**交付**：`BattleState.h`、`CombatModel`、`CombatGrid`、`CombatSystem`（接战 + 配对 + 结算 + 死亡事件）、
`NextTotalwarCombatConfig.hpp`、种子 RNG、`tw.combat.*` cvar；`FSoldierVisual` 拆分让 `Battle/` 脱离 Node 依赖。
战斗结果先只体现为数值（HUD 计数变化），不做任何视觉。

**验收**：`src/Tests/Test_NextTotalwarCombat.cpp`

```bash
gnb.bat build NextTotalwar gkNextUnitTests
out\build\windows\bin\gkNextUnitTests.exe "[NextTotalwar][Combat]"
```

必须覆盖：命中率对 (attack − defense) 单调且被 clamp；伤害累加与死亡边界；
攻击弧分类（正面/侧/背）；空间哈希查询结果与暴力枚举一致；
**平地合成战斗**：两个 100 人方阵对撞，跑 2000 tick，断言一方 strength 归零、
另一方 strength 单调不增、同种子两次运行逐 tick 完全一致、不同种子结果不同。

### C1 — 接战、减员与尸体（可见）

**交付**：`TickSoldiers` 三分支、`RepackSlots`、§5.5 表格里的全部"死人过滤"改动、
`CombatFx` 的倒地 clip 与冻结、HUD 血条与计数、
`tw.battle.deployDistance` 调近以便手动开打；SCAD 侧新增 `anim_die`（非循环）与 `anim_attack`。

**资产**：在 `assets/scad/lib/kit_tw.scad` 增加 `tw_clip_attack_thrust()`（矛）、
`tw_clip_attack_slash()`（剑/弓近战）、`tw_clip_die_fall()`（首项 `["loop", false]`，
末帧 root 绕 X 倒下 ≈ −85° 并下沉，四肢摊开），三个 rig 文件各加 `anim_attack` / `anim_die`。
注意 SCAD 是 Z-up、鼻子朝 −Y（`AGENT_GUIDE/ScadRig.md`），倒下是绕 X 轴旋转。

```bash
gnb.bat scad catalog                       # 0 bad / 0 warning
out\build\windows\bin\gkNextUnitTests.exe "[ScadRig]"
gnb.bat shot --target NextTotalwar --ui
```

截图必须看到：两阵接触、前排贴脸、战线后方地面有倒地尸体、HUD 血条掉下来。

### C2 — 命中反馈

**交付**：闪白（含"阵亡前恢复材质"）、血迹池、攻击方短闪、调试面板的 FX 计数。

```bash
gnb.bat shot --target NextTotalwar --ui
```

肉眼过：接触线上有明显白色闪烁、没有永久变白的尸体、血迹贴地不悬空、帧率相对 C1 不显著下降
（记录 C1/C2 的 FPS 对比进 journal）。

### C3 — 士气与胜负

**交付**：`MoraleSystem`、Wavering/Routing/Rally、溃逃跑向本方边缘、歼灭与胜负判定、
胜负横幅、`tw.morale.*` cvar。

**验收**：单测补 `[NextTotalwar][Morale]`（目标士气公式、限速逼近、状态迁移阈值与迟滞、
"友军溃逃传染"上限）；`gnb shot` 观察一次完整崩盘。

### C4 — 指挥官 AI

**交付**：`CommanderAI`（快照、角色分配、三类行为、约束）、`IssueMoveOrder` 单部队化重构、
`T` 键自动战斗、`tw.ai.*` cvar、HUD 显示接管方与 AI 下令计数。

**验收**：单测 `[NextTotalwar][AI]` 用合成快照断言角色分配稳定、冲锋触发条件、
预备队投入条件、冷却与去抖生效（同一快照连续两次决策只下一次令）。

### C5 — 自动化验收与文档收口

**交付**：`assets/agentscripts/nexttotalwar-battle.agentscript.json`；
更新 `AGENT_GUIDE/NextTotalwar.md`（战斗层代码导览 + 修正过时的兵力数字）；
把本文 status 改为"已实现"并追加实测记录（战斗时长、FPS、proxy 占用、CPU 分解）；
`docs/README.md` 索引项已在本期建档时加好。

```bash
gnb.bat validate --script assets\agentscripts\nexttotalwar-battle.agentscript.json
```

脚本骨架：

```json
{
  "name": "nexttotalwar-battle",
  "target": "NextTotalwar",
  "steps": [
    {"type": "wait-until", "query": "engine.status", "op": "eq", "value": "Running", "timeoutMs": 30000},
    {"type": "wait-until", "query": "game.navReady", "op": "eq", "value": true, "timeoutMs": 120000},
    {"type": "cvar", "name": "tw.battle.deployDistance", "set": "45"},
    {"type": "cvar", "name": "tw.ai.factions", "set": "3"},
    {"type": "wait-until", "query": "game.engagedRegiments", "op": "ge", "value": 2, "timeoutMs": 120000},
    {"type": "screenshot", "out": "screenshots/nexttotalwar-clash", "ui": true},
    {"type": "wait-until", "query": "game.totalKills", "op": "ge", "value": 150, "timeoutMs": 180000},
    {"type": "assert", "query": "game.aliveSoldiers", "op": "le", "value": 2250},
    {"type": "assert", "query": "game.corpseCount", "op": "ge", "value": 150},
    {"type": "wait-until", "query": "game.routingRegiments", "op": "ge", "value": 1, "timeoutMs": 180000},
    {"type": "screenshot", "out": "screenshots/nexttotalwar-rout", "ui": true},
    {"type": "assert", "query": "game.renderProxyCount", "op": "le", "value": 30000},
    {"type": "quit"}
  ]
}
```

**新增 agent 查询**：`aliveSoldiers`、`factionStrength0`、`factionStrength1`、`engagedRegiments`、
`routingRegiments`、`destroyedRegiments`、`totalKills`、`corpseCount`、`combatTicks`、
`aiOrdersIssued`、`battleOutcome`（字符串）。

---

## 11. 性能预算与验证矩阵

**CPU（战斗层新增，2400 兵满编交战为最坏情况）：**

| 项 | 频率 | 量级 | 说明 |
|---|---|---|---|
| regiment OBB 接战判定 | 20 Hz | 144 对 | 可忽略 |
| 空间哈希重建 | 20 Hz | ≤ 2400 插入 | 扁平数组，零分配 |
| 配对查询 | 20 Hz | ≤ 2400 × ~10 候选 | 黏性目标后实际远低于此 |
| 攻击结算 | 20 Hz | ≤ 2400 次判定 | 只有 Fighting 的士兵 |
| 士气 | 20 Hz | 24 | 可忽略 |
| AI | 4 Hz（分桶） | 12–24 | 可忽略 |
| 闪白材质写入 | 事件驱动 | 数百次/秒 | 每次 6 个 part 的数组赋值 |

**渲染预算**：新增仅血迹池 256 proxy。尸体复用原节点，proxy 峰值 = 开局值（当前 ≈ 14–15k / 262,140）。
**死亡反而降低 CPU**：Dead 士兵跳过槽位计算、transform 写入与 animator。

**必跑矩阵**：

| 改动类型 | 必跑 |
|---|---|
| `Battle/` 纯逻辑 | `gnb.bat build NextTotalwar gkNextUnitTests`；`gkNextUnitTests "[NextTotalwar]"` |
| 表现/FX | 上面 + `gnb.bat shot --target NextTotalwar --ui` |
| SCAD clip / kit | `gnb.bat scad catalog`；`gkNextUnitTests "[ScadRig]"`；同步 cp 到 `out/build/<preset>/assets/scad/` |
| AI / 命令链路 | 上面 + `nexttotalwar-battle` / `-select` / `-march` 三个 agentscript 全绿 |
| 触碰 `Gameplay/Rig` 共享层 | 额外 `gnb.bat build AirportSim StudioSim CitySolSim CharacterDemo` |

**回归红线**：C1–C5 全程 `nexttotalwar-select` 与 `nexttotalwar-march` 必须继续通过
（战斗层不得破坏既有行军 MVP）；`tw.combat.enabled 0` 时行为必须与战斗前完全一致。

---

## 12. 风险与降级

| 风险 | 触发信号 | 降级 |
|---|---|---|
| 两阵穿模、士兵互相插进对方阵中 | 观感崩坏 | 接战即冻结 anchor（已在设计内）；仍不够就加 `maxBreakDistance` 收紧到 1.5 m |
| 目标抖动导致士兵原地转圈 | 士兵频繁换目标 | 黏性目标 + 1.5× 滞回；必要时加最短锁定时长 1 s |
| 战斗过快/过慢 | 一次交锋 < 20 s 或 > 3 min | 只调 `tw.combat.hitBase` 与 `maxHealth`，不动结构 |
| 士气雪崩，开打就全军溃逃 | 多支部队几秒内 Routing | 降 `lossWeight`、提 `fallRate` 限速、抬高 Routing 阈值 |
| 白色尸体 / 闪白残留 | 截图出现纯白士兵 | 死亡路径强制恢复基准材质；加调试查询 `game.flashingSoldiers` 观察是否归零 |
| PathTracing 下闪白不明显或产生噪点 | 截图看不出闪 / 出现萤火虫 | 近白漫反射 ↔ 自发光二选一（§2.3），或临时切 `r.rendererType 2` 验证并记录 |
| 战斗 CPU 超预算 | 帧时间 CPU 侧 > 8 ms | 降战斗 tick 到 10 Hz；配对改为每 tick 只处理一半士兵（错峰）；缩小 `searchRadius` |
| AI 抽搐/来回走 | 部队反复改向 | 冷却 + 8 m 去抖（已在设计内）；再不行把 AI 频率降到 0.5 Hz |
| 溃逃部队卡在河/山脊 | 逃跑部队原地不动 | 溃逃走 NavGrid 路径而不是直线；失败则沿地图边缘方向直退，不做寻路 |

---

## 13. 非本期（记录，不构成授权）

- **远程作战**：弓兵齐射。接口预留——`FUnitCombatDef` 加 `isRanged / range / volleyInterval / accuracy`，
  `CombatSystem` 增加一个 `RangedPhase`，在近战阶段之前跑；箭矢用池化实例或纯数值抽象（不出实体）二选一。
  齐射会引入"部队朝向 vs 射界""友军阻挡"两个新问题，必须单独立项。
- 疲劳（影响攻防与速度）、将领与光环、兵种克制矩阵、装备/经验等级。
- 骑兵与冲击碾压（需要单位间推挤，与"不做碰撞"这条支柱冲突，要重新评估）。
- 尸体烘焙成单 part 模型的 LOD（只有扩到万级兵力才需要，见原 MVP 设计 §4.6 的 L2）。
- 战场 UI：兵牌/单位卡、战斗结算面板、录像回放。
- 攻城、多人 lockstep（可参考 NextRA 的 order 协议）。

这些都要在战斗 MVP 验收后各自立项、各自定验收标准，不要在本期"顺手做一半"。
