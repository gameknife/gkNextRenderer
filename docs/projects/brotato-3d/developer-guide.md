---
title: Brotato3D 开发者指南
category: project
status: 现行
owner: docs
last_updated: 2026-05-10
---

# Brotato3D 开发者指南

本文是 Brotato3D 的开发者梳理文档，目的是让你在阅读完后可以**快速完成数值调整、新增敌人/武器/物品、调整波次和角色配置**。文档不覆盖引擎层细节（Vulkan/RT/ECS），只关注 Brotato3D 子项目的玩法、配置和资产组织。

> 想先了解 Brotato3D 的项目定位，请读 `introduction.md`；想了解代码结构与工程模式，请读 `AGENT_GUIDE/Brotato3D.md`。

---

## 1. 全景概览

Brotato3D 是一款 **C++ 原生** 子应用（不是 QuickJS 脚本游戏），位于：

| 路径 | 内容 |
| --- | --- |
| [src/Application/Game/Brotato3D/](../../../src/Application/Game/Brotato3D) | 全部 C++ 源码（32 个 cpp/hpp） |
| [assets/configs/brotato3d/](../../../assets/configs/brotato3d) | 9 个 JSON 配置（敌人/武器/角色/波次/物品/商店/升级/场景/i18n） |
| `assets/sounds/brotato3d/` · `assets/textures/brotato3d/` | 官方 SFX 与 UI 图标，打包进 `assets/paks/brotato3d.pak` 运行时挂载 |
| `assets/_placeholder/brotato/` | 仍待替换的占位 BGM / 字体 / HUD·菜单图（Brotato 参考素材，**不可分发**） |

**一切玩法数值都通过 JSON 配置驱动**，C++ 只实现**机制（mechanics）**，例如"什么是 charge 冲撞"，而具体冲撞距离、冷却、伤害倍率全在 JSON 里。新增数据通常无需改 C++；只有新增**新机制**（例如全新的怪物 AI 类型）才需要改 C++。

### 1.1 子系统/文件对应关系

| 系统 | 关键源文件 | 说明 |
| --- | --- | --- |
| 入口 / 状态机 | [Brotato3DGameInstance.cpp](../../../src/Application/Game/Brotato3D/Brotato3DGameInstance.cpp), [Brotato3DGameFlowSystem.cpp](../../../src/Application/Game/Brotato3D/Brotato3DGameFlowSystem.cpp) | `OnTick` 主循环、`EAppState` 状态切换、Best Record 持久化 |
| 玩家 | [Brotato3DPlayerSystem.cpp](../../../src/Application/Game/Brotato3D/Brotato3DPlayerSystem.cpp) | 移动、Dash、瞄准、HP、属性合并 |
| 武器 / 弹道 | [Brotato3DProjectileSystem.cpp](../../../src/Application/Game/Brotato3D/Brotato3DProjectileSystem.cpp) | 自动开火、Tier 升级、命中、爆炸、激光 |
| 战斗 | [Brotato3DCombatSystem.cpp](../../../src/Application/Game/Brotato3D/Brotato3DCombatSystem.cpp) | 受伤、被动 Item 触发器、低血怒等 |
| 敌人 AI | [Brotato3DEnemySystem.cpp](../../../src/Application/Game/Brotato3D/Brotato3DEnemySystem.cpp) | 普通追击、Charge、Bomb、Heal、Mortar、Lance、Boss 二阶段 |
| 波次 / 黄昏 | [Brotato3DWaveSystem.cpp](../../../src/Application/Game/Brotato3D/Brotato3DWaveSystem.cpp) | Active → DuskSurge → Intermission 状态机 |
| 商店 / 升级 | [Brotato3DShopSystem.cpp](../../../src/Application/Game/Brotato3D/Brotato3DShopSystem.cpp), [Brotato3DShop.cpp](../../../src/Application/Game/Brotato3D/Brotato3DShop.cpp) | 4 卡商店、Reroll、武器合并 |
| 战利品 / 物理碎块 | [Brotato3DDebrisSystem.cpp](../../../src/Application/Game/Brotato3D/Brotato3DDebrisSystem.cpp) | XP/Material 碎块池、Jolt 物理体、磁吸 |
| 视觉效果 | [Brotato3DEffectSystem.cpp](../../../src/Application/Game/Brotato3D/Brotato3DEffectSystem.cpp) | 屏幕震动、爆炸环、激光束、地面警示 |
| 数据加载 | [Brotato3DDataLoader.cpp](../../../src/Application/Game/Brotato3D/Brotato3DDataLoader.cpp) | 所有 JSON → 内存结构体 |
| UI / HUD | [Brotato3DUI.cpp](../../../src/Application/Game/Brotato3D/Brotato3DUI.cpp) | ImGui 主菜单、HUD、升级/商店/结算面板 |
| 音频 | [Brotato3DAudio.hpp](../../../src/Application/Game/Brotato3D/Brotato3DAudio.hpp) | 全部 SFX/BGM 通道集中在此 header（inline 函数） |
| 资源路径 | [Brotato3DAssetPaths.hpp](../../../src/Application/Game/Brotato3D/Brotato3DAssetPaths.hpp) | `assets/_placeholder/brotato/` 解析 |

