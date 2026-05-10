---
title: Brotato3D 新敌人 + 黄昏撤离波次重构计划
status: 待开发
owner: codex
last_updated: 2026-05-09
---

# Brotato3D Dusk-Extraction & New Enemies Plan

本计划包含两块独立但相关的内容：

1. 新增两种重型敌人：**Mortar Tank（血牛炸弹兵）** 与 **Lance Charger（冲撞手）**，都需要"地面预警指示器"。
2. 把当前"倒计时结束 → 直接进商店"的波次切换，改造为 **昼夜博弈撤离循环**：倒计时归零=入夜，怪物潮变密集，玩家需要走到地图上的撤离车里站够一段时间才能真正结束一波；硬留下来收益更高，但风险陡增。

> 依赖：本计划与 [loot-overhaul-plan.md](loot-overhaul-plan.md) 互不相关，可并行/先后实现，但若先做 Loot 重构，"夜晚 XP 加成"就直接挂在新的 XP 碎块系统上即可。

## 一、新敌人

### 1.1 Mortar Tank（血牛炸弹兵）

**定位**：远程压制 + 高血量肉盾。被动威胁巨大，但移动迟缓，玩家可以走位破解，但落弹会强迫玩家持续位移。

**核心数值**（写入 `assets/configs/brotato3d/enemies.json`）：

```jsonc
"mortar_tank": {
  "name": "Mortar",
  "hp": 360,                  // ≈ rat 18HP × 20
  "moveSpeed": 0.9,           // 比 Brute 还慢
  "contactDamage": 8,
  "size": [1.0, 1.1, 1.0],
  "color": [0.30, 0.30, 0.42],
  "xpDrop": 8,
  "materialDrop": 5,
  "kitingDistance": 7.5,      // 远离玩家，保持射程
  "mortar": {                 // ★新增子结构★
    "fireIntervalMs": 3500,
    "telegraphMs": 1200,      // 地面警示亮起到落弹之间的时间
    "explosionRadius": 1.8,
    "explosionDamage": 28,
    "throwRangeMin": 4.0,     // 射程下限：太近不投弹（避免自爆）
    "throwRangeMax": 14.0,    // 射程上限
    "lobHeightMeters": 4.0,   // 抛物线峰值高度（仅用于视觉）
    "leadFactor": 0.35        // 玩家速度预测系数：0=死板，1=完全瞄准未来位置
  }
}
```

**FEnemyDef 扩展**（`Brotato3DDataLoader.hpp`）：

```cpp
struct FMortarDef
{
    float fireIntervalMs = 0.0f;
    float telegraphMs = 0.0f;
    float explosionRadius = 0.0f;
    int   explosionDamage = 0;
    float throwRangeMin = 0.0f;
    float throwRangeMax = 0.0f;
    float lobHeightMeters = 0.0f;
    float leadFactor = 0.0f;
    bool  enabled = false;
} mortar{};
```

`Brotato3DDataLoader.cpp::LoadEnemies` 解析该子节点（参考已有 `bomb` / `ranged` 的写法）。

**Runtime 扩展**（`Brotato3DEnemy.hpp`）：

```cpp
float mortarFireCooldownMs = 0.0f;        // 距离下一次投弹的冷却
float mortarTelegraphRemainingMs = 0.0f;  // > 0 表示警示器在地上亮着
glm::vec3 mortarTargetPos = glm::vec3(0); // 警示器在地图上的位置（已在 telegraph 触发时锁定）
```

**行为机器**（在 `Brotato3DEnemySystem.cpp::UpdateEnemies` 增加分支）：

1. AI 复用 `kitingDistance` 字段：与玩家距离 < `throwRangeMin` 时后退；距离 > `throwRangeMax` 时前压；中间停下来准备投弹。
2. `mortarFireCooldownMs <= 0 && playerDistance ∈ [min,max] && telegraphRemainingMs<=0` 时触发：
   - `mortarTargetPos = player.worldPos + player.velocityXZ * leadFactor`（带预测，但限制在 arena 内）。
   - `mortarTelegraphRemainingMs = telegraphMs`。
   - 同步推一条"持续型"地面警示（见 1.3）。
