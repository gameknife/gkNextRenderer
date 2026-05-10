---
title: Brotato3D 战利品体系重构计划
status: 待开发
owner: codex
last_updated: 2026-05-09
---

# Brotato3D Loot & XP Overhaul Plan

本计划对 Brotato3D 的"碎块/掉落/经验"体系做一次有意义的重构。当前实现里击中和击杀产生的彩色碎块只是装饰，没有玩法语义；XP 和 Material 又是两套独立系统。重构目标是让所有可见碎块都成为可吸收的资源（XP 或 Material），强化"边打边捡"的紧张感，并放慢玩家成长曲线。

## 1. 设计目标

1. **取消纯装饰碎块**：命中和击杀都不再喷射"无意义"的色块。
2. **命中即给经验**：每点命中伤害向射击反方向喷出 1 个 XP 碎块（颜色统一，与角色阵营色一致）。
3. **击杀掉 Material**：杀死敌人时，按 `materialDrop` 喷出对应数量的 Material 碎块（同样可吸收）。
4. **XP 升级曲线变陡**：升级所需 XP 由当前公式（`5 + level * 4`）大幅上调，让升级与击杀强相关而非"几只小怪就升级"。
5. **碎块统一吸附**：XP 与 Material 碎块都参与磁吸（沿用 `pickupRadius`）。
6. **去描边**：XP / Material 碎块均不再显示选中描边。
7. **每波结束清零**：所有未吸附的 XP / Material 碎块在 `ConsumeWaveEnded()` 时立刻被销毁，逼玩家在战斗中及时收集。
8. **敌人 HP 微调**：略微提升常规敌人血量，让 TTK 更长一些（不要影响 Boss 节奏）。

## 2. 当前实现摘要（背景，便于改动定位）

- **Impact 碎块**（无意义装饰）
  - `Brotato3DDebrisSystem.cpp::SpawnImpactDebris(...)`，调用方在
    `Brotato3DProjectileSystem.cpp:113 / 211 / 246`（瞬发武器命中、子弹命中、爆炸 AoE）。
- **Death 碎块**（无意义装饰 + Material 碎块）
  - `Brotato3DDebrisSystem.cpp::SpawnDeathDebris` 由 `Brotato3DEnemySystem.cpp:294` 调用。
  - 函数内部一半发非 pickable Tiny/Chunk（装饰），一半发 pickable Chunk + `materialDebrisMatId_`（Material 碎块）。
  - Boss 死亡有自定义喷射逻辑（`Brotato3DEnemySystem.cpp:298-352`）——本次保留 Boss 仪式感，不动其装饰碎块逻辑，但 Material 部分要走新通道。
- **XP Pickup**
  - `Brotato3DPickupSystem.cpp` 中 `FPickupRuntime` 池，128 个独立物理体，盒子模型，使用 `RenderOutlineFlags::hovered` 描边。
  - 击杀时一次性 `SpawnPickup(enemy.def->xpDrop, EPickupKind::XP, enemy.worldPos)`。
- **Material 碎块**
  - 复用 Debris 池（`EDebrisKind::Chunk`），`pickable=true`，描边 `RenderOutlineFlags::selected`，`materialValue=1`。
  - 吸附逻辑在 `UpdateDebris` 内（`Brotato3DDebrisSystem.cpp:464-587`），claim 时 `player_.materials += slot.materialValue;`。
- **波次清理**
  - `Brotato3DGameInstance.cpp:200` `ClearAllDebris(false)` 已经会清掉所有 Debris。
  - 但 `FPickupRuntime`（XP 球）目前不在 `ConsumeWaveEnded` 路径里被清，需要补一个 `ClearAllPickups()`。
- **XP 公式**
  - `Brotato3DPlayerSystem.cpp:262 GetXpToNextLevel() => 5 + player_.level * 4;`
- **敌人配置**
  - `assets/configs/brotato3d/enemies.json`，关键字段 `hp / xpDrop / materialDrop`。

## 3. 改造方案

### 3.1 把 XP 也变成 Debris 碎块

最干净的做法：废弃 `FPickupRuntime` 系统，让 XP 走 Debris 池，避免维护两套吸附逻辑。