### 1.2 核心数据结构（在 [Brotato3DDataLoader.hpp](../../../src/Application/Game/Brotato3D/Brotato3DDataLoader.hpp) 定义）

| 结构体 | 来源 JSON | 说明 |
| --- | --- | --- |
| `FEnemyDef` | `enemies.json` | 敌人原型（含 `ranged/charge/bomb/heal/mortar/lance/boss` 子能力） |
| `FWeaponDef` | `weapons.json` | 武器原型 |
| `FCharacterDef` | `characters.json` | 角色起始属性 + 起手武器 |
| `FUpgradeCardDef` | `upgrades.json` | 升级卡（每级 3 选 1） |
| `FShopItemDef` | `shop_items.json` | 商店属性卡（与 Item 不同） |
| `FItemDef` | `items.json` | 商店被动道具（最多持有 6 个） |
| `FArenaDef` | `arenas.json` | 竞技场半径、地面材质瓷砖 |
| `FWaveDef` | `waves.json` | 波次时长、刷怪表、黄昏倍率、撤离时间 |

---

## 2. 游戏循环与状态机

`EAppState`（在 [Brotato3DGameInstance.hpp:23](../../../src/Application/Game/Brotato3D/Brotato3DGameInstance.hpp)）：

```
MainMenu → CharacterSelect → Playing ⇄ {Hitstop, Paused, LevelUpPicking, Shopping} → Result
```

`OnTick` 流程（[Brotato3DGameInstance.cpp:136](../../../src/Application/Game/Brotato3D/Brotato3DGameInstance.cpp)）：

1. 屏幕震动 / 受伤闪烁 / 天空过渡 / 相机跟随都是**实时**（不受 timeScale 影响）。
2. `globalTimeScale_`：Boss 击杀后 1.2 秒慢动作（0.4 → 1.0 缓动），武器开火**仍是实时**，子弹/敌人/碎块用 `effectiveDt`（注释见 [Brotato3DProjectileSystem.cpp:59](../../../src/Application/Game/Brotato3D/Brotato3DProjectileSystem.cpp)），这是有意为之的"savor the kill"演出，**不要"修复"它**。
3. 子系统按顺序：Player → Weapons → Projectiles → Enemies → EnemyProjectiles → Items 触发器 → 撤离车 → WaveSystem。
4. WaveSystem 通过事件 (`ConsumeWaveEnded` 等) 通知主类何时清场、刷新商店、触发胜利。

### 2.1 波次状态机

[Brotato3DWaveSystem.cpp](../../../src/Application/Game/Brotato3D/Brotato3DWaveSystem.cpp)：

```
Idle → Active → (waveTimeRemainingSec → 0) → DuskSurge → (玩家进入撤离车持续 extractionRequiredSec) → Intermission(=商店) → 下一波
                                                                                                             │
                                                       Boss 波 (bgmCue=="boss") 跳过 DuskSurge ─────────────┘
```

