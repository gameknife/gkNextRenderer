# Brotato 3D — 射击手感打磨计划（Phase 4 · Feel）

## Context

MVP（M1–M9）→ 产品化（P1–P10）→ 资产占位（B1–B10）三阶段完成后，Brotato3D 在「玩法循环 + 内容厚度 + 视听包装」上已经成型。本计划专门解决最后一公里的**射击手感与命中反馈**：让屏幕在交火瞬间充满"重量"——子弹打到敌人、敌人被打死、自己被打中，都要有大量带真实物理模拟的碎块飞出来；同时让现有的 Material / XP 拾取物作为「碎块 + 战利品」混合体一起被炸飞，玩家走过去自动磁吸捡起来。

> **本计划的前提**：
> - MVP 计划见 [plan.md](plan.md)，产品化见 [product-plan.md](product-plan.md)，资产占位见 [asset-polish-plan.md](asset-polish-plan.md)
> - 假设 P1–P10 已完成（B1–B10 完成与否不影响本计划，本计划只动 ProcModel + 物理 + 池子）
> - 引擎已开启 `WITH_PHYSIC=1`（[CMakeLists.txt:30](../../../CMakeLists.txt:30)），Jolt 通过 `NextPhysics` 暴露 [`CreateBoxBody / SetBodyVelocity / SetBodyActive / SetBodyTransform`](../../../src/Runtime/Subsystems/NextPhysics.h:60)
> - KongLie3D 已经为 piece 死亡实现了真物理 knockout 范式（[KongLie3DGameInstance.cpp:795](../../../src/Application/KongLie3D/KongLie3DGameInstance.cpp:795) 创建 + [KongLie3DBattleSystem.cpp:255](../../../src/Application/KongLie3D/KongLie3DBattleSystem.cpp:255) 复位）—— 本计划直接复用同一套路

## Implementation Notes（实现偏离 plan 的点）

最终落地的实现与本计划在以下几点有出入，后续阅读以代码为准：

- **没有按 R1–R6 拆分独立任务**：实际是一次性写完，文件结构按 plan 走（`Brotato3DDebris.hpp` + `Brotato3DDebrisSystem.cpp`）
- **Material 拾取物走统一 debris pool**：通过 `FDebrisRuntime::pickable` flag 区分装饰碎块和 Material box，而非 plan 描述的"独立 emission"。XP 球同样获得了真物理 body（plan 里只规划了伪物理弹跳）
- **三态 pickup 状态机简化**：Settling 状态曾仅作为计时器存在，无独立行为差异；后续清理已改为 `None / Physics / Magnetic`
- **Kinematic body 加了"代理 Node" workaround**：因为 `Scene::RebuildMeshBuffer` 会把没有 PhysicsComponent 持有的 body 替换成 mesh body。绕过方法是为每个 kinematic body 创建一个 invisible 渲染 Node，挂 PhysicsComponent（mobility 标记 Dynamic 让 mesh rebuild 跳过）。这是引擎层缺陷，未来应在引擎层修
- **Kinematic body 同步使用固定 Jolt step**：`MoveKinematic` 的 dt 用于计算到达目标的速度，最终实现传固定 `1/60s` 以匹配 `NextPhysics::Tick` 的 fixed step；不能传 render-frame dt，否则 physics skip / 高帧率下会产生速度尖峰
- **Wave 切换时 `ClearAllDebris(false)` 会一并清掉未拾取的 Material box**：玩家来不及捡的就丢了。这是产品决策（避免跨波累积破坏经济）

## 目标（Phase 4 验收线）

完成 R1–R6 后，端到端体验应该达到：

1. **每一发子弹命中敌人**：在命中点炸出 4–8 块小 box 碎块（cm 级），真物理模拟，落地翻滚后**留在场上**——后续可被玩家 / 敌人走过时推动、被新爆炸的冲击波弹飞
2. **击败小怪 / 中怪 / 精英 / Boss**：原位炸出 8 / 14 / 20 / 50 块大 box 碎块（dm 级），颜色继承敌人色，物理弹飞、互相碰撞、堆积；**Material 黄色 box 混在其中一起被冲量打飞**（同一发爆炸的发射，区分仅在颜色 + 是否可拾取）
3. **主角被打中**：玩家位置喷出 6 块小 box 碎块（玩家服装色 / 白色），高度集中、低初速
4. **Material 拾取物**：从「磁吸 lerp 球」改为「物理 box」，与死亡碎块**走完全相同的 spawn 函数**（同一份 corner offset / 冲量分布），仅 material id（黄色）不同 + 标记 pickable。落地翻滚清晰可见（box 翻滚比球翻滚视觉强 5×）。玩家进入 pickupRadius 时 → 解除物理 → 切回磁吸吸过来
5. **XP 球**：保持原有磁吸（升级反馈不能破坏），但**spawn 瞬间**追加一个轻微弹起 + 短暂物理（200–400ms 伪物理，不接 Jolt）
6. **场地被战斗"刻"出痕迹**：碎块默认**不**淡出，留在场上直到池满 LRU 替换；wave 进行中场地逐步堆积碎块，越打越乱、越打越爽；wave 结束清场时一起 deactivate（避免 Material 跨波残留）
7. **物理对象总量**：池子开大——同屏 ~1500 个物理 body（debris 1080 + material 256 + player + enemies），Jolt 处理几千个 sleeping body 几乎零成本，瓶颈在 Scene Node 数量
8. **玩家 / 敌人能推动碎块**：玩家与敌人各挂一个 kinematic body（玩家 sphere、敌人 box），**仅用于推开 debris**，不参与战斗碰撞（仍是距离判定）
9. **不破坏既有功能**：升级 / 商店 / hit-stop / 屏幕震动 / 暴击 / 拾取磁吸 / 战利品计数全部不变

> **不在范围内**：不改美术资产路线（继续 ProcModel 几何体），不改 wave / 升级 / 商店逻辑，不引新依赖（Jolt 已经在用），不实装碎块互相碰撞造成伤害（纯视觉 + 推力），不做布料 / 软体 / 流体。

## 引擎已有的关键能力（直接复用，不新造轮子）

| 需求 | 复用 | 路径 |
|---|---|---|
| 物理 body 创建 / 复用 | `NextPhysics::CreateBoxBody / CreateSphereBody`（Dynamic）+ `SetBodyActive(false)` 池化 | [src/Runtime/Subsystems/NextPhysics.h:61](../../../src/Runtime/Subsystems/NextPhysics.h:61) |
| Body 复位（重用碎块） | `SetBodyTransform(pos, rot, resetVelocity=true)` + `SetBodyVelocity(0, 0)` | [NextPhysics.h:71](../../../src/Runtime/Subsystems/NextPhysics.h:71) |
| Body 给一股冲量 | `SetBodyVelocity(linear, angular)` —— 无需 `AddForce`（瞬时初速度更可控） | [NextPhysics.h:72](../../../src/Runtime/Subsystems/NextPhysics.h:72) |
| Node ↔ Body 同步 | `PhysicsComponent` + 引擎自动每帧用 body 位置驱动 node transform | [src/Runtime/Components/PhysicsComponent.h](../../../src/Runtime/Components/PhysicsComponent.h)，[KongLie3DGameInstance.cpp:795](../../../src/Application/KongLie3D/KongLie3DGameInstance.cpp:795) |
| 几何 / 材质组合复用 | `FProcModel::CreateBox / CreateSphere` + `SceneBuilder::AddLambertianMaterial` | [src/Assets/Loaders/FProcModel.h](../../../src/Assets/Loaders/FProcModel.h) |
| 碎块淡出（无需删 Node） | 已有套路：`SetVisible(false)` + 移到 `HiddenPosition` + `SetBodyActive(false)` | [Brotato3DEffectSystem.cpp:262](../../../src/Application/Brotato3D/Brotato3DEffectSystem.cpp:262) |
| 「飞溅 + 物理冲量」工具函数 | 已有 `SpawnImpactDebris` / `SpawnDeathDebris`，本计划改写其内部不改签名 | [Brotato3DEffectSystem.cpp:487](../../../src/Application/Brotato3D/Brotato3DEffectSystem.cpp:487)、[Brotato3DEnemySystem.cpp:298](../../../src/Application/Brotato3D/Brotato3DEnemySystem.cpp:298) |
| 调参随机数 | 已有 `rng_`（mt19937） | [Brotato3DGameInstance.hpp](../../../src/Application/Brotato3D/Brotato3DGameInstance.hpp) |

**仍然不需要**：粒子系统、布料、shader 调整、glTF 资产、关卡几何变形。

## 设计要点（Why）

> 这一段是设计意图，agent 执行时遇到边界判断要按这个意图走。

