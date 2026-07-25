# Brotato3D 代码结构梳理

本文梳理 `src/Application/Game/Brotato3D/` 目录下 Brotato3D 小游戏的**代码结构与工程模式**，目标是让你在动手改 **C++** 之前先建立整体心智模型：知道每个文件负责什么、一帧数据怎么流、对象怎么复用、约定有哪些。

> **分工**：本文讲"代码怎么组织"。如果你只想**调数值 / 加敌人武器物品 / 改波次**（多数情况只改 JSON），请读 [`docs/projects/brotato-3d/developer-guide.md`](../docs/projects/brotato-3d/developer-guide.md)（配置/玩法向）。两篇互补。

---

## 1. 一句话架构

Brotato3D 是一个 **C++ 原生子应用**（不是 QuickJS 脚本游戏），核心是一个"上帝类" `Brotato3DGameInstance`，它继承引擎的 `NextGameInstanceBase`，把所有运行时状态作为成员持有；**实现按功能域拆分到多个 `*System.cpp` 翻译单元**里，但它们都是同一个类的成员函数。

```
Brotato3DGameInstance  (类声明集中在 Brotato3DGameInstance.hpp)
   │  成员函数实现分散在：
   ├── Brotato3DGameInstance.cpp     入口、OnTick 主循环、相机、Best Record
   ├── Brotato3DGameFlowSystem.cpp   状态切换（开局/暂停/结算/升级抽卡）
   ├── Brotato3DPlayerSystem.cpp     输入、移动、Dash、属性结算、ResetRuntimeState
   ├── Brotato3DProjectileSystem.cpp 武器开火、子弹、命中、爆炸、武器合并
   ├── Brotato3DEnemySystem.cpp      敌人生成/AI/受击/死亡、身体方块破碎
   ├── Brotato3DCombatSystem.cpp     伤害结算、被动 Item 触发器、漂浮文字
   ├── Brotato3DDebrisSystem.cpp     物理碎块池、拾取磁吸、kinematic body 池、竞技场墙体
   ├── Brotato3DEffectSystem.cpp     场景重建(!)、灯光、屏幕震动、撤离车、天空过渡
   └── Brotato3DShopSystem.cpp       商店购买、属性卡应用、被动加成
```

**为什么这么拆？** 把 130+ 个方法塞进一个 `.cpp` 会让编译变慢、阅读困难；按"游戏子系统"切分翻译单元是这套代码的核心组织手法。**所有 `*System.cpp` 第一行都是 `#include "Brotato3DGameInstance.hpp"`**，因为它们实现的都是这同一个类。

独立于上帝类、可单独复用/测试的有两块：
- `FWaveSystem`（[Brotato3DWaveSystem.hpp](../src/Application/Game/Brotato3D/Brotato3DWaveSystem.hpp)）—— 波次/黄昏状态机，纯逻辑、无渲染依赖。
- `FShop`（[Brotato3DShop.hpp](../src/Application/Game/Brotato3D/Brotato3DShop.hpp)）—— 商店抽卡，纯逻辑。

> **架构启示**：要加一个新子系统（例如"天气系统"），优先开一个新的 `Brotato3DXxxSystem.cpp`，在 `Brotato3DGameInstance.hpp` 里加成员/方法声明，在 `OnTick` 里插入调用——而不是往现有大文件里堆。能做成无渲染依赖的纯逻辑类（像 `FWaveSystem`）就更好，可单测。

---

## 2. 文件地图