- **DuskSurge（黄昏潮）**：刷怪间隔 / `duskSpawnMultiplier`，并且**无视 spawn count 上限**（无限刷），直到玩家走进撤离车区域累计 `extractionRequiredSec` 秒。
- **撤离车**：每波黄昏开始时一台车从场地外驶入；玩家进入半径 `extractionRadiusM` 内累计停留时间。撤离车本身有碰撞，会挡子弹和敌人。
- **Intermission**：固定 5 秒倒计时（写死在 `EnterShop()`），同时弹出商店面板。

### 2.2 经验与升级

- 击杀敌人喷出 `materialDrop` 个 Material 碎块；每点造成的伤害都喷一个 XP 碎块。
- 升级所需 XP：[Brotato3DPlayerSystem.cpp:381](../../../src/Application/Game/Brotato3D/Brotato3DPlayerSystem.cpp)
  ```cpp
  return 20 + level * 10 + level * level * 2;
  ```
- 每次升级触发 `EAppState::LevelUpPicking`，从 `upgrades.json` 按权重抽 3 张卡。

### 2.3 玩家属性合并

最终值 = `角色 startStats` + 升级卡累加 + 被动 Item 加成 + 动态 buff（低血怒等）。
合并函数 `GetEffectiveStats()` 见 [Brotato3DShopSystem.cpp:206](../../../src/Application/Game/Brotato3D/Brotato3DShopSystem.cpp)。

| Stat 字段 | 含义 | 修改方 |
| --- | --- | --- |
| `maxHpFlat` / `maxHpFlatBonus` | 基础 HP / 累加 HP | character / `+20 Max HP` 卡 |
| `damagePct` `damageFlat` | 伤害百分比 / 平加 | character / 升级卡 / item |
| `atkSpeedPct` | 攻速 | 升级卡 |
| `rangePct` | 射程 | character / 升级卡 |
| `moveSpeedPct` | 移速 | character / 升级卡 / `speed_boots` |
| `pickupRadiusPct` | 拾取半径 | 升级卡 / `magnet` |
| `critChancePct` `critMultiplier` | 暴击率 / 暴击倍率 | character / 升级卡 |
| `dashChargeBonus` | Dash 豆数 | `spare_capacitor` |

伤害计算（[Brotato3DProjectileSystem.cpp:378](../../../src/Application/Game/Brotato3D/Brotato3DProjectileSystem.cpp)）：

```
damage = round(weapon.damage * (1 + damagePct) + damageFlat)
crit   = round(damage * critMultiplier) when rng < (critChancePct + weapon.critChanceBonus)
```

---

## 3. 配置实战 —— 怎样改数值 / 加内容

### 3.1 新增 / 调整一个敌人

文件：[assets/configs/brotato3d/enemies.json](../../../assets/configs/brotato3d/enemies.json)

**最小化模板（追击型）**：

```json
"slime": {
  "name": "Slime",
  "hp": 22,
  "moveSpeed": 1.8,
  "contactDamage": 4,
  "size": [0.50, 0.40, 0.50],
  "color": [0.40, 0.85, 0.55],
  "xpDrop": 1,
  "materialDrop": 1
}
```

**赋予 AI 子能力**：在该敌人 JSON 上加可选块。可以多块组合，但同一帧 `mortar` 与 `lance` 互斥（只走第一个 enabled 的自定义 AI）。