3. `mortarTelegraphRemainingMs > 0` 时每帧 `-= deltaMs`；归零瞬间执行落弹：
   - `PushExplosionRing(mortarTargetPos, vec4(1.0, 0.30, 0.10, 1.0), explosionRadius)`。
   - `SpawnTempLight(mortarTargetPos, vec3(1.0, 0.45, 0.1), 5.0f, 250.0f)`。
   - `StartScreenShake(180.0f, 3.0f)`。
   - 对所有 `DistanceXZ(target, player.worldPos) <= explosionRadius` 的玩家造成 `explosionDamage`。
   - 复位 `mortarFireCooldownMs = fireIntervalMs`。
4. **被动 BBO**：投弹期间锁定移动（`moveSpeed=0`），强化"准备动作"的可读性。
5. **死亡处理**：若怪在 telegraph 期间死亡，警示器立即取消，不落弹（由 KillEnemy 路径里清 `mortarTelegraphRemainingMs=0` 即可，并要从地面警示池里把它移除）。

**视觉**：模型仍用现有立方体（暂用 `size=[1,1.1,1]`、`color=深紫蓝`），头顶的"装弹中"用 `warningMaterialId` 闪烁（沿用 Bomber 的现成机制）。

### 1.2 Lance Charger（冲撞手）

**定位**：高威胁突进，看到玩家就锁定一条直线高速冲过来；地面会先出现一条"冲击带"作为预警，给玩家走位窗口。

**与既有 Charger 的区别**：现有 `charger` 是"靠近 → 进入 chargeRamp → 加速冲撞"的渐进型，没有路径预警，难以读招。新 `lance_charger` 是"先锁定方向并地面亮线 → 等待 telegraph → 沿固定方向直线冲刺 → 撞墙/失误后短停"的"读招型"。两个保留各自 ID。

**核心数值**：

```jsonc
"lance_charger": {
  "name": "Lance",
  "hp": 90,
  "moveSpeed": 1.6,           // 待机移动慢
  "contactDamage": 6,
  "size": [0.55, 0.9, 0.55],
  "color": [0.95, 0.50, 0.10],
  "xpDrop": 4,
  "materialDrop": 3,
  "lance": {                  // ★新增子结构★
    "telegraphMs": 700,       // 路径亮线时间
    "windupRangeMin": 4.0,    // 玩家进入此距离开始锁定路径
    "dashSpeed": 14.0,        // 冲刺速度（远高于 moveSpeed）
    "dashDistanceMax": 8.0,   // 冲刺最大距离（撞墙或撞玩家立刻停）
    "dashContactDamageMult": 2.5,
    "recoverMs": 900,         // 冲刺结束后僵直
    "cooldownMs": 2800        // 两次冲刺间最短间隔
  }
}
```

**FEnemyDef 扩展**（同上模式新加 `FLanceDef`）。

**Runtime 扩展**：

```cpp
enum class ELanceState : uint8_t { Idle, Telegraph, Dashing, Recovering };
ELanceState lanceState = ELanceState::Idle;
float lanceStateMs = 0.0f;       // 当前状态剩余 ms
float lanceCooldownMs = 0.0f;    // 全局冷却
glm::vec3 lanceDashDir = glm::vec3(0.0f);  // Telegraph 锁定时记录的冲刺方向
glm::vec3 lanceDashStartPos = glm::vec3(0.0f);
float lanceDashRemainingDist = 0.0f;
```

**状态机**（`UpdateEnemies` 内）：

| 当前状态 | 条件 | 转移 | 副作用 |
|---|---|---|---|
| Idle | `lanceCooldown<=0 && playerDist <= windupRangeMin` | Telegraph | `lanceDashDir = normalize(toPlayerDir)`；`lanceStateMs = telegraphMs`；推路径警示 |
| Idle | else | Idle | 用 moveSpeed 慢慢逼近玩家 |
| Telegraph | `lanceStateMs <= 0` | Dashing | `lanceDashStartPos = worldPos; lanceDashRemainingDist = dashDistanceMax`；播 `Brotato3D::PlayWeaponFireSfx("rifle")` 类似冲刺音效（先复用既有 hitstop） |
| Telegraph | enemy 被击杀 | Idle | 清警示 |
| Dashing | `lanceDashRemainingDist <= 0 \|\| 撞墙(ClampToArena 触发)` | Recovering | `lanceStateMs = recoverMs` |
| Dashing | 与玩家距离 < radius+player.radius | Recovering | 造成 `contactDamage * dashContactDamageMult`；屏震；recoverMs 一致 |
| Dashing | else | Dashing | `worldPos += lanceDashDir * dashSpeed * dt`；不偏转方向 |
| Recovering | `lanceStateMs <= 0` | Idle | `lanceCooldownMs = cooldownMs` |