| 文件 | 角色 | 关键内容 |
| --- | --- | --- |
| `Brotato3DGameInstance.hpp` | **类总声明** | `EAppState`、所有成员变量与方法声明、`FEnemyVisualResource`/`FTempLightRuntime` 等内嵌结构 |
| `Brotato3DGameInstance.cpp` | 入口 + 主循环 | `CreateGameInstance` 工厂、`OnInit/OnTick/OnDestroy`、`OnRenderUI`、相机平滑、Best Record 读写 |
| `Brotato3DCommon.hpp` | 共享常量/工具 | 配置文件路径常量、`ClampToArena`/`DistanceXZ`/`RotateY`/`HiddenPosition` 等 inline 工具 |
| `Brotato3DDataLoader.{hpp,cpp}` | 配置加载 | 所有 `FXxxDef` **只读原型结构** + `LoadXxx(json)` 函数 |
| `Brotato3DPlayer.hpp` | 玩家数据 | `FPlayerStats`（属性）、`FPlayerRuntime`（运行时状态） |
| `Brotato3DEnemy.hpp` | 敌人数据 | `FEnemyRuntime`、`ELanceState`、`FEnemyBodyBlockRuntime` |
| `Brotato3DProjectile.hpp` | 投射物/特效数据 | `FProjectileRuntime`、`FEnemyProjectileRuntime`、`FExpandingRing`、`FLaserBeam`、`FGroundIndicator` |
| `Brotato3DWeapon.hpp` | 武器数据 | `FWeaponRuntime`（含 `tieredDef` 副本） |
| `Brotato3DDebris.hpp` | 碎块数据 | `FDebrisRuntime`、`EDebrisKind/EPickupState/EDebrisPayload` |
| `assets/scad/brotato3d/*.scad` | **固定竞技场** | 基于 `kit_deadly.scad` 的城镇、公路郊外、沙漠荒野场景，可直接人工修缮 |
| `Brotato3DWaveSystem.{hpp,cpp}` | 波次状态机 | `FWaveSystem`：Active/DuskSurge/Intermission，事件用 `Consume*()` 拉取 |
| `Brotato3DShop.{hpp,cpp}` | 商店抽卡 | `FShop`：按权重抽属性卡/被动/武器 |
| `Brotato3DUI.{hpp,cpp}` | ImGui 界面 | 主菜单、HUD、升级/商店/暂停/结算/设置面板 + 本地化辅助 |
| `Brotato3DAudio.hpp` | **音频唯一入口** | 全部 `PlayXxxSfx`/`StartBgm` inline 函数；全局 `SfxVolume`/`MasterDifficulty` 等 |
| `Brotato3DAssetPaths.hpp` | 资源路径解析 | `Assets::Sfx/Icon` + `PlaceholderAssets::Resolve`（运行时根/仓库根双 fallback） |

> 历史上还有 `Brotato3DEnemy.cpp / Player.cpp / Projectile.cpp / Weapon.cpp` 四个**只 include 头文件、无任何实现**的空壳文件，已删除（逻辑早就搬进了 `*System.cpp`）。新增数据类型时**不要**再建这种空 `.cpp`。

**构建方式**：`src/Application/Game/Brotato3D/CMakeLists.txt` 用带 `CONFIGURE_DEPENDS` 的 `GLOB_RECURSE` 收集本目录源码，并在同一文件定义 target 与 `DEV_MODE=1`。普通增量 `./gnb.sh build Brotato3D` 会让 CMake 在 glob 变化时自动重扫；只有改 CMake/preset 或生成器没有重扫时才显式加 `--reconfigure`。

---

## 3. 数据模型：Def（原型）vs Runtime（实例）

这是整套代码最重要的一条心智线——**两类结构体严格分开**：

| | `FXxxDef`（原型 / 配置） | `FXxxRuntime`（实例 / 运行时） |
| --- | --- | --- |
| 来源 | JSON 加载，启动后**只读** | 游戏过程中**每帧可变** |
| 数量 | 每种类型一份（`std::map<id, Def>`） | 每个活动实体一份（对象池里复用） |
| 例子 | `FEnemyDef`、`FWeaponDef`、`FItemDef` | `FEnemyRuntime`、`FProjectileRuntime`、`FDebrisRuntime` |
| 关系 | —— | Runtime 持有 `const FXxxDef* def`（回指原型） |

典型：`FEnemyRuntime.def` 指向 `enemyDefs_` 里的某个 `FEnemyDef`，运行时只在 Runtime 上改 `currentHp/worldPos/...`，原型永不变。

**一个值得注意的例外 —— 武器分层**：`FWeaponRuntime` 不是简单回指原型，而是**自带一份 `tieredDef` 副本**（因为 Tier 2 是在 1 级数值上算出来的，不是 JSON 配置），`def` 指向自己的 `tieredDef`。所以每当 `equippedWeapons_` 这个 `vector` 重新分配（push_back 后地址可能变），必须调 **`NormalizeWeaponDefPointers()`** 把每个 `def` 重新指回自己的 `tieredDef`——这是个容易踩的坑，改武器相关逻辑时务必记得。

---

## 4. 一帧的数据流（`OnTick`）

`Brotato3DGameInstance::OnTick` 是总编排器（[Brotato3DGameInstance.cpp](../src/Application/Game/Brotato3D/Brotato3DGameInstance.cpp)）：