| 块 | 字段 | 含义 |
| --- | --- | --- |
| `ranged` | `projectileDamage`, `projectileSpeed`, `projectileLifetimeMs`, `projectileColor`, `projectileSize`, `fireIntervalMs`, `preferredDistance` | 远程射手；会 kite 维持 `preferredDistance` 距离 |
| `charge` | `triggerDistance`, `chargeSpeedMult`, `chargeRampSec`, `contactDamageMult`, `cooldownMs` | 进距离后冲撞，加速 ramp 后撞玩家造高额伤害 |
| `bomb` | `triggerDistance`, `fuseMs`, `explosionRadius`, `explosionDamage` | 自爆怪：进距离引信启动，到时爆炸（自身死亡） |
| `heal` | `radiusMeters`, `healAmount`, `intervalMs` | 治疗法师：周期性治疗最近的低血友军 |
| `mortar` | `fireIntervalMs`, `telegraphMs`, `explosionRadius`, `explosionDamage`, `throwRangeMin`, `throwRangeMax`, `lobHeightMeters`, `leadFactor` | 迫击炮：在 `[min, max]` 距离里隔段时间预瞄玩家落点 |
| `lance` | `telegraphMs`, `windupRangeMin`, `dashSpeed`, `dashDistanceMax`, `dashContactDamageMult`, `recoverMs`, `cooldownMs` | 长枪冲锋：进 `windupRangeMin`，预警条→冲刺→恢复 |
| `boss` | `phase2HpRatio`, `phase2MoveSpeedMult`, `phase2ContactDamageMult` | Boss 二阶段倍率，HP 降到比例时切换 |

**实例：迫击炮坦克（已存在）**

```json
"mortar_tank": {
  "name": "Mortar", "hp": 360, "moveSpeed": 0.9, "contactDamage": 8,
  "size": [1.0, 1.1, 1.0], "color": [0.30, 0.30, 0.42],
  "xpDrop": 8, "materialDrop": 5,
  "kitingDistance": 7.5,
  "mortar": {
    "fireIntervalMs": 3500, "telegraphMs": 1200,
    "explosionRadius": 1.8, "explosionDamage": 28,
    "throwRangeMin": 4.0, "throwRangeMax": 14.0,
    "lobHeightMeters": 4.0, "leadFactor": 0.35
  }
}
```

**让新敌人出场**：在 [waves.json](../../../assets/configs/brotato3d/waves.json) 对应波次的 `spawns` 数组里加 `{"enemyId":"slime","count":N,"intervalMs":M}`。

**视觉**：当前所有敌人都是程序化盒子，模型是按 `size` 自动生成的（[Brotato3DEffectSystem.cpp:239](../../../src/Application/Game/Brotato3D/Brotato3DEffectSystem.cpp)）。颜色取自 `color`，并自动派生 4 个材质：基础 / 暗化 / 命中白闪 / 红色警告 / Boss 二阶段红。**不需要**自己写材质代码。

**HUD 图标**（可选）：放置 `assets/textures/brotato3d/icons/enemies/<enemyId>.png`。文件不存在不会报错，只是 HUD 不显示。

### 3.2 新增 / 调整一把武器

文件：[assets/configs/brotato3d/weapons.json](../../../assets/configs/brotato3d/weapons.json)

**字段速查**：

| 字段 | 含义 | 备注 |
| --- | --- | --- |
| `damage` | 单发伤害 | 受 `damagePct` `damageFlat` 加成 |
| `atkSpeedHz` | 每秒射击数 | 受 `atkSpeedPct` 加成 |
| `rangeMeters` | 最大锁敌距离 | 受 `rangePct` 加成 |
| `projectileSpeed` | 子弹速度 m/s | |
| `projectileLifetimeMs` | 子弹生命周期 | |
| `projectileColor` `projectileSize` | 视觉 | |
| `pellets` | 单次发射弹丸数 | 霰弹 = 5 |
| `spreadDeg` | 扇形角度 | |
| `pierceCount` | 穿透次数 | 0 = 不穿透 |
| `explosionRadius` `explosionDamage` | AoE 爆炸 | 命中后或抵达终点时触发 |
| `instantHit` | 瞬发激光 | true 时无子弹，绘制 `beamWidth` 宽、`beamDurationMs` 时长的激光段 |
| `beamWidth` `beamDurationMs` | 激光视觉 | 仅 `instantHit` 用 |
| `critChanceBonus` | 武器固有暴击率加成 | 与玩家 `critChancePct` 累加 |
| `knockbackMeters` | 击退距离（米） | Boss 自动 ×0.25，大型怪按尺寸缩减 |
| `tier` | 武器等级 | 始终是 1，2 级由商店购买重复 1 级武器自动合并产生 |

**Tier 升级公式**（[Brotato3DProjectileSystem.cpp:410](../../../src/Application/Game/Brotato3D/Brotato3DProjectileSystem.cpp)，硬编码）：
- `damage * 1.5`
- `atkSpeedHz * 1.2`
- `knockbackMeters * 1.15`
- 名字加 ★