**FDebrisRuntime 扩展**（`Brotato3DDebris.hpp`）：

```cpp
enum class EDebrisPayload : uint8_t
{
    None = 0,
    Material = 1,
    Xp = 2,
};

struct FDebrisRuntime
{
    // ... 既有字段保留 ...
    EDebrisPayload payload = EDebrisPayload::None;
    int payloadValue = 0;   // Material -> 材料数；Xp -> 经验值（默认 1）
};
```

`materialValue` 字段建议直接替换为 `payloadValue + payload`。

**SpawnDebris 接口扩展**：把当前 `bool pickable / int materialValuePerSlot` 改为 `EDebrisPayload payload, int payloadValuePerSlot`，旧的 `pickable` 等价于 `payload != None`。

**Claim 路径**（`UpdateDebris`）：
- `payload == Material`：保持 `player_.materials += value`、`+N MAT` 浮字、PlayPickupMaterialSfx。
- `payload == Xp`：`player_.currentXp += value`，`+N XP` 浮字，PlayPickupXpSfx，并触发 `BeginLevelUp` 检查（沿用 `Brotato3DPickupSystem.cpp:226-236` 那段逻辑）。

**移除 Pickup 系统**：
- 删除 `Brotato3DPickup.hpp / Brotato3DPickup.cpp / Brotato3DPickupSystem.cpp` 中跟 `FPickupRuntime` 相关的代码。
- 删除 `BuildPickupPool / SpawnPickup / UpdatePickups`、相关成员（`pickupPool_, pickupXpModelId_, pickupXpMaterialId_`）。
- `Brotato3DGameInstance::Tick` 里去掉 `UpdatePickups(...)`，`SceneRebuild` 里去掉 `BuildPickupPool(...)`。
- 废弃 `EPickupKind`（如有别处使用，单独清理）。

> 备注：debris 池上限 `TinyDebrisCount = 600 / ChunkDebrisCount = 480` 已经足够大，但本次每发命中要喷"伤害值"个 XP 碎块，普通近战秒数百输出会瞬间填满池子。需要：
> - 对单次伤害的喷射数做上限（见 3.2）。
> - 若发现池子被打爆，给 `TinyDebrisCount` 适当调高（建议先观察实际命中频率，必要时提到 800）。

### 3.2 命中喷 XP 碎块

**调用方**：所有现在调 `SpawnImpactDebris` 的地方改调新的 `SpawnHitXpDebris(...)`。

```cpp
// 取消纯装饰命中碎块
- SpawnImpactDebris(hitPos, dir * speed, weaponColor, enemyColor, isCrit, enemyName);
+ SpawnHitXpDebris(hitPos, /*backDir=*/ -dir, /*damage=*/ actualDamage);
```

**伤害换算 → 碎块数量**：
- 计算 `int xpChunks = std::clamp(actualDamage, 1, kMaxXpChunksPerHit)`（建议 `kMaxXpChunksPerHit = 12`，避免单次大爆炸瞬间几百碎块）。
- 单次溢出的伤害不再产生额外 XP（接受这点游戏性损失换性能稳定）。
- `actualDamage` 取实际造成的伤害（在 `ApplyDamageToEnemy` 里 clamp 到剩余 HP），避免溢杀也产 XP。需要把 `ApplyDamageToEnemy` 的 effective damage 返回出来给调用方。

**喷射方向**：朝击中点的"反弹"方向，即 `-dir`（dir 为子弹/瞬发命中的飞行方向）。
- 喷射点：命中点稍微往子弹来源方向偏移（沿用现 `surfaceOffset` 思想，但减小到 0.2 米左右，避免和敌人模型穿插）。
- 锥角：保持 0.6 rad 左右。
- 速度：保持 4–6 m/s 区间，比当前 impact 慢一点便于玩家看清。

**统一颜色**：所有 XP 碎块使用同一个材质，建议是亮绿（沿用现 `pickupXpMaterialId_` 的 `(0.2, 1.0, 0.35)`，但去描边）。在 `BuildDebrisPool` 里新增 `xpDebrisMatId_`，去掉 hovered/selected 描边设定（见 3.5）。