**视觉**：
- 待机/恢复期：身体 `materialId`。
- Telegraph 期：`warningMaterialId` 闪烁 + 地面亮一条 **3D Beam strip**（见 1.3）。
- Dashing：身体 `hitFlashMaterialId` 持续亮（让玩家清楚"它在冲刺中、会撞死人"）。

### 1.3 地面指示器（共享子系统）

抽出独立 `FGroundIndicator` 数据，避免在 `FExpandingRing`/`FLaserBeam` 上臆造耦合：

```cpp
// Brotato3DProjectile.hpp 中新增
enum class EGroundIndicatorShape : uint8_t { Circle, Strip };

struct FGroundIndicator
{
    EGroundIndicatorShape shape = EGroundIndicatorShape::Circle;
    glm::vec3 worldPos = glm::vec3(0.0f);     // Strip: 起点 / Circle: 中心
    glm::vec3 endPos = glm::vec3(0.0f);       // Strip 专用：终点
    float radius = 0.0f;                      // Circle 专用
    float width = 0.0f;                       // Strip 专用
    glm::vec4 color = glm::vec4(1.0f, 0.18f, 0.10f, 1.0f);
    float totalMs = 0.0f;
    float remainingMs = 0.0f;
    uint32_t enemyTag = 0;                    // 用于"敌人死亡时取消对应指示器"
};
```

**渲染策略**（最少改动）：
- `Circle`：复用 `PushExplosionRing` 的渲染路径，但用一个不同的"模式 = telegraph"标志，让 ring 在 telegraph 阶段不扩散，仅按 `remainingMs/totalMs` 渐变颜色（红→白→爆裂）。
- `Strip`：复用 `PushLaserBeam` 渲染（实际上现有的 LaserBeam 是 3D 线段）；将 y 拍到 0.06f 让它贴地，width 设为 0.6m 即可。落地 0.05f 应避免与地板深度冲突。

**生命周期**：每帧 `UpdateGroundIndicators(deltaSeconds)`：
1. 把所有 `remainingMs` 减去 deltaMs。
2. 颜色按 `t = 1 - remainingMs/totalMs` 渐变（前 60% 偏红橙、60-100% 闪烁/泛白警告）。
3. 归零的指示器转成"瞬间爆炸"事件（推 `FExpandingRing`）。

**敌人死亡的取消机制**：`KillEnemy` 时按 `enemyTag = enemy索引 + 1` 把对应指示器置零。（这里不能复用 FEnemyRuntime 指针因为 vector 可能 reallocate，必须用稳定 ID。）

## 二、黄昏撤离波次重构

### 2.1 设计目标

- 把"打满倒计时 → 直接进商店"改成 **三段式**：
  1. **白天战斗（Day Battle）**：和当前一样，倒计时显示在 UI 上，叫 *Time Until Dusk*。
  2. **黄昏暴动（Dusk Surge）**：天黑了（SkyIntensity 30→5 渐变），怪物刷新速度倍增，地图上出现 **撤离车（Extraction Truck）**。XP 掉落带"夜晚加成"。
  3. **撤离倒计时（Extraction）**：玩家进入车的范围则累计 `extractionElapsed`，离开则停止累计但不归零（先不做"离开扣减"，避免太硬核；可在 playtest 后调）。`extractionElapsed >= extractionRequired` 时本波结算 → 进商店。

- 玩家可以选择"早撤"（车一出现就上）或"贪玩"（多杀一会儿赚 XP）。强制清波时机由玩家决定，不再由倒计时单方面决定。

### 2.2 状态机改造

**当前 `EWaveState`**：`Idle / Active / Intermission / AllCleared`。

**新增**：`DuskSurge`、`Extracting`。

```cpp
enum class EWaveState : uint8_t
{
    Idle,
    Active,         // 白天战斗
    DuskSurge,      // 天黑了，玩家未上车（也可正在上车）
    Intermission,   // 已撤离，进商店
    AllCleared,
};
```

**`FWaveSystem` 内新增运行时字段**：