```
1. SetWorldPhysicsPaused(state != Playing)        // 非游玩态冻结 Jolt
2. 非游玩态（菜单/暂停）→ 只 MarkTransformDirty 后 return
3. 实时计时（不受慢动作影响）：屏幕震动 / 受伤闪 / 天空过渡 / 相机跟随 / 武器合并 banner
4. 计算 globalTimeScale_（Boss 击杀后 0.4→1.0 缓动）→ effectiveDt = dt * timeScale
5. if Playing:
      UpdatePlayer(dt)            // 玩家用真实 dt（手感优先）
      UpdateWeapons(dt)          // ⚠ 武器开火也用真实 dt——见下方注释
      UpdateProjectiles(effectiveDt)
      UpdateEnemies(effectiveDt)
      UpdateEnemyProjectiles(effectiveDt)
      ProcessItemTriggers(effectiveDt)
      UpdateExtractionVehicle(dt)
      waveSystem_.Update(dt, spawnCallback)    // 回调里调 SpawnEnemy
      // 然后拉取波次事件：
      ConsumeDuskBegan / ConsumeExtractionCompleted / ConsumeWaveEnded
      / ConsumeIntermissionStarted / ConsumeVictory
   else if Hitstop: 倒计时结束回到 Playing
6. if Playing: UpdateDebris(effectiveDt) + UpdateCombatEffects(effectiveDt)
7. UpdateFloatingTexts(dt)
8. MarkTransformDirty()          // 通知引擎本帧 transform 变了
```

**两个刻意设计，别"优化"掉**（代码里有注释守护）：

1. **慢动作下武器仍用真实 dt**：Boss 击杀后子弹/敌人/碎块用 `effectiveDt` 爬行，但玩家可以正常开火——这是"savor the kill"的演出节奏。注释在 [Brotato3DProjectileSystem.cpp](../src/Application/Game/Brotato3D/Brotato3DProjectileSystem.cpp) `UpdateWeapons` 上方。
2. **事件用 `Consume*()` 一次性拉取**：`FWaveSystem` 不回调主类、不持有主类指针，而是把"本波结束了""该开商店了"等事件存成 bool，主类每帧用 `std::exchange` 语义的 `ConsumeXxx()` 取走。这让波次逻辑保持零渲染依赖、可单测。

### 状态机

```
MainMenu → CharacterSelect → Playing ⇄ {Hitstop, Paused, LevelUpPicking, Shopping} → Result
```
`EAppState` 切换集中在 `Brotato3DGameFlowSystem.cpp`（`StartNewRun/PauseGame/EnterResult/BeginLevelUp/StartShopping`...）。几乎每个切换都会 `SetWorldPhysicsPaused()` + `ClearMovementInput()`，这是约定动作。

波次内部状态机（独立于 `EAppState`，在 `FWaveSystem` 里）：
```
Idle → Active →(时间到)→ DuskSurge →(玩家在撤离车驻留够秒数)→ Intermission(开商店) → 下一波
                  └─ Boss 波(bgmCue=="boss") 跳过 DuskSurge，杀光即 Victory
```

---

## 5. 关键工程模式

### 5.1 对象池（贯穿全代码）

为避免运行时频繁创建/销毁场景节点和物理体，**所有高频实体都用预分配池 + active 标志复用**：

| 池 | 容量 | 建池位置 | 复用方式 |
| --- | --- | --- | --- |
| 子弹 `projectilePool_` | 每种武器 128 | `BeforeSceneRebuild` | 按 `weaponId` 找第一个 `!active` 的槽 |
| 敌方子弹 `enemyProjectilePool_` | 128 | 同上 | 找第一个 `!active` |
| 碎块 `debrisPool_` | Tiny 800/Chunk 480/Boss 80 | `BuildDebrisPool` | 找 `!active`，没有则抢占 `activatedTickId` 最旧的 |
| 临时灯光 `tempLightPool_` | 32 | `BeforeSceneRebuild` | 找 `!active`，没有则抢 `remainingMs` 最小的 |
| 敌人 `enemies_` | 动态增长 | 运行时 | `SpawnEnemy` 优先复用同 `def` 的 dead 槽，节点/身体方块原地重置 |
| Kinematic 推挤体 | 每类敌人 32/Boss 4/Rat 96 | `BuildKinematicCollisionBodies` | `AcquireEnemyKinematicBody` 按占用情况分配 |

隐藏一个对象的统一手法：把节点平移到 `HiddenPosition`（`(0,-100,0)`，定义在 `Brotato3DCommon.hpp`）+ `SetVisible(false)` + 物理体 `SetBodyActive(false)`。