商店里购买重复的 1 级武器即合并；玩家最多 6 把武器。商店武器的卡价格写死在 [Brotato3DShop.cpp:46](../../../src/Application/Game/Brotato3D/Brotato3DShop.cpp)（smg/shotgun=8、sniper/laser=14、其它=18）。**这是非配置数据**，新增武器请同步修改这段 if/else。

**实例：等离子手枪**

```json
"plasma": {
  "name": "Plasma",
  "damage": 12,
  "atkSpeedHz": 2.5,
  "rangeMeters": 9.0,
  "projectileSpeed": 22.0,
  "projectileLifetimeMs": 600,
  "projectileColor": [0.55, 0.85, 1.0],
  "projectileSize": 0.10,
  "knockbackMeters": 0.18,
  "pierceCount": 1
}
```

**音频**：[Brotato3DAudio.hpp:42](../../../src/Application/Game/Brotato3D/Brotato3DAudio.hpp) `PlayWeaponFireSfx()` 是一个写死的 if-chain。新增武器请加一个分支或 fallback 到 SMG 音效。

### 3.3 新增 / 调整一个角色

文件：[assets/configs/brotato3d/characters.json](../../../assets/configs/brotato3d/characters.json)

```json
{
  "id": "rogue",
  "name": "Rogue",
  "tagline": "高暴击短射程",
  "color": [0.95, 0.85, 0.30],
  "startWeapon": "sniper",
  "startStats": {
    "maxHpFlat": 40,
    "atkSpeedPct": 0.10,
    "critChancePct": 0.15,
    "rangePct": -0.10
  }
}
```

`startStats` 字段集合见 [`ReadPlayerStats`](../../../src/Application/Game/Brotato3D/Brotato3DDataLoader.cpp)。`color` 同时用于角色模型主体材质、Dash 拖尾、玩家受伤碎块色（[Brotato3DPlayerSystem.cpp:555](../../../src/Application/Game/Brotato3D/Brotato3DPlayerSystem.cpp)）。

可选：`assets/textures/brotato3d/icons/characters/<id>.png` 用于角色选择面板。

### 3.4 新增升级卡（每级 3 选 1）

文件：[assets/configs/brotato3d/upgrades.json](../../../assets/configs/brotato3d/upgrades.json)

```json
{"id":"crit_chance","name":"+8% Crit Chance","stat":"critChancePct","delta":0.08,"weight":2}
```

`stat` 必须匹配 [Brotato3DShopSystem.cpp:99](../../../src/Application/Game/Brotato3D/Brotato3DShopSystem.cpp) `ApplyShopItem()` 的分支之一：`damagePct / damageFlat / atkSpeedPct / rangePct / moveSpeedPct / pickupRadiusPct / critChancePct / critMultiplier / maxHpFlat / healPct`。如果用了不在列表里的字段，**不会生效也不会报错**。

`weight` 是抽卡权重；想做"罕见卡"就把权重调到 1，常见卡是 3。

### 3.5 新增商店属性卡（数值类）

文件：[shop_items.json](../../../assets/configs/brotato3d/shop_items.json)。结构和升级卡几乎相同，多了 `cost`（材料价）。同样必须使用 `ApplyShopItem` 列表里的 stat 字段。

### 3.6 新增商店被动 Item（最多持有 6 个）

文件：[items.json](../../../assets/configs/brotato3d/items.json)

**触发器/效果（trigger / effect）**当前**白名单**——必须二者都在表里，否则 item 不生效：

