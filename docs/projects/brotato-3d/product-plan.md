# Brotato 3D — 产品化（Phase 2）开发计划

## Context

MVP（M1–M9）已完成并通过人工验收，现在把项目从「能跑通的原型」推到「可向人展示的 demo 级产品」。本计划的产品定义不是 Steam 上线（那需要存档/成就/云同步/本地化等另一轮投入），而是 **itch.io demo 水准**：能完整玩 15–25 分钟一局、有内容厚度、有"枪械爽快感"、看上去像一个真游戏。

> 本计划的前提：MVP 计划见 [plan.md](plan.md)，所有 M1–M9 已完成。

## 当前状态盘点（截至 Phase 2 开始）

**MVP 已实现（来自 `src/Application/Brotato3D/`）**：
- 主循环：5 波 × 30s + 商店阶段 + 升级/死亡/胜利结算 + 重开
- 角色：1 个固定起手（50HP，SMG 一把）
- 敌人：rat（小怪）/ Brute（坦克）/ Spitter（绿色，但**只有近战接触伤害，远程未实现**）
- 武器：SMG（自动）+ Shotgun（5 pellet 散射，已实装）
- 拾取：XP 球 / 材料球 + 磁吸
- 升级卡：6 张 stat 卡（伤害/攻速/移速/HP/射程/拾取半径）
- 商店卡：5 张 stat 卡 + reroll 经济
- HUD：HP/XP/Wave 倒计时/材料/武器槽 cooldown / 敌人头顶血条 / 飘字
- 抛光：屏幕震动、击中红闪、hit-stop（80ms）、impact 碎片池、死亡淡出

**MVP 留下的窟窿（产品化需要先补）**：
1. `Spitter.kitingDistance = 4.5` 在 JSON 里有，**代码里没有任何使用** — 该敌人的差异化未兑现
2. 玩家从未承受过远程攻击，所以「躲子弹」这个吸血鬼幸存者核心玩法元素没有
3. 5 波 = 约 2.5 分钟一局（含商店 < 4 分钟），低于产品级游戏期望（Brotato 原版 20 波 ~10 分钟）
4. 没有 Boss，没有"高潮时刻"，过关感稀薄
5. 1 角色 1 把起手武器 → 重玩动机弱
6. 没有主菜单 / 暂停菜单 / 设置 — 进程一启动直接进战斗
7. 武器开火"无声、无烟"——只有子弹球飞出，不像枪
8. `Brotato3DGameInstance.cpp` 已经 1201 行，继续加东西会成炮架

**用户决策**（已确认）：
- 范围：内容厚度 + 枪感 + Meta 框架（**不**做存档/成就/网络/移动端适配）
- 视觉：继续 ProcModel 几何体路线（不引美术资产），但用动态点光源 + ImGui 特效叠加把"枪感"做出来
- 数据：继续 JSON 配置驱动
- 文档：本计划存到 `docs/projects/brotato-3d/product-plan.md`

## 产品化目标（验收线）

完成 Phase 2 后，端到端体验应该达到：

1. **启动后看到主菜单**（不再是直接进战场）
2. **可选 3 个角色**，每个起手武器/属性偏置不同
3. **10 波 + 第 10 波 Boss**，普通水平玩家通关时长 12–18 分钟
4. **6+ 种敌人**，至少 2 种远程
5. **6 种武器**（4 个新增），每把视觉/手感差异明显
6. **被动 item 系统**（与 stat 卡分开）
7. **武器 tier 合成**（同武器 3 把可升 tier 2）
8. **暴击系统** + 升级卡覆盖 critChance / critDamage
9. **开火有 muzzle flash + 动态点光源 + 子弹 trail + 命中飞溅**
10. **音频对接**：开火/命中/拾取/升级/波次开始/Boss 出场/死亡/胜利
11. **Esc 暂停菜单** + 简易设置（音量/震动）
12. **Wave 开始横幅**「WAVE 3 / 10」大字 1.2 s
13. **GameInstance.cpp 拆为多个 system**（每个文件 < 500 行）

## 引擎已有的关键能力（产品化阶段直接复用）

| 需求 | 复用 | 文件路径 |
|---|---|---|
| 音频播放 | `engine_->PlaySound(path, loop, volume)` | [src/Runtime/Subsystems/NextAudio.h](../../../src/Runtime/Subsystems/NextAudio.h) |
| 音频节流模式 | KongLie3D `PlayKongLieSfx` 的「per-sound 最小间隔」实现 | [src/Application/KongLie3D/KongLie3DAudio.hpp](../../../src/Application/KongLie3D/KongLie3DAudio.hpp) |
| 动态点光源 | `Assets::LightObject` + `BeforeSceneRebuild` 的 lights vector | [src/Assets/Core/Scene.hpp:34](../../../src/Assets/Core/Scene.hpp), [src/Application/KongLie3D/KongLie3DBattleSystem.cpp:511](../../../src/Application/KongLie3D/KongLie3DBattleSystem.cpp) |
| 临时光源材质构造 | `FBattleSystem::EnsureTempLightMaterial` 套路 | [src/Application/KongLie3D/KongLie3DBattleSystem.cpp:1755](../../../src/Application/KongLie3D/KongLie3DBattleSystem.cpp) |
| 投射物/碎片池 | 已在 Brotato3D 中实现，参考既有结构扩展即可 | [src/Application/Brotato3D/Brotato3DProjectile.hpp](../../../src/Application/Brotato3D/Brotato3DProjectile.hpp) |
| ImGui 字体加载 | `AddFontFromFileTTF` + `GlyphRangesBuilder` | [src/Application/gkNextRenderer/gkNextRenderer.cpp:251](../../../src/Application/gkNextRenderer/gkNextRenderer.cpp) |

**仍然不引入**：物理引擎、glTF 资产、QuickJS、第三方音频库、网络。

## 任务索引（共 10 个核心 + 1 可选，~14–16h）

执行顺序遵循「内容深度 → 爽快感 → 玩法系统 → Meta 框架 → 工程整理」的产品价值递增：