> **改动提示**：要加新的高频视觉实体（如"弹壳"），照搬碎块池的模式（在 `BeforeSceneRebuild` 预建 N 个节点入池，运行时复用），不要在 `OnTick` 里 `CreateRenderNode`。

### 5.2 场景重建生命周期（`BeforeSceneRebuild`）

引擎在加载/重建场景前回调 `BeforeSceneRebuild(nodes, models, materials, lights, tracks)`，这是**唯一可以批量塞入程序化几何体的窗口**（实现在 `Brotato3DEffectSystem.cpp`，体量大是因为它一口气建了：竞技场 → 灯光池 → 玩家+武器节点 → 撤离车 → 每种敌人的盒模型+5 个材质 → 子弹池 → 碎块池 → kinematic body 池）。

切换竞技场时加载 `arenas.json` 中配置的固定 SCAD 场景并触发重建。`sceneReady_` 标志保证波次只在场景与运行时对象池准备完毕后才启动。

### 5.3 敌人移动的统一落点解析

敌人每条移动路径（普通追击 / 迫击炮走位 / 长枪冲锋待机 / 冲刺 / 击退）都要做同一串处理：**夹到竞技场内 → 推出障碍（撤离车+道具）→ 再夹一次 → 吸附到地形高度**。这串逻辑已抽成单一方法：

```cpp
// Brotato3DEffectSystem.cpp
glm::vec3 Brotato3DGameInstance::ResolveEnemyGroundedPosition(
    const Brotato3D::FEnemyRuntime& enemy, glm::vec3 candidate) const;
```

调用方只需：`enemy.worldPos = ResolveEnemyGroundedPosition(enemy, 期望位置); enemy.node->SetTranslation(enemy.worldPos);`。**新增敌人 AI 移动时请走这个 helper**，不要再手写那串 clamp/resolve/clamp/setY。

### 5.4 物理 kinematic body 的 hack（务必先读注释）

为了让玩家/敌人能"推开"满地的物理碎块，又不被引擎的 mesh 自动晋升机制坑到，`BuildKinematicCollisionBodies` 用了一个 workaround：给每个 kinematic body 挂一个隐藏的、带 `PhysicsComponent`（标 `Dynamic` 以跳过 mesh 晋升）的代理节点来"认领"该 body。详见该函数上方注释（含 `TODO: PhysicsComponent::IsManuallyBound()` 的引擎层修复方向）。改物理相关代码前先读那段注释。

### 5.5 灯光即材质

没有传统点光源——临时光效（开火、爆炸）是**一块发光材质的 area light**（`SpawnTempLight`）。`EnsureLightMaterial(color)` 按量化后的颜色 key 缓存材质 id，避免每次新建材质。

---

## 6. 约定速查

- **命名**：类型/函数 `PascalCase`，变量/参数 `camelCase`，私有成员 `camelCase_`（尾下划线），常量 `camelCase`，宏 `UPPER_CASE`。游戏内结构体加 `F` 前缀（`FEnemyDef`），枚举加 `E`（`EAppState`）。
- **计时单位**：运行时计时几乎全是 **毫秒 `float`**，字段名带 `Ms` 后缀（`cooldownMs`、`remainingMs`）。`OnTick` 拿到的是秒（`double deltaSeconds`），进函数后立刻 `deltaMs = deltaSeconds * 1000`。
- **坐标系**：俯视 3D，玩法在 **XZ 平面**，Y 是高度。所以到处是 `DistanceXZ()` 而非 `glm::distance`。
- **`using namespace Brotato3DUtil;`**：每个 `*System.cpp` 顶部都写，以直接用 `ClampToArena`/`DistanceXZ`/`HiddenPosition` 等共享工具。
- **音频单入口**：玩法代码**只调** `Brotato3DAudio.hpp` 里的 `PlayXxxSfx`，绝不直接碰 `NextAudio`。加武器/敌人音效就扩这个 header。
- **`MasterDifficulty`**（`Brotato3DAudio.hpp`，默认 1.0）：作用于敌人接触/迫击炮伤害的全局难度乘子，可做难度/作弊开关。
- **`DEV_MODE`**：编译期宏；当前 target 在所有构建配置都定义 `DEV_MODE=1`。`K` 召唤 rat、`1~6` 切单一武器等调试键在 `#if DEV_MODE` 中，但不要声称当前 Release 会自动移除它们；若要区分配置，应先修改 CMake 条件。
- **本地化**：UI 文本走 `Tr(gameInstance, key, fallback)` / `TrFormat(...)`，底层是引擎的 `NextLocalization`（在 `OnInit` 里 `LoadFromJson(i18n.json, "zh")`）。