| `trigger` | `effect` | 含义 | 字段 |
| --- | --- | --- | --- |
| `passive_stat` | `stat_pickupRadiusPct` `stat_moveSpeedPct` `stat_damagePct` `stat_atkSpeedPct` `stat_rangePct` `stat_critChancePct` `stat_dashCharges` | 永久属性 | `value` |
| `on_kill` | `heal` | 击杀回血 | `value` = HP |
| `on_kill_chance` | `explosion` | 击杀几率小爆炸 | `value` = 概率, `explosionRadius`, `explosionDamage` |
| `on_tick` | `heal_per_sec` | 周期回血（按秒累加，floor 取整） | `value` = HP/秒 |
| `low_hp_buff` | `stat_damagePct` | 低于 HP 阈值时加伤害 | `value`, `threshold` (0~1) |
| `on_dash_end` | `dash_knockback` | 冲刺结束推开附近敌人 | `value`, `explosionRadius`, `explosionDamage` |

> **加新 trigger/effect 必须改 C++**：[Brotato3DCombatSystem.cpp:42-150](../../../src/Application/Game/Brotato3D/Brotato3DCombatSystem.cpp) 和 [Brotato3DShopSystem.cpp:168 ApplyPassiveItemStats](../../../src/Application/Game/Brotato3D/Brotato3DShopSystem.cpp)。

**实例：闪电护符（命中几率麻痹）—— 需要 C++ 支持**

```json
{
  "id": "lightning_charm",
  "name": "闪电护符",
  "description": "命中有 10% 概率麻痹敌人",
  "trigger": "on_hit_chance",      // 新 trigger，需要在 C++ 注册
  "effect": "stun",                // 新 effect，需要写处理逻辑
  "value": 0.1,
  "rarity": "rare",
  "weight": 1,
  "cost": 28
}
```

如果只是改数值（如把吸血鬼之牙的回血从 1 改成 2），改 JSON 即可。

### 3.7 新增 / 调整波次

文件：[waves.json](../../../assets/configs/brotato3d/waves.json)

```json
{
  "durationSec": 45,                  // 主战斗时长
  "bgmCue": "battle",                 // calm / battle / boss
  "duskSpawnMultiplier": 2.5,         // 黄昏期刷怪密度倍率
  "duskBonusXpMult": 1.6,             // 黄昏期 XP 倍率（当前未实际乘进 XP，预留）
  "extractionRequiredSec": 3.0,       // 撤离车停留时间；0 表示无 dusk 阶段（boss 波）
  "extractionRadiusM": 2.5,           // 撤离车有效半径
  "spawns": [
    {"enemyId":"rat",  "count":150, "intervalMs":400},
    {"enemyId":"tank", "count":20,  "intervalMs":4000}
  ]
}
```

刷怪间隔会**随波次进度从 ×1.15 逼近 ×0.45**（写死在 [Brotato3DWaveSystem.cpp:9](../../../src/Application/Game/Brotato3D/Brotato3DWaveSystem.cpp) `SpawnIntervalScaleStart/End`），所以 `intervalMs` 是"开局值"，越往后会越快。

最末波是 Boss 波（`bgmCue="boss"`），直接跳过 DuskSurge，杀光 boss 即胜利。

### 3.8 新增 / 调整竞技场

文件：[arenas.json](../../../assets/configs/brotato3d/arenas.json)

`scene`: 固定 SCAD 场景路径，当前场景位于 `assets/scad/brotato3d/`，统一复用 `kit_deadly.scad` 末日素材。
`halfExtent`: `[halfX, halfZ]`，玩家和敌人的活动范围；SCAD 布景可以延伸到范围外作为远景。
`baseGroundColor` / `borderColor`: 角色选择界面的场景色板，不参与生成场景几何。
HUD 中的相机始终俯视玩家头顶，跟随有一个 `CameraFollowSharpness=8.0` 的 lerp（[Brotato3DGameInstance.cpp:26](../../../src/Application/Game/Brotato3D/Brotato3DGameInstance.cpp)）。

> 角色选择界面可在小型城镇、公路郊外、沙漠荒野之间切换，选择后会立即加载对应 SCAD 场景。

---

## 4. 资产组织

### 4.1 官方资产与占位资产

[Brotato3DAssetPaths.hpp](../../../src/Application/Game/Brotato3D/Brotato3DAssetPaths.hpp) 分两类：