```cpp
float duskTransitionMs_ = 0.0f;       // SkyIntensity 渐变剩余
float extractionElapsedSec_ = 0.0f;   // 玩家在车里的累积时间
bool  playerInExtractionZone_ = false;
glm::vec3 extractionVehiclePos_ = glm::vec3(0.0f);
bool  extractionVehicleSpawned_ = false;
```

**FWaveDef 扩展**：

```jsonc
"durationSec": 40,           // 既有：白天时长（命名建议保留）
"duskSpawnMultiplier": 2.5,  // ★新增：黄昏期间所有 spawn intervalMs 除以此值
"duskBonusXpMult": 1.6,      // ★新增：黄昏期间敌人掉落 XP 乘数
"extractionRequiredSec": 6.0, // ★新增：站车里需累积秒数
"extractionRadiusM": 2.5     // ★新增：车的吸附半径（XZ 平面）
```

> 默认值如果为 0 就回退到当前行为（不进入 DuskSurge，直接 Intermission）——这样 boss wave 可以保持旧逻辑。

**状态转移**：

| 当前 | 条件 | 转移 | 副作用 |
|---|---|---|---|
| Active | `waveTimeRemainingSec_ <= 0 && wave.bgmCue != "boss"` | DuskSurge | `duskTransitionMs_ = 1500`；触发 OnDuskBegan 事件；车从地图边缘"驶入"指定锚点 |
| DuskSurge | `playerInExtractionZone_` | DuskSurge | `extractionElapsedSec_ += dt` |
| DuskSurge | `extractionElapsedSec_ >= wave.extractionRequiredSec` | Intermission | 触发 waveEndedEvent_ + intermissionStartedEvent_ |
| Active | `bossWave && bossDead` | AllCleared | 保持现状 |

**spawn interval 改造**：`CalculateSpawnIntervalMs` 在 DuskSurge 状态再除一个 `duskSpawnMultiplier`。游戏内主感受：每次刷怪间隔从 1.0s 压缩到 0.4s。

**事件接口**：

```cpp
bool ConsumeDuskBegan();              // 给 GameInstance：触发车驶入、SkyIntensity 渐变、BGM 切到紧张
bool ConsumeExtractionCompleted();    // 给 GameInstance：拉起 ClearAllDebris 等收尾
void NotifyPlayerInExtractionZone(bool inZone);  // GameInstance 每帧把状态喂给 WaveSystem
```

### 2.3 撤离车（Extraction Vehicle）

**外形**：用 `FProcModel::CreateBox` 拼一个简易长方体卡车（车身 + 车斗），单 lambertian 材质足够。后续美术可换 mesh。

**生成**：`Brotato3DGameInstance` 在 `BeforeSceneRebuild` 阶段预创建一个 `extractionVehicleNode_`，初始 `SetVisible(false)`、`Translation = HiddenPosition`。

**驶入动画**（OnDuskBegan 触发）：
1. 选锚点：从 4 个 arena 边中点中随机一个。
2. 起点：锚点向外 4m；终点：锚点向内（到 `extractionVehiclePos_`，例如距离 arena 中心 0.4 倍长边）。
3. 播一个 1.5s 的位移 lerp（用 `runElapsedSec_` 风格的小状态机，不需要走真正的 AnimationTrack）。
4. 同步把 `extractionVehiclePos_` 写入 `FWaveSystem` 用于范围检测。

**触发区域**：`DistanceXZ(player.worldPos, extractionVehiclePos_) < extractionRadiusM`。

**视觉反馈**：
- 站在范围内时车顶发光（emissive 材质切换或叠 area light）。
- HUD 出现"EXTRACTING [###---] 60%"读条（使用现有 `PushFloatingText` 不够，需要在 `Brotato3DUI.cpp` 的 HUD 层加一行）。
- 完全撤离时屏幕轻微闪光 + `PlayWaveStartSfx(...)` 触发当前的"波次结束"音效。

### 2.4 SkyIntensity 渐变

`Brotato3DEffectSystem.cpp::ApplyLightingSettings` 当前是单点写死 50。要做成 runtime 可调：