| # | 阶段 | 标题 | 工时 | 依赖 |
|---|---|---|---|---|
| [P1](#p1-spitter-远程攻击--内容窟窿修补) | A 内容 | Spitter 远程攻击 + 内容窟窿修补 | ~1h | — |
| [P2](#p2-武器扩展-4-把--暴击系统) | A 内容 | 武器扩展（+4 把） + 暴击系统 | ~1.5h | — |
| [P3](#p3-敌人扩展-3-种--boss-波--扩展到-10-波) | A 内容 | 敌人扩展（+3 种） + Boss 波 + 扩展到 10 波 | ~1.5h | P1 |
| [P4](#p4-枪感升级-muzzle--trail--动态点光源--死亡爆碎) | B 枪感 | 枪感升级（muzzle / trail / 点光源 / 死亡爆碎） | ~1.5h | P2 |
| [P5](#p5-音频系统对接) | B 枪感 | 音频系统对接 | ~1h | P2, P3 |
| [P6](#p6-item-被动系统) | C 玩法 | Item 被动系统 | ~1.5h | — |
| [P7](#p7-武器-tier--商店出武器) | C 玩法 | 武器 tier 合成 + 商店出武器 | ~1h | P2, P6 |
| [P8](#p8-主菜单--选角系统3-角色) | D Meta | 主菜单 + 选角系统（3 角色） | ~2h | P2 |
| [P9](#p9-暂停菜单--设置--ux-抛光) | D Meta | 暂停菜单 + 设置 + UX 抛光 | ~1h | P5, P8 |
| [P10](#p10-gameinstance-拆系统重构) | E 工程 | GameInstance 拆系统重构 | ~1.5h | 所有上述 |
| [P11](#p11-可选-多场地--本地化--最佳记录) | E 工程 | （可选）多场地 + 本地化 + 最佳记录 | ~1.5h | P10 |

> **里程碑切分原则**：每个任务卡约束在「单 agent 单次会话能完成 + 完成后游戏仍可启动到主循环」。允许跳过某个任务（除 P1 外其他都不强卡核心循环）后续补上。

---

## P1. Spitter 远程攻击 + 内容窟窿修补

**优先级**: P0  **工时**: ~1h

### 背景

把现存的内容矛盾先补上：`spitter.kitingDistance = 4.5` 在 JSON 里挂着却没人用，Spitter 跟 Rat 实际无差异。本任务给 Spitter 加远程吐口水攻击 + 风筝行为，玩家第一次需要"躲子弹"。这是产品化最划算的一步——纯 JSON+少量 code 改动，但**显著**改变了玩法手感。

### TODO
- [ ] 在 `enemies.json` 的 spitter 项添加：
  ```json
  "ranged": {
    "projectileDamage": 5,
    "projectileSpeed": 8.0,
    "projectileLifetimeMs": 1200,
    "projectileColor": [0.30, 0.95, 0.20],
    "projectileSize": 0.18,
    "fireIntervalMs": 1800,
    "preferredDistance": 4.5
  }
  ```
- [ ] 在 `Brotato3DDataLoader.hpp` 的 `FEnemyDef` 添加可选 `ranged` 子结构 `FEnemyRangedDef { int dmg; float speed; float lifetimeMs; vec3 color; float size; float intervalMs; float preferredDistance; bool enabled; }`
- [ ] `LoadEnemies` 解析该字段（缺失时 `enabled=false`）
- [ ] 新增**敌方投射物池** `enemyProjectilePool_`（与玩家 projectile 池**分开**——伤害目标不同、视觉不同）
  - 在 `Brotato3DProjectile.hpp` 已有 `FProjectileRuntime`：扩展可选字段 `bool fromEnemy = false;` 或新建 `FEnemyProjectileRuntime` 简化结构（**推荐分开**避免误判敌我）
  - 池大小 128（敌方子弹同屏不会比玩家多）
- [ ] `FEnemyRuntime` 加字段：`float rangedFireCooldownMs = 0.0f;`
- [ ] **AI 行为修改**（在 `UpdateEnemies` 内对 spitter 类敌人）：
  - 设 `dist = distance(enemy, player)`
  - 若 `dist > def.ranged.preferredDistance + 0.5` → 朝玩家走（原行为）
  - 若 `dist < def.ranged.preferredDistance - 0.5` → 反向后退（朝玩家反方向走，速度 = `def.moveSpeed * 0.7`）
  - 否则停下（不动）
  - 无论何种状态：`rangedFireCooldownMs -= dt*1000`，<= 0 时朝玩家方向发射敌方投射物，`cooldown = def.ranged.fireIntervalMs`
- [ ] **敌方投射物更新**：
  - 直线前进；命中玩家圆判定（`dist < player.radius + projectile.radius`）
  - 命中：`player.currentHp -= projectile.damage`，触发 `damageFlashMs = 250`、屏幕震动 `screenShakeMs = 120`、紫色飘字
  - 寿命到 / 边界外：失活
- [ ] **不**实现敌方投射物之间穿透 / 反弹 / AOE，单发单命中即可

### 涉及文件
- 改：`assets/configs/brotato3d/enemies.json`
- 改：`src/Application/Brotato3D/Brotato3DDataLoader.{hpp,cpp}`
- 改：`src/Application/Brotato3D/Brotato3DEnemy.hpp`（加字段）
- 改：`src/Application/Brotato3D/Brotato3DProjectile.hpp`（敌方池结构）
- 改：`src/Application/Brotato3D/Brotato3DGameInstance.{hpp,cpp}`（敌方投射物池 + AI 分支 + 命中判定）

### 验收方法
1. 编译通过
2. wave 2 出现 spitter（绿色），它会保持距离玩家约 4.5m，每 1.8s 吐绿色弹丸
3. 玩家被命中 HP 下降、紫色飘字 `-5`、屏幕震动 + 红边闪
4. 玩家走近 spitter（< 4m）→ spitter 反向后退
5. 玩家拉开 > 5m → spitter 重新前进
6. 多 spitter 同时存在不卡顿
7. 玩家能用走位躲弹丸（弹速 8m/s 比 SMG 子弹 18m/s 慢一倍，可视化清晰可躲）

### 注意
- **不要**直接复用玩家 projectilePool — 否则命中判定容易误伤友军
- 敌方子弹 model 可以复用同一个 `CreateSphere` 但用单独的 material（绿色）
- 出枪点 = `enemy.worldPos + vec3(0, def.size.y * 0.5f, 0)`，不要从地面 0 高度发射（视觉错位）
- spitter 在 `kitingDistance` 内时**不停止攻击**（吐口水只看 cooldown，不看距离）
- 命中玩家不触发 hit-stop（hit-stop 只为玩家击杀重要敌人时保留）

---

## P2. 武器扩展（+4 把） + 暴击系统

**优先级**: P0  **工时**: ~1.5h

### 背景

武器是 Brotato 的灵魂。当前只有 SMG + Shotgun 两把，build 变化空间小。新增 4 把感觉差异明显的武器，并引入**暴击系统**——这是 ARPG 类游戏的标配 stat 轴，对应「+暴击率」「+暴击伤害」两类升级卡，让玩家有明确的属性堆叠目标。

### TODO

**新武器（在 `weapons.json` 添加）**：
- [ ] `sniper`（高伤、慢速、长射程，子弹细长）
  ```json
  "sniper": {
    "name": "Sniper", "damage": 35, "atkSpeedHz": 0.6,
    "rangeMeters": 14.0, "projectileSpeed": 36.0,
    "projectileLifetimeMs": 600, "projectileColor": [0.85, 0.95, 1.0],
    "projectileSize": 0.08, "spreadDeg": 0.0,
    "critChanceBonus": 0.20
  }
  ```
- [ ] `flamethrower`（短射程、高频率、贯穿）
  ```json
  "flamethrower": {
    "damage": 3, "atkSpeedHz": 12.0, "rangeMeters": 4.0,
    "projectileSpeed": 9.0, "projectileLifetimeMs": 350,
    "projectileColor": [1.0, 0.55, 0.10], "projectileSize": 0.16,
    "pellets": 1, "spreadDeg": 22.0,
    "pierceCount": 3
  }
  ```
- [ ] `rocket`（爆炸 AOE）
  ```json
  "rocket": {
    "damage": 20, "atkSpeedHz": 0.5, "rangeMeters": 8.0,
    "projectileSpeed": 11.0, "projectileLifetimeMs": 1500,
    "projectileColor": [1.0, 0.30, 0.10], "projectileSize": 0.22,
    "explosionRadius": 1.8, "explosionDamage": 14
  }
  ```
- [ ] `laser`（瞬时命中条）
  ```json
  "laser": {
    "damage": 14, "atkSpeedHz": 1.0, "rangeMeters": 11.0,
    "projectileColor": [0.30, 0.95, 1.0],
    "instantHit": true, "beamWidth": 0.18, "beamDurationMs": 100
  }
  ```

**FWeaponDef 扩展**：
- [ ] 加可选字段：`int pierceCount = 0;`、`float explosionRadius = 0.0f;`、`int explosionDamage = 0;`、`bool instantHit = false;`、`float beamWidth = 0.0f;`、`float beamDurationMs = 0.0f;`、`float critChanceBonus = 0.0f;`
- [ ] LoadWeapons 解析新字段（用 `value("xxx", default)`）

**Pierce（贯穿）**：
- [ ] `FProjectileRuntime` 加 `int pierceRemaining = 0;`、`std::unordered_set<size_t> hitEnemyIndices;`（避免同一个敌人多次扣血）
- [ ] 命中处理：扣血 + 加入 hitEnemyIndices；若 `--pierceRemaining < 0` 才失活；否则继续飞

**Explosion（AOE）**：
- [ ] 命中处理（rocket）：在命中位置半径 `explosionRadius` 内的所有 alive 敌人 `currentHp -= explosionDamage`，每个受击点 push 飘字
- [ ] 视觉：在爆炸点 push 一个 `FExpandingRing { worldPos, color, durationMs=300, maxRadius=explosionRadius }`，UI 层用 ImGui 画扩张圆环
- [ ] 屏幕震动：`screenShakeMs = max(screenShakeMs, 200)`
- [ ] 爆炸不伤友（玩家），不连锁（被爆炸杀死的敌人不再次爆）

**Instant Hit（激光）**：
- [ ] cooldown 到时不发射 projectile，而是即时命中：找朝向方向最近的射程内敌人，扣血、屏幕画一条 `FLaserBeam { from, to, color, durationMs=100, width }`
- [ ] UI 层投影 from/to 到屏幕，画粗线（带 alpha 渐淡）

**暴击系统**：
- [ ] `FPlayerStats` 加：`float critChancePct = 0.05f;`（基础 5%）、`float critMultiplier = 2.0f;`
- [ ] 计算最终伤害：
  ```cpp
  bool isCrit = uniform_real_distribution<>(0,1)(rng_) <
                std::clamp(stats.critChancePct + weapon.critChanceBonus, 0.0f, 1.0f);
  int dmg = baseDamage;
  if (isCrit) dmg = int(dmg * stats.critMultiplier);
  ```
- [ ] 暴击飘字用**金黄色** + 字号 ×1.4 + 文本前加 `"!"` 前缀（`"!42"`）
- [ ] `upgrades.json` 新增 2 张卡：
  - `"crit_chance"` `"+8% 暴击率"` stat=`critChancePct` delta=0.08 weight=2
  - `"crit_multi"` `"+30% 暴击伤害"` stat=`critMultiplier` delta=0.30 weight=2
- [ ] `shop_items.json` 新增对应购买项

### 涉及文件
- 改：`assets/configs/brotato3d/weapons.json`、`upgrades.json`、`shop_items.json`
- 改：`src/Application/Brotato3D/Brotato3DDataLoader.{hpp,cpp}`、`Brotato3DPlayer.hpp`（FPlayerStats 加 crit 字段）、`Brotato3DProjectile.hpp`（pierce 字段 + 激光结构）、`Brotato3DGameInstance.{hpp,cpp}`、`Brotato3DUI.cpp`（爆炸圈/激光线/暴击字号渲染）

### 验收方法
1. 编译通过
2. 用控制台 / debug 临时把起手武器换成各新武器（在 `OnInit` 注释切换），逐一验证：
   - sniper：1 颗子弹 35 伤、慢速但远射程，能秒掉 rat
   - flamethrower：高频小子弹、短射程、能贯穿打穿 3 个敌人
   - rocket：抛物落地（直线即可，不强求弹道）→ 爆炸黄圈扩张 → 范围内敌人同时掉血同时飘字
   - laser：每 1s 一道蓝白光束，瞬时命中
3. 装备 sniper 时多次击杀，金色暴击字偶尔出现（约 25% 频率：基础 5% + sniper 加成 20%）
4. 升级 `+8% 暴击率` 后频率明显增加
5. 升级 `+30% 暴击伤害` 后暴击数字明显变大

### 注意
- **不要**让 rocket 在出膛瞬间爆炸（cooldown 到了还没飞出去）— 命中判定从子弹存活 100ms 后开始
- 激光 `instantHit` 不走 projectile pool，但飘字、暴击逻辑要走同一套伤害计算
- pierce 不要无限——`pierceCount=3` 表示**多打 3 个**（共 4 个），第 5 个时失活
- 暴击使用 `std::uniform_real_distribution` 不要 `rand() % 100`（前者均匀更可靠）
- 武器槽 UI 颜色提示：让 4 把武器各自子弹颜色与 weapon 槽边框颜色一致（玩家一眼能看出哪把武器在响）

---

## P3. 敌人扩展（+3 种） + Boss 波 + 扩展到 10 波

**优先级**: P0  **工时**: ~1.5h  **依赖**: P1（敌方投射物机制）

### 背景

把游戏从 5 波 2.5 分钟扩到 10 波 + Boss 收尾，并引入 3 种新敌人差异化挑战。Boss 是产品化的"高潮节点"，10 波是产品级时长（10–18 分钟一局）。

### TODO

**新敌人（在 `enemies.json` 添加）**：
- [ ] `charger`（冲锋型）：远距离时静止，玩家进入 8m 内**冲刺**（速度 ×2.5，加速 0.6s 内完成），冲刺中接触伤害 ×2
  - 字段：`"charge": { "triggerDistance": 8.0, "chargeSpeedMult": 2.5, "chargeRampSec": 0.6, "contactDamageMult": 2.0, "cooldownMs": 4000 }`
  - 视觉：冲刺前 0.5s 颜色变红色 + 屏幕轻微 zoom in（FOV 临时 +3°，可省）
- [ ] `bomber`（爆炸自爆）：靠近玩家 < 1.5m 时进入引爆倒计时 1.0s（颜色脉冲红/白），到时自爆造成 25 伤害（圆形 2m），不留尸不掉落
  - 字段：`"bomb": { "triggerDistance": 1.5, "fuseMs": 1000, "explosionRadius": 2.0, "explosionDamage": 25 }`
  - 视觉：fuse 期间脉冲缩放（scale ∈ [0.9, 1.1]）+ 颜色闪烁
  - 注意：被打死也会取消引爆（不爆炸），所以是**可击杀的 deny 对象**
- [ ] `shaman`（治疗辅助）：在范围内的敌人每 1.5s 回血 5 点，自身脆皮（HP 25），不主动攻击玩家（仍走向玩家但慢，速度 1.0）
  - 字段：`"heal": { "radiusMeters": 4.0, "healAmount": 5, "intervalMs": 1500 }`
  - 视觉：脚下绘制紫色圆环（ImGui 屏幕投影）；治疗瞬间从 shaman 到目标画一道紫色短线（150ms）

**FEnemyDef 扩展**：
- [ ] 加可选子结构：`FCharge { triggerDistance, chargeSpeedMult, chargeRampSec, contactDamageMult, cooldownMs; bool enabled; }`、`FBomb { ... }`、`FHeal { ... }`、`FBoss { ... }`（见下）
- [ ] LoadEnemies 增量解析

**FEnemyRuntime 扩展**：
- [ ] `float chargeRampMs = 0.0f;`、`float chargeCooldownMs = 0.0f;`、`bool charging = false;`
- [ ] `float bombFuseMs = -1.0f;`（>=0 表示在引爆，到 0 触发）
- [ ] `float healIntervalMs = 0.0f;`

**AI 实现**（在 `UpdateEnemies` 中按 def 类型分支）：
- [ ] charger 逻辑见上
- [ ] bomber 逻辑见上；引爆触发同 P2 rocket 爆炸视觉 + 伤害（仅伤玩家）
- [ ] shaman：每帧 `healIntervalMs -= dt*1000`；<=0 时找最近 alive 敌人（非自身、不超 4m）→ 该敌人 `currentHp = min(maxHp, +5)`，push 治疗飘字 `+5` 紫色，画治疗线，cooldown 重置

**Boss 实现**：
- [ ] 新增 `boss_warden` 敌人，HP 800，size [1.6, 2.0, 1.6]，moveSpeed 1.2，contactDamage 18
  - 字段 `"boss": { "phase2HpRatio": 0.5, "phase2MoveSpeedMult": 1.5, "phase2ContactDamageMult": 1.3 }`
  - 字段 `"ranged"`：复用 P1 的 schema，子弹伤害 12，间隔 2200ms
  - 视觉：颜色暗红 (0.55, 0.10, 0.10)，第二阶段时材质切换为更亮红 (0.95, 0.20, 0.10)
  - 死亡：直接触发 victory（在 KillEnemy 中检查 `def->boss.enabled` → `EnterResult(false)`）
- [ ] **不**为 boss 单独实现技能 1/2/3（MVP+1，技能在后续迭代）；Boss 与玩家差异在「血厚 + 远程 + 二阶段加速 + 体型大」即可

**Wave 扩展（`waves.json`）**：
- [ ] 改为 10 波。前 5 波保持现状的难度曲线，后 5 波加入新敌人：
  - wave 6：rat 大量 + spitter 大量 + 1 charger + 1 bomber
  - wave 7：spitter + charger ×3 + 1 shaman + bomber ×3
  - wave 8：tank ×4 + shaman ×2 + charger ×4 + bomber ×4
  - wave 9：所有混编，密度最大（Brotato 称 "panic wave"）
  - wave 10：开场 rat 浪潮 5 秒，然后只 spawn 1 只 boss_warden（durationSec 60）
- [ ] Wave 表添加每波的 `bgmCue` 字段（"calm" / "battle" / "boss"，留给 P5）

**Wave intro banner**：
- [ ] 每波开始前 1.2s 屏幕中央大字 `"WAVE 6 / 10"`（白色 alpha 渐入渐出），boss 波改为 `"⚠ BOSS"` 红色（中文支持时用 `"BOSS 来袭"`）
- [ ] 这 1.2s 期间 wave timer 不开始倒计时（`waveTimeRemainingSec` 暂停）
- [ ] 字体放大 ×3.0 — 用 ImGui 已有字体 `SetWindowFontScale`

### 涉及文件
- 改：`assets/configs/brotato3d/enemies.json`、`waves.json`
- 改：`Brotato3DDataLoader.{hpp,cpp}`、`Brotato3DEnemy.hpp`、`Brotato3DGameInstance.{hpp,cpp}`、`Brotato3DWaveSystem.{hpp,cpp}`（intro 计时阶段）、`Brotato3DUI.cpp`（intro banner、shaman 治疗线、bomber 脉冲）

### 验收方法
1. 编译通过
2. 玩到 wave 6 看到红色 charger 在远处突然冲刺
3. wave 7 出现 bomber 自爆，玩家如果没躲开 HP 大量损失
4. wave 7/8 看到 shaman 给小怪回血，治疗线视觉出现
5. wave 10 开始 5s 后单只 boss 出现，HP 800，第二阶段（血量 50% 以下）颜色变亮 + 移速变快 + 攻击变猛
6. boss 死亡 → 立即胜利结算
7. 每波开始有 1.2s 横幅 `"WAVE N / 10"`
8. 整局通关流畅（中等水平玩家约 12–18 分钟）

### 注意
- charger 冲刺触发后**完全不能转向**（直线追玩家上一帧位置 0.6s），玩家可以横向闪避
- bomber 引爆触发后**走过去**（仍然移动，但不再决策），引爆只看 fuse 计时；可被击杀阻止
- shaman 治疗友军范围 4m，**不**治疗自己（避免双 shaman 互奶不死）
- boss 死亡触发胜利时，剩余 alive 敌人保留淡出动画（不要瞬时清场——视觉断裂）
- intro banner 1.2s 期间允许玩家移动 + 武器开火，**只是 wave timer 不动**（让玩家有适应时间）
- boss 波 wave timer 改为 60s 以容纳长战，超时不强制结束（直到 boss 死或玩家死）

---

## P4. 枪感升级（muzzle / trail / 点光源 / 死亡爆碎）

**优先级**: P0  **工时**: ~1.5h  **依赖**: P2

### 背景

这是用户最强调的产品差异化点：「爽快的枪械设计感觉」。当前开火 = 子弹球飞出，无烟、无光、无后坐力反馈。本任务把开火和命中包装成"屏幕在响"的视觉反馈。**所有效果用 ImGui foreground draw list + 临时 Node + 临时 PointLight 实现，不引粒子系统**。

### TODO

**Muzzle Flash（开火枪口）**：
- [ ] 在 `Brotato3DGameInstance` 加成员 `std::vector<FMuzzleFlash> muzzleFlashes_;`，结构 `{ glm::vec3 worldPos; glm::vec3 color; float lifeMs=80; float remainingMs; }`
- [ ] 武器开火时：`worldPos = player.worldPos + facingDir * 0.6f + vec3(0, 0.4, 0)`，`color = weapon.projectileColor`，push 一个 muzzle flash
- [ ] OnTick 中 `remainingMs -= dt*1000`，<=0 移除
- [ ] UI 渲染：foreground draw list 在投屏位置画一个圆 + 4 道短线（米字星状），半径随 `remainingMs/lifeMs` 衰减（开始 18px → 0），alpha 同步衰减

**子弹 Trail**：
- [ ] `FProjectileRuntime` 加 `glm::vec3 lastWorldPos = worldPos;`
- [ ] 每帧子弹 update 时记录上一帧位置，UI 层从 `lastWorldPos` 投屏画到当前位置（细线，width=2px，颜色 = projectileColor，alpha = 0.6）
- [ ] **不**保留多帧轨迹（每帧只画 1 段，不留尾巴），简单且爽——参考 Brotato 实际效果

**动态点光源（核心爽快感）**：
- [ ] 玩家身上**常驻一个点光源**（暖白色、半径 6m、强度中等）：
  - 在 `BeforeSceneRebuild` 创建 1 个 `LightObject`，跟着 player.worldPos 每帧更新
  - 让玩家在场地里像"打着手电筒"一样
- [ ] 武器开火**临时点光源**（120ms）：
  - 在玩家 muzzle 位置 spawn 一个匹配 `weapon.projectileColor` 的点光源
  - 半径 3m、衰减 quadratic
  - 用对象池 `std::vector<FTempLight> tempLightPool_`（结构含 `lightIndex` 引用 Scene 里的灯位置 + remainingMs）
  - 池大小 32（够 8 武器 4Hz 同时开火）
  - 失活时把 light intensity 设为 0（不真的删除 — 删除会触发 Scene rebuild）
- [ ] 爆炸（rocket / bomber）的临时光源：用 explosion 颜色，半径 5m，持续 250ms，强度从 max 衰减到 0
- [ ] 参考实现位置：[KongLie3DBattleSystem.cpp:1755](../../../src/Application/KongLie3D/KongLie3DBattleSystem.cpp) 的 `EnsureTempLightMaterial` + 临时光源池

**死亡爆碎升级**：
- [ ] `SpawnImpactDebris` 现在固定 3 个碎片，改为按敌人体型缩放：
  - 普通敌人（rat/spitter/charger）：3 个碎片
  - 中型（tank/shaman）：6 个碎片
  - bomber：8 个碎片 + 临时光源（橙色，180ms）
  - boss：30 个碎片 + 持续 800ms 的扩张光圈 + 屏幕大震动（screenShakeMs=600）
- [ ] 碎片初速 2–4 m/s 随机方向，重力下落（每帧 velocity.y -= 9.8 * dt）

**屏幕震动调优**：
- [ ] 现状：飘忽地用 `screenShakeMs` 实现，但震动强度未分级。改为：
  - 玩家受小伤（< 5 dmg）→ shakeIntensity = 1.0
  - 玩家受中伤（5–15 dmg）→ shakeIntensity = 2.0
  - 玩家受大伤（> 15 dmg，含 charger 冲撞 / bomber 爆炸 / boss 攻击）→ shakeIntensity = 3.5
  - 玩家击杀 boss → shakeIntensity = 5.0 持续 800ms
- [ ] 震动实现：每帧给 ImGui 主 viewport 加一个屏幕级偏移（用 `ImGui::GetMainViewport()` 不能直接改，可以在 UI 渲染时给所有 HUD window 的 SetNextWindowPos 加抖动 offset；游戏世界画面的抖动通过临时偏移 camera ModelView 实现 — 在 `OverrideRenderCamera` 内每帧加 random `vec3 * shakeIntensity * 0.05`）

**子弹拖尾消散粒子（可选省略）**：
- [ ] flamethrower 子弹失活时 push 一个小烟雾飘字（白色 `"·"` 0.4s），让贯穿轨迹有"残留感"

### 涉及文件
- 改：`Brotato3DGameInstance.{hpp,cpp}`（muzzle 池/light 池/震动分级/玩家身上灯）、`Brotato3DUI.cpp`（muzzle 渲染、子弹 trail 渲染）、`Brotato3DProjectile.hpp`（lastWorldPos 字段）

### 验收方法
1. 编译通过
2. 开火时枪口**有黄色（SMG）/橙色（rocket）/蓝白（laser/sniper）的星状闪光**，~80ms 消失
3. 子弹飞行时屏幕上能看到**短拖尾**（投影一段细线）
4. 玩家走过场地时附近敌人**确实更亮**（点光源在跟随）
5. 多武器同时开火时场景明显被多个临时光源照亮（光影变化丰富）
6. rocket 爆炸时**屏幕变橙、地面发光**（爆炸点光源）
7. boss 死亡时屏幕大震动 + 大量碎片飞溅 + 持续光圈
8. 普通敌人受击不震屏，但 charger 撞到玩家有明显震动

### 注意
- 临时光源**不能**每帧重建（Scene rebuild 开销）— 用池 + 设置 intensity=0 关闭
- 玩家身上的常驻光源不要太亮（半径 6m 强度中等），否则远处场地变得昏暗反差大
- muzzle flash 的米字星画法：在中心点画 4 条线（上、下、左、右，长度 = radius），线宽 3 px，外加一个圆心实心圆（radius/2），颜色一致
- 子弹 trail 不要画整条飞行轨迹（性能 + 视觉杂乱）— 每帧只画上一帧到当前帧的一小段
- 屏幕震动 camera 偏移用**世界空间随机偏移**（X/Z），不要 Y（俯视游戏 Y 抖动会让地面跳）
- HUD 的抖动可以省略（只在游戏世界相机加抖动），HUD 一直稳是 OK 的
- 对引擎 LightObject 不熟时**先**读 [KongLie3DBattleSystem.cpp:1755](../../../src/Application/KongLie3D/KongLie3DBattleSystem.cpp) 的 `EnsureTempLightMaterial` 完整实现再下手

---

## P5. 音频系统对接

**优先级**: P0  **工时**: ~1h  **依赖**: P2, P3

### 背景

声音是"枪感"的另一半。引擎已有 `engine_->PlaySound(path, loop, volume)`（[NextAudio.h:23](../../../src/Runtime/Subsystems/NextAudio.h)），KongLie3D 的 `KongLie3DAudio.hpp` 提供了**节流模式**（per-sound 最小间隔避免连发被淹没）— 直接照抄。

### TODO

**音频模块**：
- [ ] 新建 `src/Application/Brotato3D/Brotato3DAudio.hpp`，照搬 KongLie3D 的 `PlayKongLieSfx` 模式（per-sound 最小间隔哈希表 + volume scale）
- [ ] 提供以下 API（声明在 namespace Brotato3D）：
  ```cpp
  inline float SfxVolume = 0.7f;
  inline float MusicVolume = 0.5f;
  void PlayWeaponFireSfx(const std::string& weaponId);
  void PlayHitSfx(int damage, bool isCrit);  // 命中音（crit 用更脆音）
  void PlayPickupXpSfx();
  void PlayPickupMaterialSfx();
  void PlayLevelUpSfx();
  void PlayWaveStartSfx(int waveIndex);  // boss 波用不同音
  void PlayPlayerHurtSfx();
  void PlayEnemyDeathSfx(const std::string& enemyId);  // boss 死用单独音
  void PlayShopOpenSfx();
  void PlayShopBuySfx();
  void PlayUiClickSfx();
  void PlayVictorySfx();
  void PlayDefeatSfx();
  void StartBgm(const std::string& trackName);  // calm / battle / boss
  void StopBgm();
  ```
- [ ] BGM 实现：`engine_->PlaySound(bgmPath, true, MusicVolume)`，切换 BGM 时**不**实现淡入淡出（产品级抛光留给后续，MVP+1 期直接切）

**音频文件清单**（`assets/sounds/brotato3d/`）：
- [ ] 列出需要的 wav 文件（不需要在本任务找 / 录素材，**仅**写注释清单）：
  - `fire_smg.wav`、`fire_shotgun.wav`、`fire_sniper.wav`、`fire_flamethrower.wav`、`fire_rocket.wav`、`fire_laser.wav`
  - `hit_normal.wav`、`hit_crit.wav`
  - `pickup_xp.wav`、`pickup_material.wav`
  - `level_up.wav`
  - `wave_start.wav`、`wave_start_boss.wav`
  - `player_hurt.wav`
  - `enemy_die_small.wav`、`enemy_die_tank.wav`、`enemy_die_boss.wav`
  - `shop_open.wav`、`shop_buy.wav`、`ui_click.wav`
  - `victory.wav`、`defeat.wav`
  - `bgm_calm.wav`、`bgm_battle.wav`、`bgm_boss.wav`
- [ ] 在 `assets/sounds/brotato3d/README.md` 写下「音频清单 + 推荐 freesound.org/CC0 来源」（不实际下载，留给用户后续填）
- [ ] **音频文件缺失时不要 abort** — `PlaySound` 内部已经 graceful（仅 spdlog 一行警告）

**接入点**（在 `Brotato3DGameInstance.cpp` 各处调用）：
- [ ] 武器开火（UpdateWeapons cooldown 触发瞬间）：`PlayWeaponFireSfx(weapon.weaponId)`
- [ ] 子弹命中敌人：`PlayHitSfx(damage, isCrit)`
- [ ] 拾取 XP / Material：分别调
- [ ] 玩家升级（BeginLevelUp 第一帧）：`PlayLevelUpSfx()`
- [ ] Wave 开始 banner 触发瞬间：`PlayWaveStartSfx(currentWaveIndex)`
- [ ] 玩家受伤：`PlayPlayerHurtSfx()`
- [ ] 敌人死亡：根据 def 名字派发
- [ ] 商店开 / 商店买入 / UI 按钮点击：对应调
- [ ] 胜利 / 失败结算：对应调
- [ ] BGM：进战斗 wave 1 → `StartBgm("calm")`；wave 4 → `"battle"`；wave 10 → `"boss"`；victory/defeat 后 `StopBgm`

**音量管理**：
- [ ] `Brotato3DAudio.hpp` 暴露 `SfxVolume`、`MusicVolume` 全局变量
- [ ] P9 的设置面板会调整这两个变量

### 涉及文件
- 新建：`src/Application/Brotato3D/Brotato3DAudio.hpp`、`assets/sounds/brotato3d/README.md`
- 改：`Brotato3DGameInstance.cpp`（多处 sfx 调用）、`Brotato3DWaveSystem.{hpp,cpp}`（暴露事件钩子让 GameInstance 拿到 wave 切换通知，**已有** `ConsumeWaveEnded/ConsumeIntermissionStarted` 可直接用）

### 验收方法
1. 编译通过
2. 即使没有真实 wav 文件也不崩溃，spdlog 警告但游戏正常运行
3. 用户后续放入测试音频后：
   - 开火每把武器声音明显不同
   - 命中音清脆，暴击命中音更脆
   - 拾取 XP/Material 各有"叮"声
   - 升级有提示音
   - 每波开始有"准备"声，boss 波有"咆哮"
   - BGM 在 wave 4 / 10 切换
   - 商店有"开张"声，购买有"叮当"声
   - 玩家受伤有"嗯"短哼

### 注意
- **不要**真的下载 / 嵌入 wav 文件（CR/版权/体积问题）— 仅留**接口和清单**给用户填
- 音效的最小间隔（min interval）让连发武器（flamethrower 12Hz）不被听觉淹没；推荐 60–80ms
- BGM 切换时一定要先 StopBgm 再 StartBgm，避免叠播
- ui_click 在所有按钮（升级卡 / 商店 / 主菜单 / 暂停）都接入；OnRenderUI 内**不要**对每帧 hover 都触发音效，只对 click

---

## P6. Item 被动系统

**优先级**: P1  **工时**: ~1.5h

### 背景

Brotato 的核心 build 多样性来自 **passive items**——和 stat 卡分开的独立持有物，每件有独特触发器（"击杀回血 1HP"、"拾取范围 +50%"、"每秒回血 1%"）。MVP 只有 stat 卡，build 趋同；引入 items 后玩家有"我这局走的是吸血流 / 范围爆破流 / 速攻流"的策略感。

### TODO

**数据结构**：
- [ ] 新建 `assets/configs/brotato3d/items.json`：
  ```json
  {
    "items": [
      { "id": "vampire_fang", "name": "吸血鬼之牙",
        "description": "每次击杀回 1 HP",
        "trigger": "on_kill", "effect": "heal", "value": 1,
        "rarity": "common", "weight": 3, "cost": 18 },
      { "id": "regen_charm", "name": "再生护符",
        "description": "每 2 秒回 1 HP",
        "trigger": "on_tick", "effect": "heal_per_sec", "value": 0.5,
        "rarity": "common", "weight": 3, "cost": 22 },
      { "id": "magnet", "name": "磁吸宝石",
        "description": "拾取半径 +60%",
        "trigger": "passive_stat", "effect": "stat_pickupRadiusPct", "value": 0.6,
        "rarity": "uncommon", "weight": 2, "cost": 16 },
      { "id": "fury_core", "name": "狂怒核心",
        "description": "HP 低于 50% 时伤害 +30%",
        "trigger": "low_hp_buff", "effect": "stat_damagePct", "value": 0.3, "threshold": 0.5,
        "rarity": "rare", "weight": 1, "cost": 32 },
      { "id": "shrapnel", "name": "碎片榴弹",
        "description": "击杀有 25% 概率小爆炸",
        "trigger": "on_kill_chance", "effect": "explosion", "value": 0.25,
        "explosionRadius": 1.2, "explosionDamage": 12,
        "rarity": "rare", "weight": 1, "cost": 30 },
      { "id": "speed_boots", "name": "疾行靴",
        "description": "移速 +25%（永久）",
        "trigger": "passive_stat", "effect": "stat_moveSpeedPct", "value": 0.25,
        "rarity": "uncommon", "weight": 2, "cost": 18 }
    ]
  }
  ```
- [ ] `FItemDef` struct 在 `Brotato3DDataLoader.hpp`，`LoadItems` 在 `.cpp`
- [ ] `FPlayerRuntime` 加 `std::vector<std::string> ownedItemIds;`（最多 6 件，按拥有顺序）

**触发器系统**（在 `Brotato3DGameInstance` 加 `ProcessItemTriggers(...)`）：
- [ ] `passive_stat`：购买 / 重启时一次性应用到 `player.stats.<field> += value`（注意：购买时应用，**重启游戏时**重新基于 ownedItemIds 重算）
- [ ] `on_kill`：在 `KillEnemy` 里调 `ProcessOnKillTriggers(enemy.worldPos)`，扫描 ownedItems → 应用 effect（heal +1）
- [ ] `on_tick`：在 `OnTick` 加累加器 `itemTickAccumMs_`，每秒触发一次扫描；`heal_per_sec` 按 `value` 累加 HP（小数累积，整数部分加血）
- [ ] `low_hp_buff`：每帧检查 `currentHp / maxHp < threshold`，根据当前是否触发对 stats 临时加/减 buff（用一个 `dynamicStatBuffs_` 缓冲区，在原 stats 之上叠加）
- [ ] `on_kill_chance`：`KillEnemy` 内 roll `value` 概率 → 触发 explosion（复用 P2 rocket 爆炸路径）

**商店扩展**：
- [ ] 商店 4 张卡的滚刷池**改为混合**：60% 概率出 stat 卡（来自 shop_items.json），40% 概率出 item 卡（来自 items.json，仅出未拥有的）
- [ ] item 卡视觉与 stat 卡区分：背景色按 rarity 上色（common 灰、uncommon 绿、rare 紫）
- [ ] item 售价从 `items.json` 的 `cost` 字段读

**Item Slot UI**：
- [ ] 在主 HUD 加一个 item 栏（位置：右下，6 格图标）
- [ ] 每格显示 item 名字首字（无图标资产时用文字 emoji 或 1 个字符占位，如 `"V"` for vampire）+ 背景色按 rarity
- [ ] hover tooltip 显示 description

**结算 UI 显示已拥有 items**：
- [ ] 死亡 / 胜利结算页加一行 `"Items: V / R / M"` 列出本局拥有的 items 简称

### 涉及文件
- 新建：`assets/configs/brotato3d/items.json`
- 改：`Brotato3DDataLoader.{hpp,cpp}`（FItemDef + LoadItems）、`Brotato3DPlayer.hpp`、`Brotato3DShop.{hpp,cpp}`（混合滚刷）、`Brotato3DGameInstance.{hpp,cpp}`（触发器逻辑、item slot 字段、ResetRuntimeState 重新应用 passive stats）、`Brotato3DUI.cpp`（item slot UI）

### 验收方法
1. 编译通过
2. wave 1 结束商店 4 张卡，约一半是新 item 卡（彩色背景）
3. 购买 vampire_fang 后击杀敌人 HP 实时回升 +1
4. 购买 regen_charm 后每 2s HP +1（持续）
5. 购买 magnet 后拾取范围明显扩大（XP 球从更远开始磁吸）
6. 购买 fury_core 后 HP 低于一半时伤害飘字明显变大；HP 回到一半上方又恢复
7. 购买 shrapnel 后约 1/4 击杀触发额外小爆炸，附近敌人也受伤
8. 右下角 item 栏 6 个格子，购买后亮起 + 显示首字
9. 结算 UI 列出本局 item 列表
10. 同种 item 不会被刷出第二次（已拥有时商店滚刷跳过）

### 注意
- item 是**永久持有**，不像 stat 卡一买完就消失（stat 卡数值已加到 stats，本身就消失）
- `low_hp_buff` 不要每次切换边界都重算 stats（会浮点精度问题）— 用 `dynamicStatBuffs_` 单独叠加，每帧重算（开销小）
- `on_kill_chance` 用 P2 同款 rocket 爆炸视觉，但**不**触发暴击 / 不计入 killCount 的额外击杀计数（避免 shrapnel 链反 stack overflow）
- 购买后扣 materials 同 stat 卡逻辑一致；售价按 rarity 区分（common 18 / uncommon 22-28 / rare 30-32）
- 商店滚刷的「未拥有过滤」很重要：若 4 张全是已拥有 item 会卡死 — 实现时若候选不足 4 个 item 就**用 stat 卡补足**

---

## P7. 武器 tier 合成 + 商店出武器

**优先级**: P1  **工时**: ~1h  **依赖**: P2, P6

### 背景

Brotato 的武器升级树是核心 build 路径。MVP+1 期不做完整 tier1→tier4 进化，**先做最小可行的合成机制**：同种武器拥有 3 把可合成 1 把 tier 2（伤害 ×1.5、攻速 ×1.2）。商店开始出武器卡（2/4 槽位概率）。

### TODO

**WeaponDef 扩展**：
- [ ] `FWeaponDef` 加 `int tier = 1;`（默认 1）
- [ ] tier 2 武器**不需要**单独 JSON 项 — 运行时从 tier 1 派生：
  ```cpp
  FWeaponDef CreateTier2(const FWeaponDef& base) {
      FWeaponDef t2 = base;
      t2.tier = 2;
      t2.damage = int(base.damage * 1.5f);
      t2.atkSpeedHz *= 1.2f;
      t2.name += " ★";   // 视觉提示
      return t2;
  }
  ```
- [ ] `FWeaponRuntime` 加 `int tier = 1;` + `FWeaponDef tieredDef;`（持有派生后的 def 拷贝；`def` 指针指向它，不是原 map 里的）

**武器槽扩展**：
- [ ] `equippedWeapons_` 上限从 1 提到 6（Brotato 真值），UI 同步显示 6 槽（前 N 个有内容亮显，其余暗）
- [ ] 起手 1 把 SMG 不变，其他 5 槽空

**商店武器卡**：
- [ ] 商店滚刷三类来源比例：50% stat 卡 / 30% item 卡 / 20% weapon 卡
- [ ] weapon 卡：从 `weapons.json` 全武器池随机抽，价格按武器分类：
  - 基础 (smg/shotgun)：8 材料
  - 中端 (sniper/laser)：14
  - 高端 (rocket/flamethrower)：18
- [ ] 购买 weapon 卡逻辑：
  - 若槽位有空（< 6 个）→ 直接装备到第一个空槽
  - 若已满 6 个：检查是否有同种武器
    - 已有 2 把同种 → 加这把成 3 把
    - 已有 3+ 把 → 检查能否合成（见下）
    - 否则：**禁止购买**（按钮灰显，提示 "槽位已满"）

**合成检测**：
- [ ] 每次购买后扫描：若同种 tier 1 ≥ 3 把 → 合成：消耗 3 把 → 增加 1 把 tier 2
- [ ] 合成播放特殊音 + 屏幕中央飘字 `"⚡ {weaponName} TIER 2"` 1.5s
- [ ] tier 2 不能再合（MVP+1 期不做 tier 3）

**UI**：
- [ ] 武器槽显示 6 格（4×40 + 2×40 错开 / 单行）
- [ ] tier 2 武器边框金色 / tier 1 灰色
- [ ] 同种武器在 UI 上**堆叠** + 角标 `"x2"`（避免 6 槽都被同武器占满视觉冗余 — 注意只是 UI 堆叠，逻辑上仍是 N 个槽）
  > **简化版**：如果 UI 改动太大，MVP+1 期 6 个独立槽就好，tier 2 视觉用边框金色区分即可
- [ ] hover tooltip：当前武器实际 stat（含玩家加成）

### 涉及文件
- 改：`Brotato3DWeapon.hpp`、`Brotato3DDataLoader.hpp`、`Brotato3DPlayer.hpp`（不变；equippedWeapons_ 在 GameInstance）、`Brotato3DGameInstance.{hpp,cpp}`（合成逻辑 + 槽满处理）、`Brotato3DShop.{hpp,cpp}`（weapon 卡滚刷）、`Brotato3DUI.cpp`（6 槽 UI + tier 视觉 + 合成飘字）

### 验收方法
1. 编译通过
2. wave 2-3 商店开始偶尔出现武器卡（约 1/5 概率每张）
3. 购买 1 把 shotgun → 装备到槽 2
4. 装备 2 把 SMG（含起手 1 把）+ 1 把 SMG → 合成成 1 把 SMG ★，槽 1 边框金色，名字带 ★
5. 合成时屏幕大字 `"⚡ SMG TIER 2"` 1.5s
6. 6 槽全满后买非同种武器按钮灰显，hover 显示提示
7. 6 槽全满 + 已有 3 把 SMG 时买第 4 把 SMG → 触发合成（消耗 3 把空出 2 个槽 + 新加这把 → 净占 1 槽）
8. tier 2 武器实际伤害是 tier 1 的 1.5 倍，飘字数字明显增大

### 注意
- weapon 卡价格不要太低（避免每波都能买武器 → 6 槽快速饱和），保持 8-18 材料区间
- 合成的视觉飞屏只在合成瞬间触发一次，不要每帧渲染
- 合成后 cooldown 不继承 — 新 tier 2 武器开战时 cooldown=0（爽快感）
- **不要**实现自动合成检测每帧扫描 — 只在「购买武器」事件触发后扫一次
- 商店滚刷时不要重复出已有但不可合成的武器（如已 5 槽满含 1 把 sniper，再出 sniper 玩家也无法买）— 这个边界用「过滤已满且不可合成」处理
- UI 同种武器堆叠 (`x2` 角标) 是 nice-to-have，时间紧就 6 个独立槽显示

---

## P8. 主菜单 + 选角系统（3 角色）

**优先级**: P0  **工时**: ~2h  **依赖**: P2

### 背景

让游戏看起来像产品而不是 demo：启动后**不直接进战场**，先到主菜单；点「开始」进选角，3 个角色分别有起手武器和起始 stat 偏置。死亡 / 胜利结算的「再来一局」改为「回主菜单」（但用户也可一键 retry 同角色）。

### TODO

**App State 扩展**：
- [ ] `EAppState` 加 `MainMenu`、`CharacterSelect`
- [ ] 启动默认进 `MainMenu`（在 `Brotato3DGameInstance` 构造完后 `appState_ = MainMenu`，**不**直接 `Playing`）

**角色配置**（`assets/configs/brotato3d/characters.json`）：
```json
{
  "characters": [
    {
      "id": "soldier",
      "name": "Soldier",
      "tagline": "全能战士，平衡型起手",
      "color": [0.30, 0.65, 0.90],
      "startWeapon": "smg",
      "startStats": { "maxHpFlat": 50, "atkSpeedPct": 0.0, "moveSpeedPct": 0.0 }
    },
    {
      "id": "brawler",
      "name": "Brawler",
      "tagline": "近距离爆破，高伤短射程",
      "color": [0.85, 0.30, 0.30],
      "startWeapon": "shotgun",
      "startStats": { "maxHpFlat": 70, "damagePct": 0.20, "rangePct": -0.20, "moveSpeedPct": 0.10 }
    },
    {
      "id": "marksman",
      "name": "Marksman",
      "tagline": "远距离精准，暴击专精",
      "color": [0.20, 0.80, 0.55],
      "startWeapon": "sniper",
      "startStats": { "maxHpFlat": 35, "critChancePct": 0.10, "rangePct": 0.20 }
    }
  ]
}
```

- [ ] `FCharacterDef` 在 `Brotato3DDataLoader.hpp`，`LoadCharacters` 在 cpp
- [ ] `Brotato3DGameInstance` 加成员 `std::vector<FCharacterDef> characterDefs_;` + `std::string selectedCharacterId_ = "soldier";`

**主菜单 UI**（`appState_ == MainMenu` 时画的全屏 ImGui）：
- [ ] 全屏暗色背景（半透明黑覆盖整个 viewport）
- [ ] 标题 `"BROTATO 3D"` 大字（3× 字体）居中靠上 1/4 处
- [ ] 副标题 `"幸存 10 波"` 小字（1.2×）
- [ ] 4 个按钮纵排居中：
  - `"开始游戏"` → `appState_ = CharacterSelect`
  - `"继续上次"` → 灰显（无存档系统）
  - `"设置"` → 弹出设置 modal（P9 实现，本任务先空白 modal 占位）
  - `"退出"` → `engine_->RequestExit()`（若引擎有；否则 spdlog 退出请求）
- [ ] 按钮悬停高亮 + 点击播放 ui_click（P5 已接）
- [ ] 主菜单期间游戏世界**不**渲染敌人 / 玩家（场景仍存在但玩家 Node 隐藏；地面保留作为背景）

**选角 UI**（`appState_ == CharacterSelect`）：
- [ ] 全屏背景同主菜单
- [ ] 顶部标题 `"选择角色"` + 返回按钮（→ MainMenu）
- [ ] 3 张角色卡横排（每张 250×400）：
  - 顶部：角色彩色色块（用 `character.color` 填一个 200×200 矩形，提示当前选中角色）
  - 中部：名字（大字）、tagline、起手武器、起始 stat 列表
  - 底部：`"选择"` 按钮 → 选中后 `selectedCharacterId_ = id; StartNewRun();`
- [ ] 当前选中角色卡边框高亮金色

**StartNewRun 流程**：
- [ ] 重置 runtime（复用现有 `ResetRuntimeState`）
- [ ] 应用所选角色的 `startStats` 到 `player.stats`
- [ ] 应用 `startWeapon` 到 `equippedWeapons_[0]`
- [ ] 应用角色 `color` 到玩家 body material（动态切换 material — 预创建 3 个角色色 material 在 `BeforeSceneRebuild`）
- [ ] 应用 `maxHpFlat` 到 `player.maxHp / currentHp`
- [ ] `appState_ = Playing`，启动 wave system

**结算回流**：
- [ ] 死亡 / 胜利结算 modal 的按钮改为：
  - `"再来一局（同角色）"` → `selectedCharacterId_` 不变 → StartNewRun
  - `"回主菜单"` → `appState_ = MainMenu`
- [ ] 删除 `ExitGame` 直接退出的占位（改为去主菜单 + 主菜单退出）

**世界暂停**：
- [ ] `MainMenu` / `CharacterSelect` / `Paused` 状态下 OnTick 不更新游戏逻辑，但渲染照常（地面 + 主菜单 UI 叠加）

### 涉及文件
- 新建：`assets/configs/brotato3d/characters.json`
- 改：`Brotato3DDataLoader.{hpp,cpp}`（FCharacterDef + LoadCharacters）、`Brotato3DGameInstance.{hpp,cpp}`（state machine + character apply 流程）、`Brotato3DUI.{hpp,cpp}`（RenderMainMenu / RenderCharSelect）

### 验收方法
1. 编译通过
2. 启动后**直接看到主菜单**，标题 + 4 按钮居中
3. 「设置」点击有空 modal（P9 占位 OK）；「退出」点击关闭程序
4. 「开始」→ 选角界面 3 张卡
5. 选 Soldier → 起手 SMG，玩家蓝色，HP 50
6. 死亡后选「回主菜单」→ 回到主菜单
7. 重新选 Brawler → 起手 Shotgun，玩家红色，HP 70，伤害高、射程低
8. 选 Marksman → 起手 Sniper，HP 35（脆皮）但暴击率 15%（10% 角色 + 5% 基础）
9. 选「再来一局（同角色）」→ 不回菜单，立即重启同角色
10. 主菜单 / 选角期间敌人不出现、wave 倒计时不走

### 注意
- 主菜单背景**不要**完全黑屏（产品级是有背景动画 / 立绘 / 远景，MVP+1 期保留地面 + 半透明黑覆盖即可，留视觉感）
- 角色色应用到 body material：在 `BeforeSceneRebuild` 时**预创建**3 种颜色的 player material（不要运行时动态创建材质 — 触发 Scene rebuild），切换时 `SetNodeMaterial`
- 起始 stat 偏置是**乘性叠加在基础值之上**，但 Brotato 实际是直接替换基础——本计划用「直接替换 + delta 叠加」混合：基础 50HP，Brawler `maxHpFlat: 70` 是直接替换，`damagePct: 0.20` 是 stat 上的 delta（叠加在升级卡之上）。**实现统一为 delta 叠加**最清晰，基础 stat 默认值不变
- 退出按钮没有引擎 API 时（`engine_->RequestExit()` 不一定存在），用 `std::exit(0)` 兜底（产品级用引擎 API，本任务先调 std::exit）
- 角色 color 在结算 UI 也展示（让玩家有"我用 X 角色赢了"的认同感）

---

## P9. 暂停菜单 + 设置 + UX 抛光

**优先级**: P1  **工时**: ~1h  **依赖**: P5, P8

### 背景

把"看起来像 demo 软件"和"看起来像产品游戏"的最后一公里做完：可以暂停（Esc）、可以调音量、可以看升级卡的 stat 预览。

### TODO

**Esc 暂停**：
- [ ] `OnKey` 监听 Esc：
  - `Playing` → `Paused`，弹出暂停 modal（同时记录 `previousPlayingMs_` 让 wave timer 完美暂停）
  - `Paused` → `Playing`，关 modal
  - 其他 state（菜单 / 选角 / 商店 / 升级 / 结算）按 Esc 不响应（避免误触）
- [ ] `EAppState` 加 `Paused`
- [ ] `Paused` 状态下 `OnTick` 跳过所有游戏逻辑

**Pause Modal**（300×360，居中）：
- [ ] 标题 `"游戏暂停"`
- [ ] 4 个按钮：
  - `"继续"` → `appState_ = Playing`
  - `"设置"` → 设置 modal
  - `"重新开始（同角色）"` → 调 `StartNewRun(selectedCharacterId_)`
  - `"退出到主菜单"` → `appState_ = MainMenu`
- [ ] 半透明黑覆盖游戏世界（让玩家明确游戏停了）

**Settings Modal**（占位由 P8 实现，本任务填充）：
- [ ] 滑条：`SfxVolume`（0.0–1.0，默认 0.7）
- [ ] 滑条：`MusicVolume`（0.0–1.0，默认 0.5）
- [ ] 复选框：`"启用屏幕震动"`（绑定全局 `bool ScreenShakeEnabled = true;`，UI 渲染相机偏移时若关闭则 shake=0）
- [ ] 复选框：`"显示敌人 HP 条"`（绑定全局 `bool ShowEnemyHpBars = true;`，HUD 渲染敌人 HP 条时检查）
- [ ] 滑条：`MasterDifficulty`（0.5–1.5，默认 1.0；乘到 enemies 接触伤害和远程伤害——临时简化，不动 wave spawn 数量）
- [ ] 底部 `"应用并关闭"` 按钮（**修改全局变量是即时的**，按钮关 modal）+ `"恢复默认"` 按钮
- [ ] 设置目前**不持久化**（关进程就重置）；持久化留给 P11

**Wave Start Banner（已在 P3 做了基础）**：
- [ ] 微调时长到 1.0s（产品级节奏比 1.2s 紧凑）
- [ ] boss 波 banner 加震动（屏幕轻微抖 250ms）

**升级卡 / 商店卡 tooltip**：
- [ ] hover 升级卡时下方画 tooltip：`"当前 {stat}: {curValue} → {newValue}"`
  - 例：当前 +15% 伤害，玩家已堆 25% 伤害 → tooltip 显示 `"当前 +25% → 升级后 +40%"`
- [ ] 商店卡 tooltip 同理
- [ ] item 卡 tooltip：原 description 加上「拥有后激活效果」

**Damage popup 错位**（避免飘字重叠）：
- [ ] PushFloatingText 的 worldPos.x += `random(-0.15, 0.15)`、worldPos.y += `random(0, 0.2)`，让同一帧多个飘字位置不重叠
- [ ] **不要**实现飘字队列固定栈（位置随机已够用）

**HUD 字体**：
- [ ] 在 `OnInitUI` 加大字体 `bigFont_`（参考 [gkNextRenderer.cpp:251](../../../src/Application/gkNextRenderer/gkNextRenderer.cpp)）：`AddFontFromFileTTF("assets/fonts/Roboto-BoldCondensed.ttf", 32)`
- [ ] Wave banner / boss 警告 / 升级标题 等大字位置切换到该 font
- [ ] 中文字体不在本任务做（P11 本地化阶段）

### 涉及文件
- 改：`Brotato3DGameInstance.{hpp,cpp}`（state Paused + Esc 处理 + Settings 全局变量 + ResetRuntimeState 时不重置 settings）、`Brotato3DUI.{hpp,cpp}`（RenderPauseModal / RenderSettingsModal / hover tooltip / banner 微调）

### 验收方法
1. 编译通过
2. 战斗中按 Esc → 屏幕暗 + 暂停 modal，wave 倒计时停止
3. 「继续」→ 游戏立即恢复（敌人和玩家精确同步）
4. 「设置」→ 滑条调音量实时生效（音效音量立刻变）
5. 关震动 → 玩家受伤不再震屏
6. 关 HP 条 → 敌人头顶 HP 条消失
7. 「重新开始（同角色）」→ 立即重置同角色 run
8. hover 升级卡显示 stat 预览 `"当前 X → 升级后 Y"`
9. wave banner 1.0s 节奏紧凑
10. boss banner 屏幕轻微抖动 250ms
11. 飘字位置错落不堆叠

### 注意
- Esc 在升级 modal / 商店 modal 期间**不**触发暂停（这些已经是暂停态）
- Settings 的全局变量定义在 `Brotato3DAudio.hpp` 同一 namespace 下统一管理
- tooltip 用 `ImGui::BeginTooltip` / `EndTooltip` 而非自定义窗口
- bigFont 字体加载失败时不要 abort — fallback 到默认字体（spdlog 警告）
- damage popup 错位的随机偏移用 `(rng_)` 同一份 RNG，**不要**新建 std::random_device（每次调用开销大）

---

## P10. GameInstance 拆系统重构

**优先级**: P1  **工时**: ~1.5h  **依赖**: P1–P9 完成

### 背景

`Brotato3DGameInstance.cpp` 在 MVP 已经 1201 行，过完 P1–P9 估计会到 2500–3000 行，单文件失控。本任务**纯结构重构**，不改行为，把 GameInstance 拆成各 system 类，每个文件 < 500 行。

### TODO

**目标结构**：
```
src/Application/Brotato3D/
├── Brotato3DGameInstance.{hpp,cpp}    # 仅状态机 + system 路由 + 输入分发，目标 < 400 行
├── Brotato3DPlayerSystem.{hpp,cpp}    # 玩家移动 / stat 应用 / 死亡判定
├── Brotato3DEnemySystem.{hpp,cpp}     # 敌人 spawn / AI 分支（rat/spitter/charger/bomber/shaman/boss）/ 死亡 / 池
├── Brotato3DProjectileSystem.{hpp,cpp}# 子弹 / 敌方子弹 / pierce / explosion / laser 池
├── Brotato3DPickupSystem.{hpp,cpp}    # XP / Material 池 + 磁吸 + 拾取触发
├── Brotato3DCombatSystem.{hpp,cpp}    # 伤害结算（含 crit / pierce / item triggers） + 飘字 push
├── Brotato3DEffectSystem.{hpp,cpp}    # muzzle / trail / 爆炸圈 / 临时光源 / 屏幕震动 / debris
├── Brotato3DAudio.hpp                 # 已有
├── Brotato3DArena.{hpp,cpp}           # 已有
├── Brotato3DDataLoader.{hpp,cpp}      # 已有
├── Brotato3DPlayer.hpp                # 已有
├── Brotato3DEnemy.hpp                 # 已有
├── Brotato3DProjectile.hpp            # 已有
├── Brotato3DPickup.hpp                # 已有
├── Brotato3DWeapon.{hpp,cpp}          # 武器 cooldown 推进、最近敌人查找、合成
├── Brotato3DShop.{hpp,cpp}            # 已有，加 item / weapon 滚刷
├── Brotato3DWaveSystem.{hpp,cpp}      # 已有
└── Brotato3DUI.{hpp,cpp}              # 已有，所有 UI 集中
```

**重构步骤**：
- [ ] **第一阶段：抽 PlayerSystem**
  - 把 `UpdatePlayer` / `ApplyUpgrade` / 玩家死亡判定 / Reset 玩家状态 移到 `FPlayerSystem` 类
  - GameInstance 持有 `FPlayerSystem playerSystem_`，每帧调 `playerSystem_.Update(dt, ctx)`
  - 引入 `FGameContext`（轻量 struct）传播 player ref / enemies ref / projectiles ref / engine ref / rng / settings — 让 system 不直接持有 GameInstance 指针
- [ ] **第二阶段：抽 EnemySystem**（最大块，最难）
  - 把所有 `UpdateEnemies` / spawn / AI 分支 / KillEnemy / 池 移过来
  - 接口：`Spawn(enemyId, pos)` / `Update(dt, ctx)` / `Clear(dropLoot)` / `GetEnemies()` / `KillByDamage(idx, dmg, source)`
- [ ] **第三阶段：抽 ProjectileSystem**
  - 玩家子弹 + 敌方子弹（独立 vector）
  - 接口：`SpawnPlayerProjectile(...)` / `SpawnEnemyProjectile(...)` / `Update(dt, ctx)` / `OnHitEnemy callback`
  - 命中 / 暴击 / explosion / pierce 的伤害最终通过 `ctx.combat.ApplyDamage(...)` 走 CombatSystem
- [ ] **第四阶段：抽 PickupSystem / EffectSystem**
  - 同上模式
- [ ] **第五阶段：抽 CombatSystem**
  - 集中伤害结算入口 `int ApplyDamage(target, amount, sourceTag, isCrit)`
  - 处理 hp 扣减、飘字 push、击杀触发器（item on_kill, on_kill_chance）、scoreboard
- [ ] **第六阶段：清理 GameInstance**
  - `OnTick` 变成短短一段：
    ```cpp
    if (appState_ != Playing) return;
    ctx_.dt = dt;
    playerSystem_.Update(ctx_);
    weaponSystem_.Update(ctx_);
    projectileSystem_.Update(ctx_);
    enemySystem_.Update(ctx_);
    pickupSystem_.Update(ctx_);
    effectSystem_.Update(ctx_);
    waveSystem_.Update(dt, [this](id, pos){ enemySystem_.Spawn(id, pos); });
    HandleStateTransitions();
    ```

**约束**：
- [ ] 行为零变化 — 跑回归：每个 milestone 验收点重跑一遍（如 P1 的 spitter 远程、P2 的暴击、P3 的 boss、P4 的 muzzle 等）必须全部通过
- [ ] system 之间**只**通过 `FGameContext` 通信，不互相直接持有指针
- [ ] FGameContext 字段尽量保持引用（避免拷贝），生命周期与 GameInstance 一致
- [ ] 各 system 不直接调 ImGui（UI 全集中在 `Brotato3DUI.cpp`），只暴露 getter 让 UI 读取数据

### 涉及文件
- 新建：`Brotato3DPlayerSystem.{hpp,cpp}`、`Brotato3DEnemySystem.{hpp,cpp}`、`Brotato3DProjectileSystem.{hpp,cpp}`、`Brotato3DPickupSystem.{hpp,cpp}`、`Brotato3DEffectSystem.{hpp,cpp}`、`Brotato3DCombatSystem.{hpp,cpp}`、`Brotato3DGameContext.hpp`
- 大幅缩减：`Brotato3DGameInstance.{hpp,cpp}`
- 微调：`Brotato3DUI.cpp`（getter 路径换）

### 验收方法
1. 编译通过
2. 单文件大小：每个 system 源文件 < 500 行；GameInstance.cpp < 400 行
3. 跑一遍完整通关：主菜单 → 选角 → 10 波 → boss → 胜利 → 回主菜单 — 与重构前**完全一致**
4. 关键功能回归：spitter 远程、暴击、爆炸 AOE、激光、charger 冲锋、bomber 引爆、shaman 治疗、武器合成、暂停、设置
5. clang-tidy 不引入新警告

### 注意
- **不要**在重构同时加新功能 — 单一职责
- 重构按"一个 system 一次合并"的节奏，每抽完一个就跑游戏验证一次
- FGameContext 不要变成 god object — 只放系统间通信必需的引用，绝大多数局部状态留在各 system 内部
- 如果某些函数横跨多个 system（如 SpawnPickup 既要敌人位置又要 pickup 池），就放在更上层的 system（CombatSystem 是中转好选择）
- Header inclusion：避免循环依赖，`FGameContext` 用前置声明 + 在 cpp 引完整头文件
- **本任务允许保留个别全局函数**（如 `Brotato3DAudio.hpp` 的 sfx helpers）— 它们已经天然解耦

---

## P11.（可选）多场地 + 本地化 + 最佳记录

**优先级**: P2  **工时**: ~1.5h  **依赖**: P10

### 背景

长尾抛光。3 个场地颜色主题让重玩有视觉新鲜感；中英本地化让中文用户体验完整；最佳记录给玩家追求目标。

### TODO

**多场地**：
- [ ] `assets/configs/brotato3d/arenas.json`：3 个主题
  ```json
  { "arenas": [
    { "id": "grassland", "name": "绿野",
      "groundColor": [0.32, 0.40, 0.28], "borderColor": [0.45, 0.55, 0.35] },
    { "id": "wasteland", "name": "荒地",
      "groundColor": [0.42, 0.36, 0.28], "borderColor": [0.55, 0.50, 0.40] },
    { "id": "tech_grid", "name": "数码",
      "groundColor": [0.18, 0.22, 0.30], "borderColor": [0.35, 0.55, 0.85] }
  ]}
  ```
- [ ] 选角时下方加场地选择栏（3 个色块）
- [ ] `Brotato3DArena::BuildArena` 接受场地参数构建对应配色

**中文本地化**：
- [ ] 加载中文字体：`AddFontFromFileTTF("assets/fonts/PingFang-SC.ttf"/* 或 SourceHanSans */, 24, glyphRanges=GetGlyphRangesChineseSimplifiedCommon())`
- [ ] 所有 HUD / modal 文本走 `i18n.Get(key)` 函数（中央表 `assets/configs/brotato3d/i18n.json`），key 例如 `"main_menu.start"`、`"wave.title"`、`"upgrade.crit_chance"`
- [ ] 默认中文，运行时不切（产品级再加切换）

**最佳记录存档**：
- [ ] 写到 `%APPDATA%/Brotato3D/best.json`（用 `Utilities::FileHelper::GetUserConfigDir()` 或类似 API）
- [ ] 记录：通关次数、总击杀、最快通关时间、各角色通关数
- [ ] 主菜单 + 结算 UI 显示

**平衡微调**：
- [ ] 用户实测后反馈 wave 难度曲线，调 `waves.json` / `enemies.json` 数值
- [ ] 目标通关率：中等水平 30–50%

### 涉及文件
- 新建：`assets/configs/brotato3d/arenas.json`、`i18n.json`、`assets/fonts/SourceHanSans-Bold.ttf`（用户提供）
- 改：所有相关 hpp/cpp 文本调用走 i18n

### 验收方法
1. 编译通过
2. 选角时可选 3 个场地
3. 进游戏地面颜色 / 边界颜色按选择变化
4. HUD 显示中文（标题 / 按钮 / 升级卡名 / 商店 / 结算）
5. 通关一局后退出再进 — 主菜单显示通关次数 +1
6. 平衡测试 5 局后，通关率约 30–50%

### 注意
- 中文字体会让二进制体积膨胀（~10MB） — **不**进 git；写到 `assets/fonts/.gitignore` + `README` 提示用户自行下载 CC0 字体
- 字体加载失败时**用英文 fallback**（i18n.Get 返回英文 key）
- best.json 写文件失败时静默 spdlog 警告，不要崩游戏
- arena 切换不要重建 Scene — 只换 ground / border 的 material（预创建 3 套）

---

## 公共约束（所有任务必读）

| 项 | 值 |
|---|---|
| 构建 | `./build.bat --preset full-windows --reconfigure` |
| 运行 | `./run.bat --preset full-windows --target Brotato3D` |
| 命名 | 类型/函数 PascalCase；变量/参数 camelCase；私有成员 `_` 后缀 |
| 头文件首行 | `#include "Common/CoreMinimal.hpp"` |
| 平台 | 用 `PlatformCommon.h`，不直接 include 平台头 |
| Vulkan | 所有 VkResult 用 `VK_CHECK_RESULT`；RAII |
| 注释 | 默认不写注释，仅写非显然的 WHY |
| 提交 | 不要执行 git commit；只完成代码改动，由用户决定何时提交 |
| 沟通 | 与用户用中文对话 |

**禁止**：
- 修改 `src/ThirdParty/` 或 `external/`
- 引入新大型依赖（粒子系统、SDL_mixer、Bullet 等）
- 在任务卡范围之外做"顺手清理"（除 P10 是专门的重构任务）
- 把代码写到注释里 — 删掉的代码就是删掉

**新依赖审批**：
- 本计划**不**引入新 vcpkg 包；继续用 `nlohmann-json` + `glm` + `imgui` + `spdlog` + `fmt`
- 中文字体（P11）由用户**手动**放入 assets/fonts，不进 vcpkg

## 验证完整端到端

完成 P1–P10 后，端到端走一遍验证：

1. `./build.bat --preset full-windows --reconfigure` 通过
2. `./run.bat --preset full-windows --target Brotato3D` 启动
3. **主菜单**：标题 + 4 按钮，点设置出 Settings modal，点开始进选角
4. **选角**：3 个角色卡，选 Brawler（红色）→ 起手 Shotgun
5. **Wave 1**：banner 1.0s 横幅 → BGM "calm" 起，spawn rat → 玩家移动 + 自动开火，子弹有 muzzle flash + trail + 拾取 XP/材料
6. **Wave 2**：spitter 出现远程吐口水，玩家走位躲弹丸；命中红边闪 + 屏幕震动
7. **Wave 1 结束 → 商店**：4 张卡混合 stat / item / weapon，购买 vampire_fang，开始击杀回血
8. **Wave 4**：BGM 切 "battle"，charger 冲锋出现
9. **Wave 6–9**：bomber 自爆、shaman 治疗、密度大
10. **Wave 10**：BGM 切 "boss"，开场 banner `"BOSS 来袭"` 红字 + 屏幕震，5s 后 boss 出现，HP 800
11. **Boss 战**：第二阶段（HP < 400）颜色变亮 + 移速变快 + 远程攻击变猛
12. **击杀 boss**：金色字 `"VICTORY"` + 大震动 + 大量碎片 + 持续光圈
13. **结算**：统计本局 kill / 等级 / 时间 / 拥有 items
14. **「再来一局（同角色）」** → 立即重启
15. **「回主菜单」** → 回主菜单
16. **暂停**：Esc 暂停，wave 完美停下
17. **设置**：调 SfxVolume → 立即生效；关屏震 → 不再震
18. 全程**敌人 + 子弹 + 拾取物峰值**（wave 9 panic 时）流畅 60fps，**无内存泄漏**（多次重开后内存稳定）

## 风险与备注

| 风险 | 应对 |
|---|---|
| 临时点光源池满（同时 8 武器 4Hz 开火）| pool 大小提到 64；满了就**最旧的一个被覆盖**（视觉损失低） |
| BGM 切换硬切割不舒适 | MVP+1 期接受硬切；产品级抛光做 200ms 交叉淡入需要扩展 NextAudio API（留给下下轮） |
| 中文字体体积大 | 不进 git；写 README 让用户自行下载；找不到字体时英文 fallback |
| 音频文件用户没填 | PlaySound 内部 graceful warn，不崩；在 README 列出 freesound.org CC0 关键词建议 |
| 重构（P10）破坏现有功能 | 每抽一个 system 就完整跑一次回归；不允许"先全抽完再修"模式 |
| Boss 设计单调 | MVP+1 期接受「血厚 + 远程 + 二阶段」即可，专门技能（召唤小怪/突进/AOE）留给下轮 |
| 商店滚刷池过滤后不足 4 张 | weapon / item 滚刷过滤后不足时**用 stat 卡补足**，永远保证 4 张 |
| crit + pierce + explosion 复合伤害爆栈 | 在 CombatSystem 入口加 `int sanitizedDamage = std::clamp(damage, 0, 99999)` |
| 屏幕震动晕 | 提供设置开关（P9 已设计）；震动强度上限 4px 屏幕偏移，不要更大 |
| boss wave 10 超时未死如何处理 | wave 10 `durationSec` 设 60s 但**到 0 也不强结束**（boss / 玩家先死的一方决定胜负） |

## 后续 agent 调用建议

每个任务（P1, P2, ...）适合用一个独立 agent 调用执行，prompt 模板：

```
请执行 docs/projects/brotato-3d/product-plan.md 中的 P{N} 任务。
- 严格按 TODO 清单做，不要扩大范围
- 完成后用 full-windows preset 编译验证
- 完成「验收方法」中的所有点，逐条勾选汇报
- 不要 commit
- 与用户沟通用中文
- P10 之前的任务允许直接修改 Brotato3DGameInstance.cpp（即使它会变长）；P10 才做重构
```

**推荐执行顺序**（按价值递增 + 风险隔离）：
1. P1（修补窟窿，最低风险）
2. P2（武器 + crit，建立 stat 多样性基础）
3. P3（敌人 + boss + wave 扩展，建立内容厚度）
4. P4 + P5 并行（视觉与音频，独立模块）
5. P6（item 系统，复用 P2 stat 通道）
6. P7（武器合成，依赖 P2 + 商店改造）
7. P8（主菜单 + 选角，外壳）
8. P9（暂停 + 设置 + UX 抛光）
9. P10（重构 — **必须最后**，避免重构与新功能冲突）
10. P11（可选长尾）

完成 P10 后整体复盘是否需要 P11 / 是否应该开 Phase 3（更深的内容、更多角色、武器进阶 tier 3-4、Steam 上架准备）。
