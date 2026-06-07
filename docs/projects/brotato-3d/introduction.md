---
title: Brotato3D 项目介绍
status: 现行
owner: docs
last_updated: 2026-05-18
---

# Brotato3D 项目介绍

Brotato3D 是 gkNextEngine 内置的俯视角 3D 生存射击子项目。它参考 Brotato / Vampire Survivors 类玩法，用 C++20 原生实现一套完整的“角色选择 → 波次战斗 → 掉落成长 → 商店构筑 → Boss 结算”循环，同时把引擎的程序化场景、Jolt 物理、ImGui HUD、音频、配置热调和运行时节点管理串在一个可玩的样例里。

它不是 QuickJS 脚本 demo，而是一个面向引擎能力验证的原生游戏原型：玩法逻辑放在 C++ 子系统中，武器、敌人、角色、物品、波次和场景数值放在 JSON 配置里。这样既能保持运行时性能和工程边界，也方便快速调参。

## 当前形态

- 入口目标：`Brotato3D`
- 源码目录：`src/Application/Game/Brotato3D/`
- 配置目录：`assets/configs/brotato3d/`
- 运行方式：`gnb.bat run Brotato3D`
- 主要文档：`docs/projects/brotato-3d/developer-guide.md`

游戏默认从主菜单进入角色选择，选择角色和竞技场后开始一局。玩家使用 WASD / 手柄左摇杆移动，武器自动锁敌开火；击杀与命中会喷出 XP / Material 碎块，靠近玩家后磁吸拾取。升级时进入三选一属性卡，波次间进入商店购买武器、被动物品或属性卡。最后一波为 Boss 波，击杀 Boss 后进入胜利结算。

## 玩法系统

Brotato3D 当前已经超过最初 MVP，核心系统包括：

- 角色：`soldier`、`brawler`、`marksman` 三个起手角色，分别对应均衡、近战爆发、远程暴击取向。
- 武器：SMG、Shotgun、Sniper、Flamethrower、Rocket、Laser，支持散射、穿透、爆炸、瞬发激光、击退和暴击。
- 敌人：普通追击、远程、冲锋、自爆、治疗、迫击炮、长枪冲刺、Boss 二阶段等多种行为。
- 波次：10 波配置，普通波之后进入 Dusk Surge 黄昏潮，玩家需要在撤离车区域内驻留完成撤离；Boss 波跳过撤离流程。
- 构筑：升级卡、商店属性卡、武器购买与合并、被动 Item、低血触发、击杀触发、Dash 结束触发等。
- 存档：记录总胜场、总击杀、最快通关时间和角色胜场。

这些系统大多由 `assets/configs/brotato3d/*.json` 驱动。新增常规敌人、武器、角色、波次或数值平衡，通常只需要改配置；新增全新机制才需要改 C++。

## 引擎验证价值

Brotato3D 的意义不只是“复刻一个玩法”，而是提供一个高频交互、实体密度高、反馈链条完整的引擎压力样例：

- 大量运行时节点：敌人、子弹、碎块、拾取物、地面警示、激光和临时光源都需要稳定创建、复用、隐藏和同步。
- 物理反馈：XP / Material / 尸块碎片使用 Jolt 物理体与 kinematic body 同步，让战斗反馈从纯 UI 飘字扩展到真实空间。
- 程序化场景：竞技场可以使用固定 tile，也可以使用 PCG 生成地形块、边界和障碍物，用于验证场景构建与碰撞体生成。
- ImGui 游戏 HUD：主菜单、角色选择、战斗 HUD、升级、商店、暂停、设置和结算都在 ImGui 中实现，覆盖较完整的游戏 UI 状态机。
- 数据加载链路：JSON 配置加载、校验、结构化为运行时数据，并与 C++ 子系统保持清晰分工。
- 音频与资源 fallback：占位音频、图标和字体通过统一路径解析，缺失资源不会阻断玩法运行。

因此它常被用作新引擎能力的试验场：如果一个 Runtime 工具、UI 组件、资源路径策略或物理同步方案能在 Brotato3D 里跑稳，通常就具备下沉到通用层的价值。

## 代码组织

| 系统 | 关键文件 | 职责 |
| --- | --- | --- |
| 主状态机 | `Brotato3DGameInstance.cpp/.hpp` | 生命周期、状态切换、场景重建、存档、子系统编排 |
| 玩家 | `Brotato3DPlayerSystem.cpp` | 移动、Dash、受伤、属性应用、角色初始化 |
| 武器 / 子弹 | `Brotato3DProjectileSystem.cpp` | 自动瞄准、开火、命中、暴击、爆炸、激光、武器合并 |
| 敌人 | `Brotato3DEnemySystem.cpp` | 刷怪、追击、远程、冲锋、自爆、治疗、迫击炮、长枪、Boss |
| 波次 | `Brotato3DWaveSystem.cpp/.hpp` | Active / DuskSurge / Intermission / Victory 状态机 |
| 掉落 / 碎块 | `Brotato3DDebrisSystem.cpp` | 物理碎块池、拾取物、磁吸、掉落表现 |
| 效果 | `Brotato3DEffectSystem.cpp` | 屏幕震动、临时光源、爆炸环、地面警示、相机覆盖 |
| 商店 / 构筑 | `Brotato3DShopSystem.cpp`, `Brotato3DShop.cpp` | 升级卡、商店、被动物品、触发器 |
| UI | `Brotato3DUI.cpp` | 菜单、HUD、Modal、结算、设置 |
| 数据加载 | `Brotato3DDataLoader.cpp/.hpp` | JSON 到运行时结构体 |
| 场景 / PCG | `Brotato3DArena.cpp`, `Brotato3DPcgGenerator.cpp` | 竞技场几何、地面 tile、PCG props 和边界 |

## 配置入口

| 文件 | 内容 |
| --- | --- |
| `characters.json` | 角色、起手武器、起始属性 |
| `weapons.json` | 武器伤害、射速、射程、弹丸、爆炸、激光、击退 |
| `enemies.json` | 敌人 HP、速度、伤害、尺寸、掉落和 AI 子能力 |
| `waves.json` | 波次时长、刷怪表、黄昏潮倍率、撤离参数、Boss 波 |
| `upgrades.json` | 升级三选一卡池 |
| `shop_items.json` | 商店属性卡、武器卡、被动 Item 入口 |
| `items.json` | 被动 Item 的 trigger / effect 数据 |
| `arenas.json` | 竞技场尺寸、材质、PCG 参数 |
| `i18n.json` | UI 文案本地化 |

如果只是调平衡，优先从这些配置开始；如果要扩 trigger、effect、AI 行为或全新武器机制，再进入 C++ 子系统。

## 推荐阅读顺序

1. 先读本文，了解 Brotato3D 的定位和系统边界。
2. 再读 `developer-guide.md`，按“改数值 / 加武器 / 加敌人 / 加波次”的方式进入实操。
3. 想了解代码结构与工程模式（god-class、子系统拆分、对象池），读 `AGENT_GUIDE/Brotato3D.md`。