1. **碎块 = 物理 box body + 渲染 node 的组合**，全部走对象池（启动时一次创建 N 个，运行时只切 active），不要每次 spawn 都 `CreateBoxBody` 然后 `RemoveBody`——Jolt 创建 body 比创建普通 C++ 对象贵 50×，运行时频繁创建是性能毒药。KongLie3D knockout 也是这么做的。
2. **所有碎块统一用 box**（Jolt 对 box 性能最好；视觉上 box 翻滚比 sphere 翻滚明显得多）。三档边长对应三种语义，**池规模开大让碎块持续留在场上**，让战斗场景越打越乱：
   - `Tiny`（0.04m）：子弹命中 hit 飞溅、玩家受击，**池 600**
   - `Chunk`（0.18m）：敌人死亡、Material 拾取物，**池 480**（其中 ~256 在任意时刻可能是 pickable Material，与纯装饰 Chunk 共池）
   - `BossChunk`（0.36m）：Boss 死亡专用，**池 80**
   - 总：**1160 个 dynamic box body**。Jolt 上千 sleeping body 几乎零成本，醒着的几十个/百来个才有性能开销。
3. **碎块寿命策略**：默认**不基于时间淡出**，碎块持续留在场地直到「池被新一轮爆炸征用」（LRU 抢占最旧的 active slot）—— 这样玩家越打地面越乱，爽快感和视觉密度持续累积。**唯一的强制清理时机**：wave 结束清场（避免 pickable Material 跨波残留）+ 重开局。被抢占的 slot 在被复用前最后 80ms 做一次 scale 1 → 0.2 收缩动画避免硬切（如果实现复杂可以省掉，直接硬切也接受）。
4. **碎块颜色继承**：hit 碎块「子弹色 + 敌人色 50/50」（被打飞的皮肉碎屑），death 碎块敌人皮肤色 70% + 阴暗版 30%（敌人本体被炸碎）。预生成 36 个 material id 存进 `enemyVisuals_`，运行时不新建材质。
5. **冲量大小**：hit 碎块 3–5 m/s + 微抬；death 碎块 4–7 m/s 散开（cosine-weighted 半球，朝爆心半空）；player 碎块 2–3 m/s + 0.5m 高度（不要飞太远，不然像爆炸而不是"被擦伤"）。所有碎块附 8–16 rad/s 角速度让其翻滚。
6. **Material 拾取物 = 黄色 Chunk box**：和 death 碎块走**完全相同的 spawn 流程**（同一函数、同一发 emission 模式、同一组 corner offset），仅有两点区别：
   - material id（黄色 `(1, 0.85, 0.15)`）
   - slot 在池里被标记 `pickable=true`，update 时检查 pickup 状态机
   - 三态机：`Physics`（spawn 后 600ms 物理飞）→ `Settling`（200ms 速度低于阈值确认静止）→ `Magnetic`（deactivate body，lerp 追玩家）。玩家进入 pickupRadius 时**任何状态强制切 Magnetic**。
   - **不允许**Material 物理 body 与玩家、敌人的 kinematic body 交互（避免被顶飞），通过 layer 隔离（详见 R5）。Material 之间、Material 与 cosmetic debris 之间则**正常碰撞**（场上越乱越好）。
7. **XP 球保留磁吸**（升级反馈核心，不能破坏）：仍然是 sphere 球，无 Jolt body，spawn 瞬间追加 0.3s 伪物理小弹跳让它和 Material 的物理飞溅节奏一致，0.3s 后 `magnetized=true` 直接锁定向玩家追。
8. **玩家 / 敌人能推动 debris**：玩家挂一个 kinematic sphere body（半径 0.4m），敌人池每个 slot 挂一个 kinematic box body（按敌人 size），仅用于把走过的 debris 推开。每帧用 `MoveKinematicBody` 同步位置（Jolt 会把玩家的运动转化为对周围 dynamic body 的推力）。**这些 kinematic body 不参与战斗碰撞**（敌人接触伤害 / 子弹命中仍是距离判定）。
9. **Boss 死亡**视觉冲击是高潮，单独 R6 做：50 Chunk + 8 BossChunk + Material 撒一圈 + 双 ring + 1.2s 慢动作 + 大屏震 + 全屏闪白 100ms。
10. **物理 body 数量预算**：debris 1160 + Material 池里复用的 256（共池） + 玩家 1 kinematic + 敌人池 ~80 kinematic ≈ 1240 ~ 1300 总 body，启动时一次性创建。Jolt 上千 dynamic body 在中端笔记本 60fps 实测无压力。

## 文件结构（最终态）

```
src/Application/Brotato3D/
├── Brotato3DDebris.hpp           # 新建：碎块统一抽象（FDebrisRuntime { kind, body, node, pickable, pickupState, ... }）
├── Brotato3DDebrisSystem.cpp     # 新建：池构造 / spawn / update / pickable 状态机 / reset / 玩家+敌人 kinematic body
├── Brotato3DGameInstance.{hpp,cpp}   # 改：BeforeSceneRebuild 调度新池构造、OnTick 加 kinematic body 同步
├── Brotato3DEffectSystem.cpp     # 改：迁出 SpawnImpactDebris；旧 pickup pool 改为 128 个 XP slot；Material model/material 删除
├── Brotato3DEnemySystem.cpp      # 改：SpawnDeathDebris 改写（含 Material 混合 emission）+ 删 Material SpawnPickup 调用 + 敌人 kinematic body 同步
├── Brotato3DPlayerSystem.cpp     # 改：DamagePlayer 加 SpawnPlayerDamageDebris；重开局调 ClearAllDebris(false)；玩家 kinematic body 同步
├── Brotato3DEnemy.hpp            # 改：FEnemyRuntime 加 NextBodyID kinematicBodyId
├── Brotato3DPickup.hpp           # 改：删 EPickupKind::Material；FPickupRuntime 加 bouncePhysicsMs / bounceVelocity
├── Brotato3DPickupSystem.cpp     # 改：删 Material 分支；XP 加 spawn 弹跳逻辑
└── Brotato3DProjectile.hpp       # 改：删 FImpactDebrisRuntime（合并到 FDebrisRuntime）
```

CMake 改动：仅 [src/cmake/SourceFiles.cmake](../../../src/cmake/SourceFiles.cmake) 的 `src_files_brotato3d` GLOB 自动收新文件，无需手改。

## 任务索引（共 6 个核心，~7–8 小时）