---

## 6.5 竞技场地图（`assets/scad/brotato3d/*.scad`）

三张固定地图由 `assets/configs/brotato3d/arenas.json` 绑定，改地图前先记住这几条硬约束：

| 事实 | 出处 | 对地图的意义 |
|---|---|---|
| `halfExtent` = town [260,160] / outskirts [320,200] / desert [340,220] | arenas.json | SCAD 的 x/y 直接对应；边界外有隐形墙 |
| 相机 = 目标 + (0, 30, 11)，FOV 60°，target 被 clamp 到 `halfExtent - (32, 38)` | `OverrideRenderCamera` / `UpdateCameraTracking` | **`halfExtent` 之外永远看不见，往外堆几何是纯浪费** |
| 世界 `z = -scad_y` | ScadLoader 的 Z-up→Y-up | 相机从 SCAD 的 -y 往 +y 看，**kit 的 front = -y 正对镜头** |
| 单屏地面 ≈ 66 m (x) x 41 m (y) | 由上面两条推出 | 设计粒度：**每 66x41 m 至少要有一处结构** |
| 建筑不参与碰撞（只有撤离车是障碍） | `ResolvePlayerObstacleCollision` | 街区的价值是**可读的规划 + 方位地标**，不是阻挡 |

写地图的方法（三张图都按这个来的，改图/加图照抄）：

1. **先画格子再写坐标**：town 是 65x58 m 的方格街坊（一街坊≈一屏），outskirts 是一条分离式公路 + 农田网格，desert 是公路 + 铺砖式地表分区。
2. **每块空地都要有用途**：停车场 / 院子 / 球场 / 田块 / 碎石场，而不是裸草皮。`dd_ground_lot`、`dd_nature_field_big`、`dd_ground_gravel` 就是为此加的。
3. **线性件划红线**：绿篱 / 铁丝网 / 栅栏 / 护栏 / 隔离墩，它们是俯视下"被规划过"的最强信号。
4. **地标锚定方位**：水塔 / 教堂尖塔 / 信号塔 / 加油站雨棚 / 广告牌，跑图时每 1~2 屏能看到一个。
5. **边界要看得见**：town/outskirts 用林带，desert 用岩带 + 沙丘，让玩家理解隐形墙在哪。
6. **朝向**：面向镜头的一排用默认朝向（front = -y），背面排用 `rotate([0,0,180])`；注意 `lay_row` 永远沿 +x 生长，旋转 180° 的排必须从街坊**东端**起算。
7. **地面叠层**：地面件重叠时上层要抬 `dd_layer(1)`，理由见 [SCAD 资产实战手册](ScadAssetPlaybook.md) 的"地面叠层"。

验收用 `gnb shot --scene assets/scad/brotato3d/<map>.scad`；注意全图 shot 的相机在 ~600 m 外，薄板堆叠处会出现光线精度造成的黑斑，**那是远距离截图的 artifact，游戏内 32 m 机位不会有**——怀疑时把该区域单独摆成一个小场景再截一张。

---

## 7. 改完怎么验证

```bash
# 构建
gnb.bat build Brotato3D      # Windows
./gnb.sh build Brotato3D     # macOS/Linux

# 运行：看到 "uploaded scene [...] to gpu" 即初始化通过
./gnb.sh run Brotato3D
```
- 配置缺字段会在 `OnInit()` 抛 `Brotato3D failed to load required data`。
- 用 `DEV_MODE` 的 `K`（喷怪）+ `1~6`（切武器）边玩边验数值。

---

## 8. 进一步阅读

- [`docs/projects/brotato-3d/developer-guide.md`](../docs/projects/brotato-3d/developer-guide.md) —— 配置/数值/加内容向开发指南（本文的姊妹篇）
- [`docs/projects/brotato-3d/introduction.md`](../docs/projects/brotato-3d/introduction.md) —— 项目定位
- [`AGENT_GUIDE/MagicaLego.md`](MagicaLego.md) —— 另一个 C++ 小游戏的同类梳理，可对照
- [`AGENTS.md`](../AGENTS.md) —— 引擎全局规范、构建/命名/目录约定