- **`Brotato3D::Assets`（官方，可分发）**：SFX 与 UI 图标，落在 `assets/sounds/brotato3d/` 与 `assets/textures/brotato3d/`，打包进 `assets/paks/brotato3d.pak`（重建见 [`tools/brotato3d-pak/README.md`](../../../tools/brotato3d-pak/README.md)）。返回的是工程根相对路径，由引擎 package file system 负责 pak 查找与磁盘 fallback。
- **`Brotato3D::PlaceholderAssets`（仍待替换，不可分发）**：BGM / 字体 / HUD·菜单图，仍来自 **Brotato 原版引用资源** `assets/_placeholder/brotato/`，仅供本地开发，**严禁随版本分发**。`Resolve()` 支持运行时根目录与仓库根目录两种 fallback；缺失资源静默跳过（字体会 fallback 到引擎自带字体）。

启动日志若出现 `[PLACEHOLDER ASSETS] Brotato vendor reference assets detected — DO NOT DISTRIBUTE`，说明检测到占位素材，打包前请移除 `assets/_placeholder/brotato/`。

### 4.2 资产路径 API

| API | 路径模板 | 类别 |
| --- | --- | --- |
| `Assets::Sfx("xxx.wav")` | `assets/sounds/brotato3d/sfx/xxx.wav` | 官方（pak） |
| `Assets::Icon("enemies", "rat")` | `assets/textures/brotato3d/icons/enemies/rat.png` | 官方（pak） |
| `PlaceholderAssets::Bgm("battle.mp3")` | `assets/_placeholder/brotato/audio/bgm/battle.mp3` | 占位 |
| `PlaceholderAssets::Font("xxx.ttf")` | `assets/_placeholder/brotato/fonts/xxx.ttf` | 占位 |
| `PlaceholderAssets::Hud/Menu("xxx.png")` | `assets/_placeholder/brotato/ui/hud · menu/...` | 占位 |

### 4.3 关键音效约定

[Brotato3DAudio.hpp](../../../src/Application/Game/Brotato3D/Brotato3DAudio.hpp) 是**唯一**音频入口；UI / 玩法代码只调用 `PlayWeaponFireSfx`、`PlayHitSfx`、`PlayShopBuySfx` 等 inline 函数。新增武器/敌人想加音效，扩这个文件即可。

| 函数 | 候选文件名前缀 |
| --- | --- |
| `PlayWeaponFireSfx(weaponId)` | `fire_<weaponId>_NN.wav`（多个则随机） |
| `PlayHitSfx(damage, isCrit)` | `hit_normal_NN.wav` / `hit_crit_NN.wav` |
| `PlayEnemyDeathSfx(name)` | `enemy_die_boss/tank/small_NN.wav` |
| `PlayPickupXpSfx` `PlayPickupMaterialSfx` | `pickup_xp_NN.ogg` / `pickup_material.wav` |
| `StartBgm("calm/battle/boss")` | `bgm_<cue>.mp3` |

`MasterDifficulty` (默认 1.0) 是个全局难度乘子（写在 [Brotato3DAudio.hpp:16](../../../src/Application/Game/Brotato3D/Brotato3DAudio.hpp)，但作用于敌人接触/迫击炮伤害，[Brotato3DEnemySystem.cpp:301](../../../src/Application/Game/Brotato3D/Brotato3DEnemySystem.cpp))。可作为 cheat / 难度模式开关。

---

## 5. 输入与调试

| 操作 | 键盘 | 手柄 |
| --- | --- | --- |
| 移动 | WASD | 左摇杆 |
| Dash | Shift | X / 西方向键 |
| 暂停 | Esc | Start |
| Debug 召唤一只 rat | K（仅 `DEV_MODE`） | — |
| Debug 切单一武器 | 1~6（仅 `DEV_MODE`） | — |

`DEV_MODE` 是 CMake 编译期宏。Release 包不含这些快捷键。

---

## 6. Best Record（存档）

[Brotato3DGameInstance.cpp:312](../../../src/Application/Game/Brotato3D/Brotato3DGameInstance.cpp) `LoadBestRecord/SaveBestRecord` 写到平台 user dir：

```
<UserPaths>/Brotato3D/best.json
```

字段：`totalWins`, `totalKills`, `fastestCompletionSec`, `characterWins{id:count}`。新增角色无需迁移逻辑——map 自动累加。

