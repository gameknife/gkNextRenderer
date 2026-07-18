---
title: "Brotato3D 配置与玩法开发指南"
category: project
status: 现行
owner: Brotato3D
created: 2026-05-10
last_updated: 2026-07-17
---

# Brotato3D 配置与玩法开发指南

Brotato3D 是 C++ 原生应用。配置负责内容和大部分平衡数值，C++ 负责状态机、AI、战斗、对象池、UI 与不适合数据化的规则。不要把“配置驱动”理解成所有玩法常量都已从代码移出。

代码结构的更稳定说明见 `AGENT_GUIDE/Brotato3D.md`；字段的最终事实来源是 `Brotato3DDataLoader.hpp/.cpp` 和各消费系统。

## 配置入口

`assets/configs/brotato3d/` 当前有 9 个 JSON：

| 文件 | 内容 |
| --- | --- |
| `characters.json` | 角色、起手武器、初始属性 |
| `weapons.json` | 武器与弹丸/激光/AoE 数值 |
| `enemies.json` | 敌人基础值和可选 AI 能力块 |
| `waves.json` | 波次、刷怪、Dusk Surge、撤离参数 |
| `upgrades.json` | 升级三选一卡池 |
| `shop_items.json` | 属性卡、武器卡和被动物品商店入口 |
| `items.json` | 被动物品 trigger/effect 定义 |
| `arenas.json` | 场地尺寸、材质 tile 与 PCG 配置 |
| `i18n.json` | UI 文案 |

加载失败会让 `OnInit` 报 `Brotato3D failed to load required data`。新增内容时先复制同类的现有对象，再对照 loader；未知或拼错的 stat/trigger/effect 可能被读入但不会生效。

## 新增敌人

`enemies.json` 的基础字段包括 `name`、`hp`、`moveSpeed`、`contactDamage`、`size`、`color`、`xpDrop`、`materialDrop`、`kitingDistance`。可选能力块：

- `ranged`：伤害、速度、寿命、间隔、preferred distance；JSON 字段名以当前现有 ranged 敌人为模板。
- `charge`：触发距离、加速倍率/ramp、碰撞伤害倍率、冷却。
- `bomb`：触发距离、引信、爆炸半径/伤害。
- `heal`：范围、治疗量、间隔。
- `mortar`：预警、射程、抛物线高度、lead 和爆炸参数。
- `lance`：预警、冲刺速度/距离、恢复和冷却。
- `boss`：二阶段 HP 阈值、移速和接触伤害倍率。

要让敌人出场，还必须在 `waves.json` 的 `spawns` 加 `enemyId/count/intervalMs`。当前视觉是按 size/color 派生的程序化模型；图标缺失不会阻断玩法。

## 新增武器与角色

武器 schema 由 `FWeaponDef` 定义：基础 damage/atkSpeed/range、projectile 参数、pellets/spread、pierce、explosion、instantHit beam、crit、knockback 和 tier。新增武器还要检查：

- `Brotato3DShop.cpp` 的武器价格/offer 逻辑；
- `Brotato3DAudio.hpp` 的开火音效映射；
- `shop_items.json` 是否提供商店入口；
- 角色 `startWeapon` 或波次/测试是否实际引用它。

角色在 `characters.json` 中声明 `id/name/tagline/color/startWeapon/startStats`。`startStats` 支持的字段以 `ReadPlayerStats` 为准；当前 UI 已能选择角色和 arena，不再锁死 `grassland`。

## 升级、商店和被动物品白名单

升级/属性卡的 `stat` 只在 `ApplyShopItem` 白名单内生效：

```text
damagePct damageFlat atkSpeedPct rangePct moveSpeedPct pickupRadiusPct
critChancePct critMultiplier maxHpFlat healPct
```

被动物品当前支持：

| trigger | effect |
| --- | --- |
| `passive_stat` | `stat_pickupRadiusPct`, `stat_moveSpeedPct`, `stat_damagePct`, `stat_atkSpeedPct`, `stat_rangePct`, `stat_critChancePct`, `stat_dashCharges` |
| `on_kill` | `heal` |
| `on_kill_chance` | `explosion` |
| `on_tick` | `heal_per_sec` |
| `low_hp_buff` | `stat_damagePct` |
| `on_dash_end` | `dash_knockback` |

新增 trigger/effect 不是单纯配置工作：要在 `Brotato3DCombatSystem.cpp` 或 `Brotato3DShopSystem.cpp` 实现消费逻辑，并补 UI/测试。玩家最多持有 6 个被动物品，武器槽上限是 `Brotato3DCommon.hpp::MaxWeaponSlots`。

## 波次

每波包含 `durationSec`、`bgmCue`、`duskSpawnMultiplier`、`duskBonusXpMult`、`extractionRequiredSec`、`extractionRadiusM` 与 `spawns`。需要注意：

- `duskBonusXpMult` 已在 `Brotato3DDebrisSystem.cpp` 的 XP 生成路径使用，不是预留字段。
- `bgmCue == "boss"` 的波次跳过 Dusk Surge；胜利条件和进入流程以 `Brotato3DWaveSystem` 为准。
- 普通波的 spawn interval 会随波次进度缩短；JSON interval 不是全程恒定间隔。
- Dusk Surge 会提高刷怪密度，玩家在撤离车半径内累计达到 `extractionRequiredSec` 后结束。

## 资产边界

- 可分发资源：`assets/sounds/brotato3d/`、`assets/textures/brotato3d/`，可打包进 `assets/paks/brotato3d.pak`。
- `assets/_placeholder/brotato/` 是本地参考占位资源，不可分发。出现 `[PLACEHOLDER ASSETS] ... DO NOT DISTRIBUTE` 日志时必须在发布前处理。
- 所有音频调用集中在 `Brotato3DAudio.hpp`；缺失可选资源应 fallback，不应让玩法初始化失败。

## 验证

```bash
./gnb.sh build Brotato3D
./gnb.sh run Brotato3D
./gnb.sh shot --target Brotato3D --ui --frames 120
```

只有修改 CMake 或新增文件未被现有 glob 收录时才加 `--reconfigure`。JSON 调整至少验证加载日志、角色/arena 选择、相关波次或商店路径；新增机制还要测试状态切换、对象池复用和重新开局是否清理干净。

进一步阅读：[项目介绍](introduction.md) · `AGENT_GUIDE/Brotato3D.md`。