```cpp
// 新增：runtime 状态
float currentSkyIntensity_ = 50.0f;
float targetSkyIntensity_ = 50.0f;
float skyTransitionTotalMs_ = 0.0f;
float skyTransitionRemainingMs_ = 0.0f;

void Brotato3DGameInstance::SetSkyIntensityTarget(float target, float transitionMs)
{
    targetSkyIntensity_ = target;
    skyTransitionTotalMs_ = transitionMs;
    skyTransitionRemainingMs_ = transitionMs;
}

void Brotato3DGameInstance::UpdateSkyTransition(double deltaSeconds)
{
    if (skyTransitionRemainingMs_ <= 0.0f) return;
    skyTransitionRemainingMs_ = std::max(0.0f, skyTransitionRemainingMs_ - deltaSeconds * 1000.0);
    const float t = 1.0f - skyTransitionRemainingMs_ / std::max(1.0f, skyTransitionTotalMs_);
    currentSkyIntensity_ = glm::mix(currentSkyIntensity_, targetSkyIntensity_, t);
    auto& env = GetEngine().GetScene().GetEnvSettings();
    env.SkyIntensity = currentSkyIntensity_;
    GetEngine().GetScene().MarkEnvDirty();
}
```

> 用户描述的 "30 → 5"。仓库当前是 50。建议：白天 30，黄昏 5。把 `ApplyLightingSettings` 中固定的 `50.0f` 改为 `30.0f` 作为白天默认（playtest 时用 visual test 比对截图）；夜晚 5。

**触发点**：
- `OnDuskBegan`：`SetSkyIntensityTarget(5.0f, 1500.0f);`
- 商店进入时：`SetSkyIntensityTarget(30.0f, 800.0f);`（让商店也是白天感）
- 新一波 Active 开始：保持 30。

**主光源 / 辅助灯**：playerLight 和 area light 可以保留，但在夜晚视觉对比度提升后，玩家光晕会更突出，反而契合"灯火 vs 夜色"。这条不用改。

### 2.5 黄昏期间的玩法增益

- **Spawn 更密**：见 2.2 `duskSpawnMultiplier`。
- **XP 掉落加成**：在 [loot-overhaul-plan.md](loot-overhaul-plan.md) 落地后，`SpawnHitXpDebris` 与 `SpawnKillMaterialDebris` 接受一个 multiplier 参数；GameInstance 在 dusk 状态下把它设为 `wave.duskBonusXpMult`。在 Loot 计划尚未落地前的临时方案：在 `SpawnPickup(enemy.def->xpDrop * mult, ...)` 处加 mult。
- **新敌人组合**：Wave 4+ 黄昏期间 forced 加入 1–2 个 Mortar Tank 或 Lance Charger，增强压迫感。建议在 `FWaveDef` 增加 `duskExtraSpawns` 字段（结构与 spawns 一致），在 DuskSurge 触发时 push 进 `spawnRuntime_`。

### 2.6 UI 改动

- **倒计时标签**：在 Active 阶段显示 "DUSK IN 0:24"；DuskSurge 阶段显示 "EXTRACT NOW" + 闪烁。
- **撤离条**：DuskSurge 期间在屏幕底部中央显示一条进度条，颜色从红 → 黄 → 绿。
- **怪物潮提示**：DuskSurge 触发瞬间推一条 wave banner "NIGHTFALL" + 持续 1500ms。

### 2.7 数据驱动 vs 硬编码

为减小数据迁移成本，第一版可以让"是否启用 DuskSurge"由代码层根据 `bgmCue != "boss" && currentWaveIndex >= 1` 决定（第一波依然走旧流程，给玩家熟悉）。`duskSpawnMultiplier` 等参数全用 wave json，未填默认 1.0 / 不做加成。

## 三、落地步骤

### Phase A — 新敌人

1. 数据层：扩 `FEnemyDef` 增 `mortar` / `lance` 子结构，loader 解析；添加 enemies.json 两条新敌人。
2. Runtime：扩 `FEnemyRuntime` 增 mortar/lance 状态；写 `UpdateMortarTank` / `UpdateLanceCharger` 私有函数从 `UpdateEnemies` 调用（保留现有分支顺序，新分支放最前面，避免互冲）。
3. 地面指示器：新增 `FGroundIndicator` 池（建议 64 容量）；新增 `UpdateGroundIndicators` Tick 步；改 KillEnemy 释放对应 indicator。
4. 视觉资源：在 `BeforeSceneRebuild` 给两个新敌人也跑现有 enemy material 生成流程（hitFlash/warning/dark/phase2）。
5. 加进 `waves.json`：从 wave 4 开始混入 1–3 个 lance_charger，wave 6+ 加入 mortar_tank。
6. 验收：手动跑游戏，能看到 mortar 在地上画红圈→爆炸；lance 画红色射线→冲过来。