---

## 7. 常见调改 cheatsheet

| 想做的事 | 怎么做 |
| --- | --- |
| 把一个怪强一点 | enemies.json 的 `hp` / `contactDamage` |
| 让升级更慢 | Brotato3DPlayerSystem.cpp 的 `GetXpToNextLevel()`（公式硬编码） |
| 调商店 reroll 价格 | Brotato3DShop.hpp `GetRerollCost()`：`2 + waveIndex`（硬编码） |
| 调武器合并阈值 | [Brotato3DProjectileSystem.cpp:466 TryMergeWeapons](../../../src/Application/Game/Brotato3D/Brotato3DProjectileSystem.cpp)（写死 2 → 1 级合并） |
| 调 Dash 距离 / 冷却 | [Brotato3DPlayerSystem.cpp:14](../../../src/Application/Game/Brotato3D/Brotato3DPlayerSystem.cpp) 三个 `PlayerDashXxx` 常量 |
| 调玩家最多持有 Item 数 | [Brotato3DGameInstance.cpp:393](../../../src/Application/Game/Brotato3D/Brotato3DGameInstance.cpp)、`BuyPassiveItem` 中的 `>= 6` 字面量 |
| 调武器槽数上限 | [Brotato3DCommon.hpp:22 MaxWeaponSlots](../../../src/Application/Game/Brotato3D/Brotato3DCommon.hpp) |
| 调撤离车驻留时间 | waves.json 的 `extractionRequiredSec` |
| 调暴击基础值 | [Brotato3DPlayer.hpp:23](../../../src/Application/Game/Brotato3D/Brotato3DPlayer.hpp) `critChancePct=0.05f, critMultiplier=2.0f` |
| 关掉 Boss 击杀慢动作 | [Brotato3DGameInstance.cpp:159-167](../../../src/Application/Game/Brotato3D/Brotato3DGameInstance.cpp) `timeScaleRecoveryMs_` 段 |

---

## 8. 改完之后怎么验证

1. **构建**：`./gnb.bat build Brotato3D --reconfigure`（Windows）/ `./gnb build Brotato3D --reconfigure`。
2. **跑**：`./gnb run Brotato3D`，看 log `uploaded scene [...] to gpu` 表示初始化通过。
3. **配置出错**：`Brotato3D failed to load required data` 抛在 `OnInit()`，常见原因是 JSON 字段缺失（敌人缺 `hp`、武器缺 `damage` 等，必填字段在 [Brotato3DDataLoader.cpp:108](../../../src/Application/Game/Brotato3D/Brotato3DDataLoader.cpp) 校验）。
4. **跑数值平衡**：用 `DEV_MODE` 的 K 键和 1~6 键一边玩一边喷怪 + 切武器，看 TTK / 受伤量。
5. **波次推进卡住**：检查 `extractionRequiredSec > 0` 才会进入 DuskSurge；boss 波必须 `bgmCue="boss"`。

---

## 9. 还没做的扩展点（开发者注意）

- **武器商店价**写死在 [Brotato3DShop.cpp:46](../../../src/Application/Game/Brotato3D/Brotato3DShop.cpp) —— 想做"贵重武器"得抽进 `weapons.json` 的字段。
- **`duskBonusXpMult`** 字段已加载但还没乘进任何地方（预留）。
- **场景选择**只有"绿野"会被默认应用。
- **新 trigger/effect** 必须改 C++（见 §3.6）。
- **外部图标**只在 HUD 命中槽位有 fallback；新增 weapon/character/item 不放 PNG 也能玩，UI 只是空白。

---

## 10. 进一步阅读

- [introduction.md](introduction.md) — 项目定位与系统概览
- [`AGENT_GUIDE/Brotato3D.md`](../../../AGENT_GUIDE/Brotato3D.md) — Brotato3D 代码结构梳理（god-class + 子系统拆分、对象池、数据模型）
- 引擎层文档：[`AGENT_GUIDE/`](../../../AGENT_GUIDE/) 与 [`AGENTS.md`](../../../AGENTS.md)