| # | 标题 | 工时 | 依赖 |
|---|---|---|---|
| [R1](#r1-debris-子系统骨架--物理池--玩家敌人-kinematic-body) | Debris 子系统骨架 + 物理池（box 1160）+ 玩家/敌人 kinematic body | ~2h | — |
| [R2](#r2-命中碎块hit-debris物理化) | 命中碎块（hit debris）物理化 | ~1h | R1 |
| [R3](#r3-敌人死亡碎块chunk物理化--颜色继承--material-混合) | 敌人死亡碎块物理化 + 颜色继承 + Material 混合 emission | ~1.5h | R1, R2 |
| [R4](#r4-玩家受击碎块) | 玩家受击碎块 | ~0.5h | R1 |
| [R5](#r5-xp-球小弹跳--旧-pickup-系统瘦身) | XP 球小弹跳 + 旧 Pickup 系统瘦身 | ~1h | R3 |
| [R6](#r6-boss-死亡高潮--大碎块--慢动作) | Boss 死亡高潮 + 大碎块 + 慢动作 | ~1h | R3, R5 |

> **执行节奏**：R1 必前置；R2 / R4 之间正交可不同 agent 并行；R3 / R5 依赖 R1 的 pickable slot 状态机端到端，建议串行；R6 最后做。

---

## R1. Debris 子系统骨架 + 物理池 + 玩家/敌人 kinematic body

**优先级**: P0  **工时**: ~2h

### 背景

把现在散落在 EffectSystem / EnemySystem 的伪物理碎块逻辑（160 红色 box 池 + 手写 `velocity.y -= 9.8 * dt`）抽出来重写。建立统一的 `FDebrisRuntime` 结构，三档边长 box 三个池子（**全部 box，不用 sphere**），共 1160 个 dynamic body 在 `BeforeSceneRebuild` 阶段一次性预创建。同时给玩家挂 1 个 kinematic sphere body、给敌人池每 slot 挂 1 个 kinematic box body，让玩家 / 敌人走过场地时能推动堆积的碎块。**本任务后游戏行为基本不变**——SpawnImpactDebris / SpawnDeathDebris 仍调旧数量，颜色仍是 fallback；R2/R3 接管视觉细节。

### TODO

**1. 新建 `src/Application/Brotato3D/Brotato3DDebris.hpp`**：
```cpp
#pragma once
#include "Common/CoreMinimal.hpp"
#include "Runtime/Subsystems/NextPhysicsTypes.h"
#include <glm/glm.hpp>

namespace Assets { class Node; }

namespace Brotato3D
{
    enum class EDebrisKind : uint8_t { Tiny = 0, Chunk = 1, BossChunk = 2 };

    // pickable=true 时，slot 是 Material 拾取物（黄色），update 跑 pickup 状态机
    enum class EPickupState : uint8_t { None = 0, Physics = 1, Settling = 2, Magnetic = 3 };

    struct FDebrisRuntime
    {
        EDebrisKind kind = EDebrisKind::Tiny;
        NextBodyID bodyId{};
        std::shared_ptr<Assets::Node> node;
        uint32_t baseModelId = 0;
        uint32_t currentMaterialId = 0;

        // LRU 抢占用：activatedTickId 越小越旧；池满找最小的覆盖
        uint64_t activatedTickId = 0;
        bool active = false;

        // pickable Material slot 专用（cosmetic 碎块这些字段不用）
        bool pickable = false;
        EPickupState pickupState = EPickupState::None;
        int materialValue = 0;
        float settleTimerMs = 0.0f;
        float magneticLerpProgress = 0.0f;
    };
}
```

**2. 在 `Brotato3DGameInstance.hpp` 的 private 成员里替换**：
- 删 `std::vector<Brotato3D::FImpactDebrisRuntime> impactDebrisPool_;`、`uint32_t impactDebrisModelId_`、`uint32_t impactDebrisMaterialId_`
- 加 `std::vector<Brotato3D::FDebrisRuntime> debrisPool_;`
- 加 `uint32_t debrisTinyModelId_, debrisChunkModelId_, debrisBossChunkModelId_;`
- 加 `uint32_t debrisFallbackTinyMatId_, debrisFallbackChunkMatId_;`
- 加 `uint64_t debrisTickCounter_ = 0;`（LRU 计数器，每次 spawn 后 ++）
- 加 `NextBodyID playerKinematicBodyId_{};`
- 删 `Brotato3DProjectile.hpp` 里的 `FImpactDebrisRuntime`

**3. 新建 `src/Application/Brotato3D/Brotato3DDebrisSystem.cpp`**：
```cpp
void Brotato3DGameInstance::BuildDebrisPool(std::vector<Assets::Model>& models,
                                             std::vector<Assets::FMaterial>& materials,
                                             std::vector<std::shared_ptr<Assets::Node>>& nodes);

void Brotato3DGameInstance::BuildKinematicCollisionBodies();   // 玩家 + 敌人 kinematic body

// 通用 spawn：count 个同 kind box，按 cosine-weighted 锥形冲量散开
// `pickable` 为 true 时把 slot 标记为 Material 拾取物（材质用黄色），spawn 完毕由调用方分发 pickupState=Physics
void Brotato3DGameInstance::SpawnDebris(Brotato3D::EDebrisKind kind,
                                         const glm::vec3& worldPos,
                                         const glm::vec3& impulseDir,
                                         float speed,
                                         uint32_t materialId,
                                         int count,
                                         float angleConeRad,
                                         bool pickable = false,
                                         int materialValuePerSlot = 0);

void Brotato3DGameInstance::UpdateDebris(double deltaSeconds);   // pickup 状态机 + 玩家磁吸
void Brotato3DGameInstance::ClearAllDebris(bool keepPickable);   // wave 清场 / 重开局
```

`BuildDebrisPool` 行为：
- 三个 model：`CreateBox(-vec3(0.04), vec3(0.04))` / `CreateBox(-vec3(0.18), vec3(0.18))` / `CreateBox(-vec3(0.36), vec3(0.36))`
- 两个 fallback material：`tiny_red(1.0, 0.18, 0.08)`、`chunk_grey(0.5, 0.5, 0.5)`
- 池规模：**Tiny=600、Chunk=480、BossChunk=80**，总 1160
- 对每个 slot：创建 Node + `PhysicsComponent`，`physics->CreateBoxBody(HiddenPosition, halfExtent, Dynamic)`，`SetBodyActive(false)`，`Node->SetVisible(false)`，`PhysicsComponent::BindPhysicsBody(bodyId)`
- **如果 `debrisPool_` 已非空**（场景重建二次调用）：先遍历旧 pool 调 `physics->RemoveBody`，再重建
- 完成后 `spdlog::info("[Brotato3D] debris pool created: {} dynamic bodies", debrisPool_.size())`

`SpawnDebris` 行为：
- 用 LRU 抢占：先扫一遍 `!active && kind == requestedKind` 的 slot 凑数；不够就按 `activatedTickId` 升序补足 `count` 个最旧的 active slot 强制覆盖
- 抢占时直接重置（不做 80ms 收缩动画 —— MVP 阶段保留硬切，注意点提到可后续优化）
- 每个 slot：
  - `spawnPos = worldPos + uniform(-0.08, 0.08)*3`
  - 方向：`impulseDir` 为锥心，cosine-weighted 半角 `angleConeRad` 抽样；垂直分量 lift `+0.3 ~ +0.8`
  - `velocity = direction * speed * uniform(0.7, 1.3)`
  - `angularVelocity = rng vec3 * uniform(8, 16) rad/s`
  - `physics->SetBodyTransform(bodyId, spawnPos, randomQuat, true)`
  - `physics->SetBodyVelocity(bodyId, velocity, angularVelocity)`
  - `physics->SetBodyActive(bodyId, true)`
  - `node->SetVisible(true)`
  - `NodeUtils::SetPrimaryMaterial(node, materialId)`
  - `slot.activatedTickId = ++debrisTickCounter_`
  - `slot.active = true`
  - 若 `pickable`：`slot.pickable = true; slot.pickupState = EPickupState::Physics; slot.settleTimerMs = 600.0f; slot.materialValue = materialValuePerSlot;`
  - 否则 `slot.pickable = false; slot.pickupState = EPickupState::None;`

`UpdateDebris` 行为（**不再有 lifetime 倒计时**）：
- **不要**手动改 cosmetic slot 的 worldPos —— 引擎会用 body 位置驱动 node transform
- 对 `pickable` slot 跑状态机：
  - `Physics`：`settleTimerMs -= dt*1000`，到 0 切 `Settling`
  - `Settling`：用 `physics->GetBody(bodyId)->velocity` 查 linear 速度（NextPhysics 已暴露 [GetBody](../../../src/Runtime/Subsystems/NextPhysics.h:74) 返回 `FNextPhysicsBody*`，里面有 `velocity` 字段，但实际 Jolt 同步频率需实测；若不可靠就直接用计时器：再 400ms 切 Magnetic）
  - 不论 Physics/Settling，**距离玩家 < `pickupRadius * (1 + stats.pickupRadiusPct)` 时强制切 Magnetic**：
    - 读出当前 body 位置存到一个临时 `magneticPos`（继续 lerp 用），`physics->SetBodyActive(bodyId, false)`，`magneticLerpProgress = 0`
  - `Magnetic`：用 lerp 把 node translation 朝玩家追（`translation = mix(translation, player_.worldPos, 12*dt)`），距离 < 0.4 时拾取（`materials += slot.materialValue`，PushFloatingText "+N MAT"，`active = false`，`SetVisible(false)`，`pickable = false`）

`ClearAllDebris(keepPickable)` 行为：
- 遍历 `debrisPool_`：
  - 若 `keepPickable && slot.pickable && slot.active` → 跳过（wave 中清场不偷玩家的 Material）
  - 否则：`physics->SetBodyActive(false)`，`physics->SetBodyTransform(HiddenPosition, identity, true)`，`physics->SetBodyVelocity(0, 0)`，`SetVisible(false)`，`active = false`，`pickable = false`，`pickupState = None`
- 实际调用：wave 强制清场调 `ClearAllDebris(/*keepPickable=*/false)`（让 Material 也清掉，避免跨波）；重开局也调 `false`

**4. `BuildKinematicCollisionBodies` 行为**：
- 玩家：`physics->CreateSphereBody(player_.worldPos, 0.4f, NextMotionType::Kinematic)`，存到 `playerKinematicBodyId_`
- 敌人池每 slot（在敌人 spawn 时若无 body 就创建；或在 `BeforeSceneRebuild` 一次性按敌人池预创建）：`physics->CreateBoxBody(HiddenPosition, def.size * 0.5f, Kinematic)`
  - 选择按敌人 def 类型创建 N 个 body 池（rat/spitter/brute/...），spawn 时绑给 `enemy.kinematicBodyId`
  - 每帧 `MoveKinematicBody(bodyId, enemy.worldPos, identity, dt)`（[NextPhysics.h:70](../../../src/Runtime/Subsystems/NextPhysics.h:70) 是为这个用例设计的）
- **关键**：这些 kinematic body 需要放进一个**与玩家/敌人不交互、只与 debris dynamic body 交互**的 layer。看 [src/Runtime/Subsystems/NextPhysicsTypes.h](../../../src/Runtime/Subsystems/NextPhysicsTypes.h) 的 `NextLayers`：如果只有 `MOVING / NON_MOVING` 两层，就用 Kinematic（默认 NON_MOVING 类似行为，但能推 dynamic）。**实测验证**：玩家 / 敌人的 kinematic body **不会**因为 dynamic 碎块的反作用力被推开（Kinematic body 不接收外力，只主动推 Dynamic）—— Jolt 默认行为。
- 玩家 kinematic body 每帧用 `MoveKinematicBody(playerKinematicBodyId_, player_.worldPos, identity, dt)` 同步

**5. 在 `BeforeSceneRebuild`（[Brotato3DEffectSystem.cpp:11](../../../src/Application/Brotato3D/Brotato3DEffectSystem.cpp:11)）的整改**：
- 删 `impactDebrisPool_` 那 ~13 行的旧创建块
- 末尾追加 `BuildDebrisPool(models, materials, nodes); BuildKinematicCollisionBodies();`

**6. 调用点改造**：
- `OnTick`：`UpdateImpactDebris` 调用改 `UpdateDebris`；删除 `Brotato3DEffectSystem.cpp` 里 `UpdateImpactDebris` 的实现
- 旧 `SpawnImpactDebris(worldPos)` 改：`SpawnDebris(EDebrisKind::Tiny, worldPos, vec3(0,1,0), 4.0f, debrisFallbackTinyMatId_, 3, glm::pi<float>()*0.7f);`
- 旧 `SpawnDeathDebris(enemy)` 改：临时仅 cosmetic（material drop 在 R3 / R5 合并）：
  ```cpp
  int chunkCount = 3;
  if (enemy.def->boss.enabled) chunkCount = 30;
  else if (enemy.def->bomb.enabled) chunkCount = 8;
  else if (enemy.def->heal.enabled || enemy.def->name == "Brute") chunkCount = 6;
  SpawnDebris(EDebrisKind::Chunk, enemy.worldPos, vec3(0,1,0), 5.0f,
              debrisFallbackChunkMatId_, chunkCount, glm::pi<float>()*0.9f);
  ```
- 重开局清理：把 [Brotato3DPlayerSystem.cpp:312](../../../src/Application/Brotato3D/Brotato3DPlayerSystem.cpp:312) 的 `for (auto& debris : impactDebrisPool_)` 替换为 `ClearAllDebris(false);`
- wave 强制清场（[Brotato3DEnemySystem.cpp](../../../src/Application/Brotato3D/Brotato3DEnemySystem.cpp) 的 `ClearAliveEnemies` 触发）：在 wave 切换 / EnterShop 时同时调 `ClearAllDebris(false)`，让上一波的 debris + 未拾取的 Material 一起清空（避免跨波堆积无限增长 + 避免 Material 跨波累积破坏经济）
- 玩家 / 敌人 kinematic body 同步：`OnTick` 内对每个 alive 敌人调 `MoveKinematicBody(enemy.kinematicBodyId, enemy.worldPos, identity, dt)`；玩家同理
- 敌人池销毁（KillEnemy 时）：把对应 kinematic body `SetBodyActive(false)`（不 RemoveBody，留池复用）

### 涉及文件
- 新建：`Brotato3DDebris.hpp`、`Brotato3DDebrisSystem.cpp`
- 改：`Brotato3DGameInstance.hpp`、`Brotato3DProjectile.hpp`（删 FImpactDebrisRuntime）、`Brotato3DEffectSystem.cpp`（删旧 spawn / update）、`Brotato3DEnemySystem.cpp`（SpawnDeathDebris 简化 + kinematic body 同步）、`Brotato3DEnemy.hpp`（FEnemyRuntime 加 `NextBodyID kinematicBodyId{};`）、`Brotato3DPlayerSystem.cpp`（重置 + 玩家 kinematic body 同步）、`Brotato3DGameInstance.cpp`（OnTick 调度）

### 验收方法
1. `./build.bat --preset full-windows --reconfigure` 通过
2. 启动后日志看到 `debris pool created: 1160 dynamic bodies` + 玩家 1 + 敌人 N 的 kinematic body
3. 玩 wave 1：击中敌人有红色小碎块飞出（真物理：能看到落地后翻滚两下），击败 rat 有 3 块大碎块（fallback 灰色）
4. **碎块不会自动消失** —— 打几只 rat，地上累积 ~30 块 box，玩家走过去能用身体推开它们
5. **敌人也会推 debris** —— 一只 rat 朝玩家冲来时碾过的碎块会被推开
6. 击败大量敌人到 wave 中段，地面碎块 ~200 块共存，60fps 不卡
7. wave 切换时（30s 倒计时到）所有碎块瞬间清空
8. 重开（按 R 或死后再来）后场地全清空、无残留 body
9. Boss 死亡有 30 块 chunk 飞出
10. 满场 30+ 敌人 + 满射时不卡顿（同时 active body 估计 < 400，sleeping body ~700+）

### 注意
- **物理 body 创建后 Scene 重建**：`BuildDebrisPool` 入口检测 `debrisPool_.empty()`，非空就先 `physics->RemoveBody` 全部再重建。同样套路在 `BuildKinematicCollisionBodies` 里也要做。
- **PhysicsComponent transform 写回**：引擎默认会用 body transform 写到 node。若发现 node 不动 / 错位，检查 `RecalcTransform(true)` 是否被错误调用覆盖了；KongLie3D knockout 阶段就是不再调 SetTranslation / RecalcTransform 让物理接管。
- **不要**给玩家 / 敌人挂动态 PhysicsComponent —— **只挂 kinematic** body，且 kinematic body 不参与战斗碰撞（敌人接触伤害 / 子弹命中仍走距离判定）
- 不要在 R1 阶段就改颜色继承（R3 任务），fallback 颜色保持现状
- LRU 抢占阈值：当一帧 spawn 数量 > 池里 `!active` 数量时才发生抢占；正常 wave 不会触发，wave 5 后期可能偶发；测试时不必特别构造，自然玩出来即可
- **kinematic body layer 隔离**：玩家 kinematic body 不应该把敌人 kinematic body 推开（虽然两个都是 kinematic 不会互推，但 layer 配置仍要确认）。如果发现玩家移动会被某个 kinematic body 卡住（不应该发生，因为 Kinematic 不接收反作用力），降级方案是把玩家不挂 kinematic body，只让敌人推 debris（玩家推 debris 的视觉就略弱）
- `MoveKinematicBody` 需要 `dt` 参数推算速度（Jolt 用速度推 dynamic body）；如果传 0 dt 会无效，注意正确传入帧 dt
- 如果发现 1160 dynamic body 启动延迟 > 200ms 不可接受，可以降到 800（Tiny=400 / Chunk=320 / BossChunk=80）但视觉密度会减弱

---

## R2. 命中碎块（hit debris）物理化

**优先级**: P0  **工时**: ~1h  **依赖**: R1

### 背景

R1 完成后命中碎块已经走真物理，但视觉上还是「3 块固定红色 tiny box」。本任务把命中碎块的：
- 数量从 3 提升到 4–8（小怪 4 / 中怪 6 / 精英 8）
- 颜色继承「子弹颜色 50% + 敌人颜色 50%」（被打飞的"皮肉碎屑"）
- 飞出方向从「向上 + 随机」改为「沿子弹来向反方向 + 随机散开」（爆炸感更对）
- 暴击 hit 数量 ×1.5 上取整 + 速度 ×1.3（暴击爽快感）

### TODO
- [ ] 在 `Brotato3DGameInstance.hpp` 加 `std::unordered_map<uint64_t, uint32_t> hitDebrisMaterialIds_;`，key 是 `(weaponColorPacked << 32) | enemyColorPacked`（color 用 8bit×3 packed），value 是预创建的 mix material id
- [ ] 加工具函数 `uint32_t Brotato3DGameInstance::EnsureHitDebrisMaterial(const glm::vec3& weaponColor, const glm::vec3& enemyColor)`：
  - 计算 `mix = weaponColor * 0.5 + enemyColor * 0.5`
  - 查 map 命中返回；未命中：`SceneBuilder::AddLambertianMaterial(scene.Materials(), mix)`，存入 map（运行时动态加 material 是否安全要确认；MagicaLego 在线增材质就用同样套路）。**保险做法**：在 `BuildDebrisPool` 里预生成（敌人种类 × 武器种类 = 6 × 6 = 36 个 material 一次性创建）
- [ ] 修改 `SpawnImpactDebris` 函数签名（**改函数签名，不再保留旧签名**）：
  ```cpp
  void SpawnImpactDebris(const glm::vec3& worldPos,
                         const glm::vec3& projectileVelocity,
                         const glm::vec3& weaponColor,
                         const glm::vec3& enemyColor,
                         bool isCrit,
                         const std::string& enemyName);
  ```
  - 数量 `count`：rat / spitter 类 → 4，brute / 中型 → 6，boss / 精英 → 8；isCrit 时 `count = ceil(count * 1.5)`
  - `impulseDir = -normalize(projectileVelocity)` + 微抬 `+ vec3(0, 0.3, 0)` 后 normalize
  - `speed = isCrit ? 6.0f : 4.0f`，每碎块独立乘 0.7..1.3
  - `materialId = EnsureHitDebrisMaterial(weaponColor, enemyColor)`
  - `lifeMs = 600` + 随机 ±100
  - `angleConeRad = 0.6f`（35°）—— 比 R1 默认的「半球」收紧成「向后扇形」
  - 调 R1 的 `SpawnDebris(...)`
- [ ] 修改 [Brotato3DProjectileSystem.cpp:146](../../../src/Application/Brotato3D/Brotato3DProjectileSystem.cpp:146) 命中分支：
  - 把 `SpawnImpactDebris(projectile.worldPos)` 改为 `SpawnImpactDebris(projectile.worldPos, projectile.velocity, projectile.color, enemy.def->color, projectile.isCrit, enemy.def->name)`
  - 注意：`projectile.color` 字段已经存在（[Brotato3DProjectile.hpp:21](../../../src/Application/Brotato3D/Brotato3DProjectile.hpp:21)）；`enemy.def->color` 可以从 `enemy.def` 取
- [ ] AOE / 爆炸命中（同函数下方）：套用同样调用，`weaponColor = explosionColor(1.0,0.55,0.12)`、`enemyColor = enemy.def->color`、`isCrit = false`、数量额外 ×1.5

### 涉及文件
- 改：`Brotato3DGameInstance.hpp`（声明 + map 成员）、`Brotato3DDebrisSystem.cpp`（`SpawnImpactDebris` 重写、`EnsureHitDebrisMaterial`）、`Brotato3DProjectileSystem.cpp`（调用点）

### 验收方法
1. 编译通过
2. SMG 命中 rat：4 块褐黄混色（rat 褐 + smg 黄）小碎块向「子弹反方向」扇形飞出
3. Shotgun pellet 命中 brute：每发 6 块红橙混色（brute 暗红 + shotgun 橙）
4. 暴击命中：碎块数量 ~6 → ~9，飞得更远
5. 同时 30+ 敌人开火不掉帧
6. 重开后无残留

### 注意
- 预生成 material 数量：(6 武器 + 1 explosion) × 6 敌人 = 42 个，可接受；动态生成会让 frame 第一次卡一下（避免）
- **不**要让 hit 碎块对玩家 / 敌人产生物理推力 —— 检查碎块 body 的 layer，必须是 `MOVING` 但不与玩家 / 敌人的 character / kinematic body 交互（玩家此时无 body，所以默认 OK；但要确认敌人也无 body）
- 子弹 `color` 字段在 spawn 时已经被赋值了（[Brotato3DProjectileSystem.cpp](../../../src/Application/Brotato3D/Brotato3DProjectileSystem.cpp) 武器开火段）；如果发现是 `vec3(1,1,1)` 默认值，需要在赋值处加 `projectile.color = weaponDef.projectileColor`

---

## R3. 敌人死亡碎块（chunk）物理化 + 颜色继承 + Material 混合

**优先级**: P0  **工时**: ~1.5h  **依赖**: R1, R2

### 背景

R1 完成后敌人死亡碎块走真物理但是 fallback 灰色，且 Material 还是单独走 sphere SpawnPickup 的旧路径。本任务做三件事：
- 数量提升 + 颜色继承敌人色：rat=8 / spitter=10 / brute=14 / 精英=20 / bomb=12 / boss=R6 接管
- 死亡瞬间碎块从「敌人 box 的 14 个采样点（8 角 + 6 面中心）」分布 spawn
- **Material drop 不再走 SpawnPickup（球）**，改为和死亡碎块**走完全相同的 emission**：在同一发 burst 中追加 `materialDrop` 个 `pickable=true` 的黄色 box，混在死亡碎块中一起被冲量打飞

### TODO

**1. 颜色继承**：
- `FEnemyRuntime` 已经有 `materialId`（皮肤色）和 `darkMaterialId`（皮肤色 × 0.4），死亡碎块直接复用 —— **不需要**新增字段
- 仅需新增 `uint32_t materialDebrisMatId_ = 0;`（黄色 Material box 用，色值 `(1.0, 0.85, 0.15)`）作为 `Brotato3DGameInstance` 私有成员，在 `BeforeSceneRebuild` 一次性创建：
  ```cpp
  materialDebrisMatId_ = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(1.0f, 0.85f, 0.15f));
  ```

**2. 改写 `SpawnDeathDebris(enemy)`**（[Brotato3DEnemySystem.cpp:298](../../../src/Application/Brotato3D/Brotato3DEnemySystem.cpp:298)）：
```cpp
void Brotato3DGameInstance::SpawnDeathDebris(const FEnemyRuntime& enemy)
{
    if (!enemy.def) return;

    // 数量决定（boss 由 R6 接管，这里返回 0）
    int chunkCount = 8;
    if (enemy.def->boss.enabled)             return;
    else if (enemy.def->name == "Spitter")   chunkCount = 10;
    else if (enemy.def->name == "Brute")     chunkCount = 14;
    else if (enemy.def->bomb.enabled)        chunkCount = 12;
    else if (enemy.def->heal.enabled)        chunkCount = 14;

    const int materialCount = enemy.def->materialDrop;   // 例如 rat=1, brute=4, spitter=2

    // 14 个采样点（敌人 AABB 8 角 + 6 面中心），归一化方向
    static const glm::vec3 sampleOffsets[14] = {
        {-1,-1,-1},{-1,-1, 1},{-1, 1,-1},{-1, 1, 1},
        { 1,-1,-1},{ 1,-1, 1},{ 1, 1,-1},{ 1, 1, 1},
        {-1, 0, 0},{ 1, 0, 0},{ 0,-1, 0},{ 0, 1, 0},{ 0, 0,-1},{ 0, 0, 1},
    };
    const glm::vec3 halfExtent = enemy.def->size * 0.5f;

    auto emitOne = [&](int i, bool pickable, uint32_t matId, int matValue)
    {
        const glm::vec3 corner = sampleOffsets[i % 14];
        const glm::vec3 spawnPos = enemy.worldPos + corner * halfExtent;
        const glm::vec3 dir = glm::normalize(corner + glm::vec3(0, 0.4f, 0));
        const float speed = std::uniform_real_distribution<float>(4.0f, 7.0f)(rng_);
        // 80% Tiny + 20% Chunk（让大块少而显眼；pickable 总用 Chunk 让 Material 看起来更重）
        const EDebrisKind kind = pickable ? EDebrisKind::Chunk
                                          : ((i % 5 == 0) ? EDebrisKind::Chunk : EDebrisKind::Tiny);
        SpawnDebris(kind, spawnPos, dir, speed, matId, /*count=*/1,
                    /*angleConeRad=*/0.0f, pickable, matValue);
    };

    // 装饰碎块（70% light + 30% dark），直接复用 FEnemyRuntime 已缓存的 materialId / darkMaterialId
    for (int i = 0; i < chunkCount; ++i)
    {
        const uint32_t matId = (i % 10 < 7) ? enemy.materialId : enemy.darkMaterialId;
        emitOne(i, /*pickable=*/false, matId, 0);
    }

    // Material 拾取物（黄色 Chunk box，混在同一发 burst 里）
    // 起始 index 从 chunkCount 开始，让 Material 用不同 corner，避开和装饰碎块重合
    for (int j = 0; j < materialCount; ++j)
    {
        emitOne(chunkCount + j, /*pickable=*/true, materialDebrisMatId_, /*matValue=*/1);
    }
}
```

**3. 删 SpawnPickup(Material) 调用**（[Brotato3DEnemySystem.cpp:275](../../../src/Application/Brotato3D/Brotato3DEnemySystem.cpp:275)）：
```cpp
// 旧：
// SpawnPickup(enemy.def->materialDrop, EPickupKind::Material, enemy.worldPos);
// 新：什么都不做（Material 已被 SpawnDeathDebris 一并处理）
```
**保留** XP 球的 `SpawnPickup(enemy.def->xpDrop, EPickupKind::XP, ...)`（XP 球继续走原路径，R5 给它加 spawn 弹跳）

**4. 暴击 kill 视觉加成**：在 `ApplyDamageToEnemy` 暴击导致敌人死亡时追加（注意只在「这一击造成死亡」时触发，不要每次暴击都触发）：
- `PushExplosionRing(enemy.worldPos, vec4(1, 0.6, 0.2, 1), 1.2f)`
- `SpawnTempLight(enemy.worldPos, vec3(1, 0.6, 0.2), 4.0f, 250.0f)`
- 不增加碎块数量（保持视觉清晰）

**5. 删旧逻辑**：[Brotato3DEnemySystem.cpp:308-318](../../../src/Application/Brotato3D/Brotato3DEnemySystem.cpp:308) 旧 debrisCount 决定块整段删除

### 涉及文件
- 改：`Brotato3DGameInstance.hpp`（仅加 materialDebrisMatId_ 成员）、`Brotato3DEffectSystem.cpp`（创建 materialDebrisMatId_）、`Brotato3DEnemySystem.cpp`（SpawnDeathDebris 改写 + 删 Material SpawnPickup 调用 + 暴击 kill 视觉）

### 验收方法
1. 编译通过
2. 击杀 rat：8 块褐色 box（含少数深褐）+ **1 块黄色 box（Material）**从 rat 体积内炸开 + 1 个绿色 XP 球
3. 击杀 brute：14 红黑 + **4 黄 Material**从 brute 体积内炸开
4. 击杀 spitter：10 绿 + **2 黄 Material**
5. 落地后地面散落黄 box，玩家走过去时被推动，进入 pickupRadius 时切磁吸吸过来
6. 暴击 kill：额外橙色 ring + 黄光闪
7. wave 5 满场击杀连环不卡（同屏 active body 可能 ~250，仍流畅）
8. wave 切换瞬间所有黄 Material 被清场（玩家来不及捡的失去）—— 这与"波间清场"的产品决定一致

### 注意
- Material 的 `corner index` 从 `chunkCount` 开始而不是从 0 开始（避免和装饰碎块用同一个 corner，视觉上 Material 会被装饰碎块遮挡看不见）—— 测试时验证一下黄 box 位置散得开
- Material box 用 Chunk 尺寸（18cm）而不是 Tiny —— 18cm 黄方块视觉醒目、便于发现
- pickable Material 是 Chunk 池占用 → wave 5 后期可能挤压 cosmetic Chunk 配额，必要时增大 Chunk 池到 600。先按 480 实测
- spitter / bomb 等远程怪自爆 / 子弹也能套用同样套路（在 SpawnDeathDebris 里），但 R3 范围只管"被玩家打死"
- **不要**让 Material box 散开后角度太垂直（emitOne 用了 `dir + (0, 0.4, 0)`），否则 Material 全部飞到 4m 高再落下来过于戏剧化；如果实测过头就把 lift 调到 0.2

---

## R4. 玩家受击碎块

**优先级**: P0  **工时**: ~0.5h  **依赖**: R1

### 背景

每次玩家被打中，除了屏幕震 + 紫色飘字之外，还要在玩家位置喷出 6 块「玩家服装色 + 白色」混合的小碎块，集中、低初速、短命，让"挨打"这件事有空间反馈。

### TODO
- [ ] 在 `Brotato3DGameInstance.hpp` 加 `uint32_t playerDebrisMatId_ = 0;`
- [ ] `BuildDebrisPool` 末尾追加：
  ```cpp
  const glm::vec3 playerColor = characterDefs_.empty() ? glm::vec3(0.20f, 0.75f, 0.30f) : characterDefs_.front().color;
  // 玩家碎块色：服装色 60% + 白色 40%（带"血肉碎屑"质感但不致血腥）
  playerDebrisMatId_ = SceneBuilder::AddLambertianMaterial(materials, playerColor * 0.6f + glm::vec3(0.4f));
  ```
- [ ] 加新函数 `void Brotato3DGameInstance::SpawnPlayerDamageDebris(int damage)`：
  ```cpp
  int count = std::clamp(4 + damage / 5, 4, 10);   // 小伤 4 块、大伤最多 10 块
  glm::vec3 dir(0, 1, 0);
  float speed = damage < 8 ? 2.5f : 3.5f;
  float life = 1500.0f;
  SpawnDebris(EDebrisKind::Tiny, player_.worldPos + glm::vec3(0, 0.4f, 0), dir, speed,
              playerDebrisMatId_, life, count, glm::pi<float>() * 0.5f);
  ```
- [ ] 在 `DamagePlayer`（[Brotato3DPlayerSystem.cpp:232](../../../src/Application/Brotato3D/Brotato3DPlayerSystem.cpp:232)）的 `player_.currentHp -= damage;` 之后追加调用 `SpawnPlayerDamageDebris(damage);`
- [ ] **不要**在玩家死亡时叠加更多碎块 —— 死亡时还有 explosion ring + 慢动作可以做，本任务只管「受击」反馈

### 涉及文件
- 改：`Brotato3DGameInstance.hpp`（playerDebrisMatId_）、`Brotato3DDebrisSystem.cpp` 或 `Brotato3DEffectSystem.cpp`（追加 `SpawnPlayerDamageDebris`）、`Brotato3DPlayerSystem.cpp`（在 DamagePlayer 末尾调用）

### 验收方法
1. 编译通过
2. 让 rat 撞玩家几次：每次玩家位置喷出 4–6 块绿白混色小碎块，1.5s 内消失
3. 被 brute 撞：碎块数量提升到 ~10
4. 玩家死亡瞬间也喷一次（不需要特别加成）

### 注意
- 玩家本身不需要变成物理 body（保持 kinematic clamp），碎块从玩家位置 spawn 不会有任何物理穿透问题
- speed 不要超过 4 m/s —— 玩家碎块飞太远像「玩家爆炸了」，不像「擦伤」

---

## R5. XP 球小弹跳 + 旧 Pickup 系统瘦身

**优先级**: P0  **工时**: ~1h  **依赖**: R3

### 背景

R1 已经在 debris pool 里实装了 pickable slot 状态机（Physics → Settling → Magnetic），R3 已经把 Material 走死亡碎块同 emission。本任务收尾：
- 给 XP 球 spawn 加 0.3s 伪物理小弹跳，让 XP 和 Material 的"撒出来"节奏一致
- 把旧 `FPickupRuntime` / `pickupPool_` 里的 Material 分支彻底废弃（pool 从 256 降到只剩 128 个 XP slot），代码和池规模都瘦下来
- 验证 pickable debris 的状态机端到端

### TODO

**1. 旧 Pickup pool 瘦身**：
- `Brotato3DPickup.hpp` 的 `EPickupKind`：保留 `XP`，删 `Material`（Material 现在归 debris 管）
- 把 `Brotato3DEffectSystem.cpp` 的 pickup 池构造（约 [Brotato3DEffectSystem.cpp:213](../../../src/Application/Brotato3D/Brotato3DEffectSystem.cpp:213)）的 256 个 slot 改为 128 个，全部初始化为 XP kind
- `pickupMaterialModelId_` / `pickupMaterialMaterialId_` 删除（不再创建黄色球 model / material）
- `SpawnPickup(value, kind, ...)`（[Brotato3DPickupSystem.cpp:12](../../../src/Application/Brotato3D/Brotato3DPickupSystem.cpp:12)）函数体内删除 `kind == Material` 分支；调用方传 Material 时 spdlog warn 并 return（双保险，R3 已删调用）
- `UpdatePickups`（[Brotato3DPickupSystem.cpp:60](../../../src/Application/Brotato3D/Brotato3DPickupSystem.cpp:60)）内删除 Material 分支

**2. XP 球 spawn 弹跳**：
- `FPickupRuntime` 加：
  ```cpp
  float bouncePhysicsMs = 0.0f;
  glm::vec3 bounceVelocity = glm::vec3(0.0f);
  ```
- `SpawnPickup` XP 分支末尾追加：
  ```cpp
  std::uniform_real_distribution<float> hd(-1.5f, 1.5f);
  slot->bouncePhysicsMs = 300.0f;
  slot->bounceVelocity = glm::vec3(hd(rng_), 4.0f, hd(rng_));
  slot->magnetized = false;     // 弹跳期间不磁吸
  ```
- `UpdatePickups` XP 分支顶部追加：
  ```cpp
  if (pickup.bouncePhysicsMs > 0.0f)
  {
      pickup.bounceVelocity.y -= 12.0f * static_cast<float>(deltaSeconds);
      pickup.worldPos += pickup.bounceVelocity * static_cast<float>(deltaSeconds);
      pickup.worldPos.y = std::max(0.15f, pickup.worldPos.y);
      pickup.bouncePhysicsMs -= static_cast<float>(deltaSeconds * 1000.0);
      pickup.node->SetTranslation(pickup.worldPos);
      if (pickup.bouncePhysicsMs <= 0.0f)
      {
          pickup.bouncePhysicsMs = 0.0f;
      }
      continue;     // 弹跳期间跳过磁吸
  }
  ```
  保留原磁吸逻辑

**3. wave 清场对 Material 的处理**：R1 的 `ClearAllDebris(false)` 在 wave 切换时已经包括 pickable slot —— 这意味着未拾取的 Material 在 wave 末尾全部消失。这是产品决策（避免 Material 跨波累积破坏经济）。在 R5 验收时确认这个行为符合期望；若不符合，可改 `ClearAllDebris(/*keepPickable=*/true)`。

**4. NextPhysics 速度查询验证（与 R1 对齐）**：
- 实测 [`physics->GetBody(bodyId)->velocity`](../../../src/Runtime/Subsystems/NextPhysics.h:74) 是否每帧反映 Jolt 内部最新值
- 若不可靠：把 R1 在 `UpdateDebris` 内的 Settling 状态改成纯计时器（`Physics 600ms → Settling 400ms → Magnetic`），不查速度。这是兜底方案，体验差距很小

### 涉及文件
- 改：`Brotato3DPickup.hpp`（删 Material kind、加 bounce 字段）、`Brotato3DEffectSystem.cpp`（pickup 池规模 256→128，删 Material 球 model/material）、`Brotato3DPickupSystem.cpp`（SpawnPickup / UpdatePickups XP 分支）、`Brotato3DEnemySystem.cpp`（确认 Material SpawnPickup 调用已被 R3 删掉）

### 验收方法
1. 编译通过
2. 击杀 rat：1 个绿色 XP 球（明显弹跳一下，约 0.3s 后开始磁吸） + 1 个黄色 Material box（混在 8 块褐色 cosmetic 碎块中飞出）
3. XP 球弹跳节奏和 Material box 飞溅节奏视觉一致（同时 spawn、同时大致落地）
4. 击杀 brute：4 个黄色 Material box 在 14 块红黑碎块中一起被炸开
5. 玩家走过 Material box 堆：进入 pickupRadius 时所有滚动中的黄 box 立即停止物理切磁吸，吸过来 +N MAT
6. 玩家走过 XP 球：磁吸吸过来，到玩家身上加 XP，满级时弹升级 modal
7. wave 切换瞬间地面所有 Material box（已落地未磁吸）和 cosmetic 碎块一起清空（产品决策已对齐）
8. 升级 modal 期间游戏暂停：黄 box 物理也停止（OnTick 早返回不调 UpdateDebris）
9. 重开局后场地全清

### 注意
- XP 球继续是 sphere（不变），仅 Material 是 box —— 这样玩家一眼能区分两种掉落（一圆一方）
- 如果 XP 球弹跳太「Q弹」（4 m/s 上抛太高），调到 3.0；高度大概到 0.6m 即可
- pickup pool 从 256 → 128 不会影响内存，仅是清理代码膨胀
- 假设 P10 拆系统重构已完成，`Brotato3DPickupSystem.cpp` 文件存在；如未完成，操作 `Brotato3DGameInstance.cpp` 内的相同函数即可
- **不要**让 XP 球也走真物理 —— 升级动作高频，磁吸的"自动收敛"是核心反馈，物理化反而拖慢

---

## R6. Boss 死亡高潮 + 大碎块 + 慢动作

**优先级**: P1（锦上添花，没做也不影响 Phase 4 验收线 1–6 中的 1–5）  **工时**: ~1h  **依赖**: R3, R5

### 背景

Boss 死亡是整个 demo 的高潮时刻。R3 把它的死亡碎块数量留空（chunkCount = 0）专门给 R6 抛光：
- 50 块 chunk 级（18cm）+ 8 块 boss_chunk 级（36cm）从 boss AABB 各位置炸出
- 1.5s 局部慢动作：`appState = Hitstop` 80ms 后切回 Playing，但全局 `timeScaleMultiplier` 0.4 → 1.0 缓动 1.2s（仅影响敌人 / 子弹 / debris，不影响 UI）
- 双 explosion ring（小+大）+ 大 temp light（200ms 800-intensity）+ 屏震 800ms 强度 5
- 100ms 全屏白闪（ImGui foreground 全屏白色 alpha 1 → 0）
- Boss 留下 3× materialDrop（不通过 SpawnPickup 的 random 半球，而是显式撒在 boss 周围 1m 的圆周上）

### TODO
- [ ] 在 `Brotato3DGameInstance.hpp` 加 `float globalTimeScale_ = 1.0f;`、`float bossKillFlashMs_ = 0.0f;`、`float timeScaleRecoveryMs_ = 0.0f;`
- [ ] 在 `OnTick` 顶部计算实际 dt：
  ```cpp
  if (timeScaleRecoveryMs_ > 0.0f) {
      timeScaleRecoveryMs_ = std::max(0.0f, timeScaleRecoveryMs_ - static_cast<float>(deltaSeconds * 1000.0));
      globalTimeScale_ = glm::mix(0.4f, 1.0f, 1.0f - timeScaleRecoveryMs_ / 1200.0f);
  } else {
      globalTimeScale_ = 1.0f;
  }
  const double effectiveDt = deltaSeconds * globalTimeScale_;
  ```
  - **把所有 update 调用改用 `effectiveDt`**：`UpdateProjectiles / UpdateEnemies / UpdateDebris / UpdatePickups / UpdateCombatEffects / UpdateImpactDebris(已删) / UpdateFloatingTexts`
  - **不**用 effectiveDt：`UpdatePlayer`（玩家不要被慢）、`UpdateWaveBanner`、屏震、HUD、UI 动画
- [ ] 改 `KillEnemy` 的 boss 分支（[Brotato3DEnemySystem.cpp:278](../../../src/Application/Brotato3D/Brotato3DEnemySystem.cpp:278)）：
  ```cpp
  if (enemy.def->boss.enabled) {
      // 50 chunk（颜色直接用 enemy.materialId / darkMaterialId）
      for (int i = 0; i < 50; ++i) {
          glm::vec3 dir = glm::normalize(glm::vec3(uniform(-1,1), uniform(0.4f, 1.0f), uniform(-1,1)));
          uint32_t mat = (i % 4 == 0) ? enemy.darkMaterialId : enemy.materialId;
          SpawnDebris(EDebrisKind::Chunk, enemy.worldPos + dir * 0.2f, dir, uniform(5, 9), mat, 1, 0.0f);
      }
      // 8 boss_chunk
      for (int i = 0; i < 8; ++i) {
          glm::vec3 dir = ...;
          SpawnDebris(EDebrisKind::BossChunk, enemy.worldPos + dir * 0.4f, dir, uniform(6, 10), enemy.materialId, 1, 0.0f);
      }
      // 撒一圈 Material box（pickable=true，使用 materialDebrisMatId_）
      for (int i = 0; i < enemy.def->materialDrop * 3; ++i) {
          float angle = float(i) / (enemy.def->materialDrop * 3) * glm::two_pi<float>();
          glm::vec3 spawnPos = enemy.worldPos + glm::vec3(cos(angle), 0.5f, sin(angle)) * 1.0f;
          glm::vec3 dir = glm::normalize(glm::vec3(cos(angle), 0.6f, sin(angle)));
          SpawnDebris(EDebrisKind::Chunk, spawnPos, dir, 5.0f, materialDebrisMatId_, 1, 0.0f, /*pickable=*/true, /*matValue=*/1);
      }
      // 双 ring
      explosionRings_.push_back({enemy.worldPos, vec4(1, 0.72f, 0.18f, 1), 800.0f, 800.0f, 4.0f});
      explosionRings_.push_back({enemy.worldPos, vec4(1, 0.92f, 0.4f, 1), 1200.0f, 1200.0f, 8.0f});
      SpawnTempLight(enemy.worldPos, vec3(1, 0.85f, 0.4f), 12.0f, 800.0f);
      StartScreenShake(800.0f, 5.0f);
      bossKillFlashMs_ = 100.0f;
      timeScaleRecoveryMs_ = 1200.0f;
      EnterResult(false);
  }
  ```
- [ ] 全屏白闪：在 `Brotato3DUI::OnRenderUI` 顶部追加：
  ```cpp
  if (bossKillFlashMs_ > 0.0f) {
      bossKillFlashMs_ = std::max(0.0f, bossKillFlashMs_ - static_cast<float>(io.DeltaTime * 1000.0));
      const float alpha = bossKillFlashMs_ / 100.0f;
      ImGui::GetForegroundDrawList()->AddRectFilled(ImVec2(0,0),
          ImVec2(io.DisplaySize.x, io.DisplaySize.y),
          ImColor(1.0f, 1.0f, 1.0f, alpha));
  }
  ```

### 涉及文件
- 改：`Brotato3DGameInstance.hpp`（globalTimeScale_ 等）、`Brotato3DGameInstance.cpp` 或 `Brotato3DPlayerSystem.cpp`（OnTick 改 effectiveDt 分发）、`Brotato3DEnemySystem.cpp`（KillEnemy boss 分支）、`Brotato3DUI.cpp`（白闪）

### 验收方法
1. 编译通过
2. 通关到 wave 10 boss，把它打死：
   - 50+ 大块碎块 + 8 块更大碎块从 boss 位置炸开
   - 屏幕短暂白闪 100ms
   - 慢动作 1.2s 让玩家「品尝」碎片飞溅
   - 双 ring + 黄光照亮整个场地
   - 屏震 800ms
   - boss 周围 1m 圆周散布 ~12 个 Material 球
3. 慢动作期间玩家移动**不慢**（玩家可以从容走过去捡战利品）
4. 慢动作恢复后游戏正常切到 Result UI

### 注意
- 慢动作期间 UI 倒计时 / 飘字动画建议保持原速（不要慢字体淡出，会显得拖沓）
- 8 boss_chunk 数量可调，太多影响视觉清晰度
- 不要给慢动作做无限叠加 —— 同一时间只允许一个 timeScaleRecoveryMs_
- 如果玩家很快连击多个 boss（理论上 demo 只有一个 boss，不会发生），第二次 trigger 直接覆盖即可

---

## 公共约束（所有任务必读）

| 项 | 值 |
|---|---|
| 构建 | `./build.bat --preset full-windows --reconfigure` |
| 运行 | `./run.bat --preset full-windows --target Brotato3D` |
| 物理总预算 | 启动后总 body：debris dynamic 1160 + 玩家 kinematic 1 + 敌人 kinematic 池 ~80 ≈ 1240；满 wave 同时 active dynamic body 不超过 400（其余 sleeping） |
| 不要写注释 | 默认不写；只在 WHY 不显然时写一行（如「pickup body layer 不与玩家交互避免被顶飞」） |
| 不引新依赖 | 仅 `NextPhysics`（已有） |
| 不动 UI 文案 | UI 文本继续用 P10 的 Localization（如果 P10 完成）或英文 fallback |
| 不动 wave / 升级 / 商店逻辑 | 严禁顺手改这些 |
| 提交 | 不要执行 git commit；用户自己决定何时提交 |
| 沟通 | 与用户用中文对话 |

**禁止**：
- 修改 `src/ThirdParty/` 或 `external/`
- 在玩家 / 敌人 / 子弹上挂 **Dynamic** PhysicsComponent —— **只允许 Kinematic**（仅用于推动 debris，不参与战斗碰撞）；子弹完全不挂 body
- 把碎块用作伤害判定（碎块是纯视觉 + 推力，不造成伤害）
- 在 BeforeSceneRebuild 之外创建 body（必须全部预创建，运行时只 SetBodyActive 切）
- 一帧内 spawn > 60 块碎块同时（boss 死亡瞬间 50+8 是上限；本规则是软上限提醒）

## 验证完整端到端

完成 R1–R6 后，端到端跑一遍：

1. `./build.bat --preset full-windows --reconfigure` 通过
2. 启动后日志看到 `debris pool created: 1160 dynamic bodies` + 玩家 kinematic 1 + 敌人 kinematic ~80
3. 击中 rat：4 块褐黄 box 向子弹反方向飞出，落地翻滚后**留在场上**
4. 击杀 rat：8 块褐色 / 暗褐 box（其中部分大块 Chunk）+ 1 个黄色 Material box 一起从 rat 体积内炸开 + 1 个绿色 XP 球弹跳一下
5. 走到黄 Material box 附近：进入 pickupRadius 时所有滚动中的黄 box 立即切磁吸 + 飞向玩家
6. 走到 cosmetic 碎块堆里：**玩家身体能把碎块推开**（kinematic body 推 dynamic）
7. 一只 rat 朝玩家冲来时，**它走过的路径上的碎块也被推开**
8. 挨打：玩家位置喷绿白小 box
9. 暴击 brute：14 块红黑碎块（含 3 大 Chunk）+ 4 块黄 Material box + explosion ring
10. wave 中段：场上累积 ~200 块碎块（cosmetic + 几十个待拾取 Material），60fps 不卡
11. wave 切换：所有碎块 + 未拾取的 Material 一起清空
12. boss 死亡：50 Chunk + 8 BossChunk + Material 撒一圈 + 双 ring + 慢动作 + 白闪
13. 重开局：场地全清空，所有 body deactivate

**不需要**：单元测试（视觉效果手玩验证最直接）；视觉测试（demo 高度交互式不进 visual_test.json）。

## 风险与备注

| 风险 | 应对 |
|---|---|
| Jolt 创建 1160 dynamic body + ~80 kinematic 启动延迟 | 实测；中端机器 1k+ body 创建 < 100ms 不会感知。若超 200ms，先实测再考虑分帧创建。 |
| 物理 body 数量过多导致 Jolt 内部 body pool 扩容 | NextPhysics 内部应该已 reserve；如出现，给 NextPhysicsContext 加 `MaxBodies = 4096` 配置（先实测再说） |
| 重开局时旧 body 残留场地上滚 | 必须调 `ClearAllDebris(false)` 把全部 1160 个 debris body deactivate + transform 复位 + 全部 pickable 标记 false |
| 暂停期间 body 还在模拟 | 简化方案：忽略（暂停短，物理继续滚无关大局）；如不接受，遍历手动 deactivate 然后恢复时再 active（开销可控，~1160 次 SetBodyActive） |
| 碎块挤压把玩家 / 敌人顶飞 | 玩家 / 敌人是 Kinematic，**不接收**外力（Jolt 默认行为：Kinematic 只主动推动 Dynamic，不被反推）；实测验证。若实际被推开，把 debris 放进与 player/enemy kinematic 不交互的 layer |
| Material box 被 Tiny 碎块持续撞导致永远不 settle | 设置 settle 计时器兜底（800ms 强制切 Magnetic 不查速度），玩家进 pickupRadius 强切 |
| 暴击 + 多敌人同时死亡导致一帧 spawn 200+ debris | LRU 抢占覆盖最旧；视觉上一瞬间多碎片完全 OK |
| Material 落到边界条停在边界 | 默认 OK，玩家走近自动磁吸；若觉得突兀，可以在 Settling 阶段检测 `if y > 0.6` 瞬移回地面（不必要的优化） |
| `MoveKinematicBody` API 行为不符 | NextPhysics 暴露 `MoveKinematicBody(bodyID, position, rotation, deltaSeconds)`（[NextPhysics.h:70](../../../src/Runtime/Subsystems/NextPhysics.h:70)）—— 内部用 dt 算速度。dt 必须是真实帧 dt 不能传 0；若发现敌人 kinematic body 不推 debris，第一步检查 dt 传值 |
| 玩家 kinematic body 阻挡玩家移动 | Kinematic body 不参与碰撞响应玩家逻辑（玩家移动用 clamp，body 仅"跟随"位置）；若发现移动卡顿，简化方案：玩家不挂 kinematic body，只让敌人推 debris |

## 后续 agent 调用建议

每个任务（R1, R2, ...）适合用一个独立 agent 调用执行，prompt 模板：

```
请执行 docs/projects/brotato-3d/feel-polish-plan.md 中的 R{N} 任务。
- 严格按 TODO 清单做，不要扩大范围
- 完成后用 full-windows preset 编译验证
- 完成「验收方法」中的所有点，逐条勾选汇报；视觉验收点用文字描述自己怎么验证的
- 不要 commit
- 与用户沟通用中文
- 物理 body 全部在 BeforeSceneRebuild 阶段预创建，运行时只 SetBodyActive 切；如果发现 BeforeSceneRebuild 会被多次调用（重开局），先 RemoveBody 已有 body 再重建池
```

R1 单独跑（必前置，含 kinematic body 基础设施）；R2 / R4 可以同会话并行（不同 agent）；R3 跟在 R2 后面（颜色继承 + Material 混合 emission）；R5 跟在 R3 后面（pickable 状态机端到端验证）；R6 最后做。

## 后续扩展方向（不在本计划内）

- **Hit-stop 升级**：除了 brute 撞玩家之外，玩家暴击 + 击杀也触发 30ms hit-stop（强化打击感）
- **碎块互相伤害**：boss 死亡的 boss_chunk 撞到附近敌人造成伤害（玩法层叠加，需要平衡）
- **碎块继承运动模糊 / Trail**：高速 debris 拖出短 trail（需要新 shader pass，超出 ProcModel 路线）
- **环境破坏**：边界条 / 地面在 boss 死亡处可见焦痕（需要 decal 系统，超出范围）
- **Material 球磁吸路径上的物理排斥**：避免被磁吸的球穿过其他球（视觉小毛病，可忽略）
- **音效与碎块联动**：每块碎块落地播 1 个微小 thud 声（可能太吵，先不做）