**碎块外观**：`EDebrisKind::Tiny` 即可（XP 视觉上是"小颗粒"），保持 `TinyHalfExtent` 体积。

### 3.3 击杀喷 Material 碎块

**改造点**：
- `Brotato3DEnemySystem.cpp:294-296` 杀敌后：
  - **删除** `SpawnDeathDebris(enemy);`
  - **删除** `SpawnPickup(enemy.def->xpDrop, EPickupKind::XP, enemy.worldPos);`
  - **新增** `SpawnKillMaterialDebris(enemy);` —— 按 `enemy.def->materialDrop` 喷出对应数量的 Material 碎块，方向以"敌人指向玩家"为中心做 360° 锥形喷射（沿用 `lastHitDebrisDir` 或简单 `enemy.worldPos - player_.worldPos` 反向）。
  - XP 不再在击杀点喷出（XP 改为"打几下喷几个"；如果团队希望仍保留击杀奖励 XP，可在杀敌时额外喷 `enemy.def->xpDrop` 个 XP 碎块——在 product-plan review 时确认，本计划默认不喷）。

**Boss 处理**：
- Boss 的死亡仪式（巨型 Chunk 暴雨、屏幕震动、爆炸光环）**保留**视觉。
- Boss 现在的 `materialDrop` 喷射逻辑（`Brotato3DEnemySystem.cpp:337-345`）改走统一 `SpawnKillMaterialDebris`，参数允许传入数量倍率与喷射半径。
- Boss 装饰用的 Chunk/BossChunk 留作 `EDebrisPayload::None`、`pickable=false`、显式 `lifetimeMs`（现在是 0，落地后留场）。**为了"波末清零"一致**，给 Boss 装饰碎块设置一个 `lifetimeMs`（例如 4000ms）让其自然消失，或在 `ClearAllDebris` 里也一并清。后者更直接，建议直接清。

### 3.4 XP 曲线上调

把 `Brotato3DPlayerSystem.cpp:262` 改成更陡的曲线：

```cpp
int Brotato3DGameInstance::GetXpToNextLevel() const
{
    // 1->2: 30, 2->3: 50, 3->4: 75, 4->5: 105, ...
    const int level = std::max(1, player_.level);
    return 20 + level * 10 + (level * level) * 2;
}
```

数值是建议起点，按 playtest 调。约束：第 1 级到第 2 级控制在玩家击杀 8–12 只 Rat 的体感（按 Rat 14 HP × 1 XP/dmg ≈ 14 XP/kill 估算）。

写下数值后必须 playtest：清出 Wave 1 数据 → 看升级次数是否落在 1–2 次区间，目标是把"3 波内升 6 级"压到"3 波内升 2~3 级"。

### 3.5 去描边

`Brotato3DDebrisSystem.cpp:447`：

```cpp
- NodeUtils::SetOutlineFlags(slot.node, pickable ? Runtime::RenderOutlineFlags::selected : Runtime::RenderOutlineFlags::none);
+ NodeUtils::SetOutlineFlags(slot.node, Runtime::RenderOutlineFlags::none);
```

XP / Material 都不描边。`UpdateDebris` 里 `magnetized` 转换处的描边切换也一并删除。

### 3.6 波末清零

在 `Brotato3DGameInstance.cpp:197` `ConsumeWaveEnded` 分支里：

```cpp
if (waveSystem_.ConsumeWaveEnded())
{
    ClearAliveEnemies(false);
    ClearAllDebris(false);   // 已经会清掉 XP / Material（因为统一进 debris 池）
}
```

由于 XP 已合并到 debris 池，无需新加 `ClearAllPickups`。但要确认 `ClearAllDebris(false)` 真的会把 `pickable=true` 的也清掉（现状是会的——`keepPickable=false` 进 else 分支）。在删除 PickupSystem 时确认这条断言。

> 设计意图：玩家必须在战斗中靠近敌人/吸附范围内捡完，逼出"贪婪 vs 安全"权衡。若 playtest 中发现挫败感过强，可在 wave 结束前给 ~1.5s 的"磁吸总动员"宽限期（所有未吸附的 chunk 强制朝玩家飞过去 / 立即被收集），再清场。该宽限期作为可选优化，先按硬清实现，挫败感不可接受时再加。