### Phase B — 黄昏撤离

1. 在 `Brotato3DEffectSystem.cpp` 把 `SkyIntensity = 50` 改成 30，并加 `SetSkyIntensityTarget` / `UpdateSkyTransition`。
2. `FWaveDef` 加新字段；`FWaveSystem` 加 `EWaveState::DuskSurge`、新事件、修改 Update 流程；spawn interval 算式加 dusk 倍率。
3. `Brotato3DGameInstance` 中：
   - 在 `BeforeSceneRebuild` 创建 extractionVehicleNode_。
   - 加 `OnDuskBegan` 处理：节点驶入动画 + SetSkyIntensityTarget(5)。
   - Tick 中调 `UpdateExtractionVehicle` 做"驶入 lerp + 范围检测 + NotifyPlayerInExtractionZone"。
   - `ConsumeExtractionCompleted` 时 `ClearAllDebris(false)`、`StartShopping()`。
4. `Brotato3DUI.cpp` 倒计时标签加状态分支；底部加撤离进度条；wave banner 加 "NIGHTFALL"。
5. waves.json 给 wave 1–9 写默认 dusk 字段；boss wave (10) 显式 `extractionRequiredSec: 0` 跳过撤离。
6. 验收：playtest 一整局；
   - 倒计时归零看到天黑、车驶入、敌人潮变密。
   - 上车有读条；下车暂停读条；满条进商店。
   - 商店时天又亮回来。
   - boss wave 不进 dusk。

### 通用回归

- `./gnb build --reconfigure` 通过。
- `./out/build/<preset>/bin/gkNextVisualTest` 跑一遍，看夜晚是不是过暗（如果 GI 失真严重再调白天默认到 25）。
- 跑 `gkNextUnitTests`，重点关注 wave / enemy 相关测试是否被破坏。

## 四、风险与备忘

- **指示器渲染对齐**：`FLaserBeam` 当前是空中 3D 线，贴到 y=0.06 可能会被地板深度裁剪/抗锯齿失真。需要 visual test 验证；如果出问题改成 `FExpandingRing` 风格的扁平面片渲染。
- **Mortar 的 lead 预测**：玩家速度变向频繁，过高的 leadFactor 会让玩家很难被命中。先保守 0.35。
- **Lance 撞墙判定**：必须在 worldPos += dashDir * speed * dt 之后立刻 `ClampToArena`，并比较 clamp 前后的位置是否相同来检测撞墙；不要靠 kinematic body 的碰撞反馈（那条链路依赖较重）。
- **DuskSurge 期间商店不开**：注意 `ConsumeIntermissionStarted` 在 DuskSurge 不能被触发，必须等 Extracting 完才进商店。重构 `EnterShop()` 调用点。
- **车阻挡玩家走位**：撤离车不要加碰撞体（NextMotionType::Static），它纯粹是触发器。这样玩家能穿过去打怪。
- **存档/best record**：当前 best record 只记 wave 完成数，不需要改。但若想加"撤离次数"统计可在 `FBestRecord` 里挂字段。
- **i18n**：新文案 "DUSK IN", "EXTRACT NOW", "NIGHTFALL", "EXTRACTING" 加进 `assets/configs/brotato3d/i18n.json`。

## 五、验收标准（DoD）

新敌人：
- Mortar：投弹前 1.2s 地面亮红圈，逃出圈外可零伤害；圈外被擦边伤害合理；自身死亡圈消失不爆炸。
- Lance：冲撞前 0.7s 地面亮红色冲击带，玩家垂直走位可整段躲开；冲撞中撞玩家造成约 2.5x 接触伤害；冲完僵直明显，可被反打。

黄昏撤离：
- 倒计时归零的瞬间天空变暗（约 1.5s 完成 30→5）。
- 车从地图边缘驶入，到达预设位置后停。
- 玩家进车圈视觉/UI 立刻反馈；累积满条后波次结算 → 进商店 → 天空回到 30。
- 黄昏期间敌人刷新明显更密（≥2x 频率），杀敌掉的 XP 比白天多约 60%。
- Boss wave 走旧流程（不进 DuskSurge，直接 AllCleared）。

工程：
- 所有改动跟 [loot-overhaul-plan.md](loot-overhaul-plan.md) 兼容（XP 多倍接口在两处都适用）。
- `./gnb build --reconfigure` 0 error / 不新增 warning。