### 3.7 敌人 HP 微调

修改 `assets/configs/brotato3d/enemies.json`：

| id | 当前 hp | 建议 hp | 理由 |
|---|---|---|---|
| rat | 14 | 18 | 让初始武器 1 发不秒，让 XP 喷射动起来 |
| spitter | 18 | 24 | 中距离威胁应更难压制 |
| charger | 34 | 42 | 冲刺敌人 TTK 拉长 |
| bomber | 24 | 30 | 拆弹决策窗口 |
| shaman | 25 | 32 | 群疗后让玩家有反应时间 |
| tank (Brute) | 60 | 80 | 维持精英定位 |
| boss_warden | 8000 | 8000 | **不动** |

> 这些是起点，开发时建议附带一段 5 波模拟基准（用 visual test 或脚本跑），确保 TTK 中位数从约 0.5s 提升到 0.8–1.2s。

## 4. 落地步骤（建议执行顺序）

1. **数据层先行**：调 enemies.json HP，跑 visual test 出对照截图。
2. **Debris 扩展**：FDebrisRuntime 加 payload 字段；SpawnDebris 接口替换；保留旧调用方做最小适配。
3. **命中改造**：`ApplyDamageToEnemy` 返回 effective damage；命中处把 SpawnImpactDebris 替换成 SpawnHitXpDebris；删除 SpawnImpactDebris/EnsureHitDebrisMaterial（含 enemyDefs × weaponDefs 的预生成材质表）。
4. **击杀改造**：删 SpawnDeathDebris；新增 SpawnKillMaterialDebris；接入 boss 路径。
5. **XP 池移除**：删 PickupSystem 文件 / 池构建 / Tick 调用；UpdateDebris 内补 XP claim 分支（升级判定逻辑搬迁）。
6. **去描边**：SpawnDebris 与 magnet 切换处统一 RenderOutlineFlags::none。
7. **波末清零确认**：检查 ClearAllDebris 真的清 pickable；移除任何残留的 pickup-only 清理。
8. **XP 曲线**：调 GetXpToNextLevel，跑一局看节奏。
9. **回归**：
   - `./gnb build` 通过，无编译/链接错误。
   - 进游戏 playtest：Rat 单发不死、命中飞 XP 颗粒、击杀飞黄色 Material、磁吸正常、波末瞬间清空。
   - `./out/build/<preset>/bin/gkNextVisualTest` 跑一遍，关注是否有材质引用残留。

## 5. 风险与注意事项

- **池容量爆掉**：每点伤害 1 个 XP 碎块，命中频率高时池压力大。已用 `kMaxXpChunksPerHit` 限上限；若仍不够，TinyDebrisCount 调到 800 并在日志里观察 "no free slot, evicting oldest" 类的兜底是否被触发。
- **物理体性能**：每个 debris 都是一个 Dynamic body。按经验 600 个还好，但若调高需注意 Jolt step 时间。
- **波末清零的玩家心理**：硬清是有挫败感的设计，必须在 changelog/UI 提示，避免老玩家误以为是 bug。建议 wave banner 上加一行 "拾取 XP！"提示。
- **Boss 流程兼容**：Boss 死亡现在跑了一段独立喷射代码，重构时不要破坏 BossChunk 池的使用。
- **i18n / 文案**：浮字 `+1 XP`、`+1 MAT` 已存在；不需改 i18n。

## 6. 验收标准

- 视觉上：命中敌人只看到一串 XP 颗粒朝玩家方向飞，不再有任何彩色无意义碎块；敌人死亡掉的是 Material 颗粒，无装饰碎块（Boss 除外，仪式喷射保留）。
- 玩法上：升级显著变慢；玩家会主动跑去捡 XP；波末清零，迟收集惩罚明确。
- 工程上：PickupSystem 完全删除，无遗留符号；Debris 池统一管理 XP/Material；编译警告无新增；`./gnb build --reconfigure` 通过。
