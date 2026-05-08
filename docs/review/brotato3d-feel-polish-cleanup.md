# Brotato3D 手感打磨 — Code Review 后续清理任务

> 针对 `dbdfacf8 Polish Brotato3D combat feel` + `b841e31a Polish Brotato3D physics feedback` 两次提交的代码质量问题做一轮打磨。玩法和体验已经验收通过，本次任务**只改代码结构和质量问题，不要动玩法、数值、视觉手感**。

## 背景

这两次提交把 Brotato3D 的碎块系统从伪物理升级到了 Jolt 真物理，新增了 debris pool（Tiny/Chunk/BossChunk 三档 box）、kinematic body 推动机制、boss 死亡慢动作 / 全屏白闪、暴击 ring、武器击退（knockback）等。现在已经走完一轮 code review，下面是要修的清单。

## 公共约束

- 构建：`./build.bat --preset full-windows --reconfigure`
- 运行：`./run.bat --preset full-windows --target Brotato3D`
- 命名：类型/函数 PascalCase；变量/参数 camelCase；私有成员 trailing 下划线；常量 camelCase
- 头文件首行 `#include "Common/CoreMinimal.hpp"`
- 默认不写注释，仅写非显然的 WHY
- 不要 commit；用户自己决定何时提交
- 与用户沟通用中文

**不要做的事**：
- 不要改 `assets/configs/brotato3d/*.json` 的数值
- 不要改 spawn 数量、冲量大小、material 颜色、knockback 系数等手感参数
- 不要重写 `BeforeSceneRebuild` 整个流程，只按下面的 TODO 做局部调整
- 不要碰 `src/ThirdParty/` 或 `external/`

## P1 — 必须修

### 1. 飘字 / 战斗特效不应只在 Playing 状态更新

**文件**：`src/Application/Brotato3D/Brotato3DGameInstance.cpp:226-231`

当前：
```cpp
if (appState_ == Brotato3D::EAppState::Playing)
{
    UpdateDebris(effectiveDt);
    UpdateCombatEffects(effectiveDt);
    UpdateFloatingTexts(deltaSeconds);
}
```

问题：升级 modal / 商店 / 结算屏幕弹出瞬间的飘字会永远停在屏幕上直到玩家关 modal。`UpdateCombatEffects`（muzzleFlash / explosionRings / laserBeams / tempLights）暂停期间冻结合理，但 `UpdateFloatingTexts` 是纯 UI 计时器，必须实时跑。

**改成**：
```cpp
if (appState_ == Brotato3D::EAppState::Playing)
{
    UpdateDebris(effectiveDt);
    UpdateCombatEffects(effectiveDt);
}
UpdateFloatingTexts(deltaSeconds);
```

验收：手动跑一局，触发升级 modal 时检查飘字（"+1 XP" / "-N" 伤害飘字）能正常淡出消失，不会卡在屏幕上。

### 2. `SyncPlayerKinematicBody` / `SyncEnemyKinematicBody` 必须传真实 dt

**文件**：`src/Application/Brotato3D/Brotato3DDebrisSystem.cpp`

当前：
- 顶部 `constexpr float KinematicMoveTimeSeconds = 0.01f;`（line 24）
- `SyncPlayerKinematicBody`（line 658）和 `SyncEnemyKinematicBody`（line 697）都先 `(void)deltaSeconds;` 主动丢弃帧 dt，然后调 `physics->MoveKinematicBody(..., KinematicMoveTimeSeconds);` 传死的 10ms。
- `ApplyWeaponKnockback` 末尾也是 `SyncEnemyKinematicBody(enemy, 1.0 / 60.0);` 写死 60fps（`Brotato3DProjectileSystem.cpp:46` 附近）。
- `SpawnEnemy` 末尾两处 `SyncEnemyKinematicBody(..., 1.0 / 60.0);`（`Brotato3DEnemySystem.cpp:72, 83`）也是写死。

`MoveKinematicBody` 用这个 dt 算速度推 dynamic body，传死值意味着推力跟帧率挂钩。

**改法**：
- 删掉 `KinematicMoveTimeSeconds` 常量
- `SyncPlayerKinematicBody` / `SyncEnemyKinematicBody` 移除 `(void)deltaSeconds;` 一行，把 `MoveKinematicBody(..., KinematicMoveTimeSeconds)` 改成 `MoveKinematicBody(..., std::max(1e-4f, static_cast<float>(deltaSeconds)))`
- `ApplyWeaponKnockback` 末尾的 `SyncEnemyKinematicBody(enemy, 1.0 / 60.0);`：knockback 是瞬移（不是速度推动），保留这个 1/60 是合理的兜底（让 Jolt 用合理推力同步到目标位置），但加一行注释说明 why
- `SpawnEnemy` 里的 `SyncEnemyKinematicBody(..., 1.0 / 60.0);`：spawn 时是位置初始化，同上保留 1/60，加注释

验收：编译通过；满 wave 5 wasd 在碎块堆里跑，碎块推开行为不应该比之前明显变化。

### 3. Kinematic 代理 Node 的 hack 加 TODO 注释 + 修复 proxy 颜色

**文件**：`src/Application/Brotato3D/Brotato3DDebrisSystem.cpp:233-253`

当前：
```cpp
const uint32_t proxyMaterialId = SceneBuilder::AddLambertianMaterial(materials, glm::vec3(0.1f, 0.9f, 1.0f));   // cyan
...
auto physicsComponent = std::make_shared<Runtime::PhysicsComponent>();
// Keep Scene::RebuildMeshBuffer from replacing this manually-created primitive body with a mesh body.
physicsComponent->SetMobility(Runtime::ENodeMobility::Dynamic);
physicsComponent->BindPhysicsBody(bodyId);
```

问题：
1. 实际 body 是 Kinematic，PhysicsComponent 谎报 Dynamic，纯粹绕过 `Scene::RebuildMeshBuffer`
2. proxy 颜色 cyan 像调试残留
3. 这是引擎缺陷的 workaround，需要 visible 标记给后人

**改法**：
- proxy 颜色改成 `glm::vec3(0.0f, 0.0f, 0.0f)`（黑色，假设以后误显示也不刺眼）
- 在 `attachPhysicsProxyNode` lambda 上方加注释块：
  ```cpp
  // HACK: Scene::RebuildMeshBuffer auto-promotes manually-created primitive bodies to mesh bodies
  // when it sees a Node without a PhysicsComponent that owns the body. Workaround: attach a hidden
  // proxy Node with a PhysicsComponent (mobility tagged Dynamic to opt out of mesh promotion) that
  // claims ownership of the kinematic body. The body itself stays Kinematic at the physics layer.
  // TODO: replace with a `PhysicsComponent::IsManuallyBound()` flag in the engine layer.
  ```
- 同时给 `EnableKinematicDebrisPush` 上方加一行 TODO 引用同一 issue：
  ```cpp
  // TODO: drop EnableKinematicDebrisPush guard once the kinematic body engine API stabilizes.
  ```

不要删 `EnableKinematicDebrisPush` 死分支（P3 才碰），只加 TODO。

验收：编译通过，运行时不应有视觉变化（proxy node 仍 invisible）。

## P2 — 建议改

### 4. 把 pickup pool 的 build/teardown 抽成专门函数

**文件**：`src/Application/Brotato3D/Brotato3DEffectSystem.cpp:204-241`

当前：pickup pool 的 RemoveBody + clear + reserve + 128 slot 创建散在 `BeforeSceneRebuild` 里，紧接着还有一个奇怪的双重 `NextPhysics* physics` 声明（第一个在 line 207 if-scoped，第二个在 line 219 函数级）。

**改法**：
- 在 `Brotato3DEffectSystem.cpp` 或 `Brotato3DPickupSystem.cpp`（哪个文件位置自然就放哪个）新增成员函数：
  ```cpp
  void Brotato3DGameInstance::BuildPickupPool(std::vector<Assets::Model>& models,
                                              std::vector<Assets::FMaterial>& materials,
                                              std::vector<std::shared_ptr<Assets::Node>>& nodes);
  ```
- 函数内：先 RemoveBody 旧 pickup body（同 BuildDebrisPool 风格），clear pickupPool_，创建 model + material + 128 slot
- `BeforeSceneRebuild` 调用点替换为单行 `BuildPickupPool(models, materials, nodes);`
- `Brotato3DGameInstance.hpp` private 区加声明

风格对齐 `BuildDebrisPool` / `BuildKinematicCollisionBodies` / `Brotato3D::BuildArena`。

### 5. `OnTick` 主循环简化

**文件**：`src/Application/Brotato3D/Brotato3DGameInstance.cpp:165-214`

当前有冗余 `if (appState_ == Playing)` 嵌套（外层已保证），还有把 wave update 拆成 if/else 仅 dt 不同的双分支。

**改法**：
```cpp
if (appState_ == Brotato3D::EAppState::Playing)
{
    if (bossVictoryDelayMs_ > 0.0f)
    {
        bossVictoryDelayMs_ = std::max(0.0f, bossVictoryDelayMs_ - deltaMs);
        if (bossVictoryDelayMs_ <= 0.0f)
        {
            EnterResult(false);
        }
    }

    if (appState_ == Brotato3D::EAppState::Playing)   // EnterResult 可能改了 appState_
    {
        runElapsedSec_ += static_cast<float>(deltaSeconds);
        UpdatePlayer(deltaSeconds);
        UpdateWeapons(deltaSeconds);
        UpdateProjectiles(effectiveDt);
        UpdateEnemies(effectiveDt);
        UpdateEnemyProjectiles(effectiveDt);
        UpdatePickups(effectiveDt);
        ProcessItemTriggers(effectiveDt);

        const double waveDt = bossVictoryDelayMs_ <= 0.0f ? deltaSeconds : 0.0;
        waveSystem_.Update(waveDt, [this](const std::string& enemyId, glm::vec3 pos)
        {
            SpawnEnemy(enemyId, pos);
        });

        if (waveSystem_.ConsumeWaveEnded())
        {
            ClearAliveEnemies(false);
            ClearAllDebris(false);
        }
        if (waveSystem_.ConsumeIntermissionStarted())
        {
            StartShopping();
        }
        if (waveSystem_.ConsumeVictory())
        {
            EnterResult(false);
        }
    }
}
else if (appState_ == Brotato3D::EAppState::Hitstop)
{
    ...
}
```

注意保留第二层 `if (appState_ == Playing)`（`EnterResult` 可能改 state），但删掉外层冗余、合并 wave dt 三元。

### 6. 在 `UpdateWeapons` 上方加注释解释时间缩放不对称

**文件**：`src/Application/Brotato3D/Brotato3DProjectileSystem.cpp` 的 `UpdateWeapons` 函数定义上方

当前 OnTick 里：
```cpp
UpdatePlayer(deltaSeconds);          // 实时
UpdateWeapons(deltaSeconds);         // 实时
UpdateProjectiles(effectiveDt);      // 慢速
```

加注释：
```cpp
// Note: weapons fire at real-time even during boss-kill slow-motion (effectiveDt < 1.0).
// This intentional asymmetry lets the player keep shooting while bullets/enemies/debris crawl,
// emphasizing the slow-mo "savor the kill" beat. Don't "fix" this by passing effectiveDt.
```

### 7. `UpdateDebris` 的 pickable 三态机简化为二态

**文件**：`src/Application/Brotato3D/Brotato3DDebrisSystem.cpp:464-477`

当前：
```cpp
if (slot.pickupState == EPickupState::Physics)
{
    slot.settleTimerMs -= deltaMs;
    if (slot.settleTimerMs <= 0.0f)
    {
        slot.pickupState = EPickupState::Settling;
        slot.settleTimerMs = 400.0f;
    }
}
else if (slot.pickupState == EPickupState::Settling)
{
    slot.settleTimerMs = std::max(0.0f, slot.settleTimerMs - deltaMs);
}
```

实际两个状态行为完全相同（都只是计时器），只是 timer 值不同。Settling 状态没有产生任何观察行为差异。

**改法**：
- `Brotato3DDebris.hpp` 的 `EPickupState`：保留 `None / Physics / Magnetic`，删除 `Settling`
- `SpawnDebris` 里 spawn pickable slot 时 `settleTimerMs = 1000.0f;`（600 + 400 = 1000ms 总等待，等价于原行为）
- `UpdateDebris` 把 if/else if 合并：
  ```cpp
  if (slot.pickupState == EPickupState::Physics)
  {
      slot.settleTimerMs = std::max(0.0f, slot.settleTimerMs - deltaMs);
      // settle 计时归零后保持 Physics 状态等玩家进半径，下方代码会处理 Magnetic 切换
  }
  ```

注意：原有"靠近玩家就强切 Magnetic"的代码逻辑不要动（`if (slot.pickupState != Magnetic && DistanceXZ(...) < pickupRadius)` 那段），因为它本来就 cover Physics 和 Settling 两态。

验收：玩一局打几只 rat，黄 box 落地后 1 秒玩家走过去仍然能磁吸，行为应跟之前一致。

### 8. Material 与 XP 的 magnet 逻辑抽公用

**文件**：`src/Application/Brotato3D/Brotato3DDebrisSystem.cpp` 的 `UpdateDebris`（pickable 分支） + `Brotato3DPickupSystem.cpp` 的 `UpdatePickups`

两边各有一份"距离玩家 < pickupRadius → 切磁吸 → lerp 朝玩家追 → distance < 0.4 拾取"逻辑。XP 用 lerp 速度 8.0、Material 用 12.0。

**只做最小重构**：在 `Brotato3DCommon.hpp` 加：
```cpp
inline constexpr float MagnetLerpSpeedXp = 8.0f;
inline constexpr float MagnetLerpSpeedMaterial = 12.0f;
inline constexpr float PickupClaimDistance = 0.4f;
```
然后两处的 `8.0f` / `12.0f` / `0.4f` 都换成对应常量。

**不要做更大的重构**（比如把 XP 也搬进 debris pool）—— 那是更大的设计变更，超出本次范围。

## P3 — 可选清理

### 9. 散落魔数搬到 Brotato3DCommon.hpp

**文件**：`src/Application/Brotato3D/Brotato3DCommon.hpp` + `Brotato3DDebrisSystem.cpp`

`Brotato3DDebrisSystem.cpp` 里这些应该提常量：
- `0.15f` y offset（target pickup y 高度）
- `5.0f` magnetic progress 速度
- `0.08f` 随机偏移幅度（spawn 位置抖动）
- `0.7f / 1.3f` speed jitter
- `8.0f / 16.0f` 角速度范围
- `0.25f / 0.65f` lift 范围
- `0.001f` 各种长度阈值

加到 `Brotato3DCommon.hpp` 或 `Brotato3DDebrisSystem.cpp` 顶部 anonymous namespace 都可以；优先选后者（这些常量只 debris system 用）。命名用 `kDebrisXxx` 风格或保持项目原有 PascalCase 风格（参考 `TinyHalfExtent`、`PickupBaseRadius`）。

### 10. `RatKinematicBodyCount` 数据驱动

**文件**：`assets/configs/brotato3d/enemies.json` + `Brotato3DDataLoader.{hpp,cpp}` + `Brotato3DDebrisSystem.cpp:261-265`

当前：
```cpp
const int count = def.boss.enabled ? BossKinematicBodyCount :
                  (def.name == "Rat" ? RatKinematicBodyCount : EnemyKinematicBodyCount);
```

改法：
- `FEnemyDef` 加可选字段 `int kinematicBodyPoolSize = 32;`
- `LoadEnemies` 用 `enemyJson.value("kinematicBodyPoolSize", 32)` 解析
- enemies.json 里给 rat 加 `"kinematicBodyPoolSize": 96`，给 boss 加 `"kinematicBodyPoolSize": 4`，其他不写（走默认 32）
- `BuildKinematicCollisionBodies` 改为 `const int count = def.kinematicBodyPoolSize;`
- 删除 `RatKinematicBodyCount` / `EnemyKinematicBodyCount` / `BossKinematicBodyCount` 三个常量

### 11. 修 `KillEnemy` 的 boss victory 缺口

**文件**：`src/Application/Brotato3D/Brotato3DEnemySystem.cpp:296-347`

当前：
```cpp
if (dropLoot && enemy.def && enemy.def->boss.enabled)
{
    // 50 chunk + 8 boss_chunk + material 撒一圈 + ring + light + bossKillFlash + timeScaleRecovery + bossVictoryDelay
    spdlog::info("[Brotato3D] [boss defeated]");
}
```

整个 boss 视觉 + bossVictoryDelayMs_ 设置 + 进 result 都包在 `dropLoot` 分支里。如果 wave 强制清场（`ClearAliveEnemies(false)`）时 boss 还活着（理论上不会发生但很微妙），boss 会静默消失但 wave system 永不进 victory state，导致游戏卡住。

**改法**：把"boss 已死亡"和"是否 dropLoot"语义分开。简单做法是把 `bossVictoryDelayMs_ = 1200.0f;` 这一行外提到 `if (enemy.def && enemy.def->boss.enabled)` 分支：
```cpp
if (enemy.def && enemy.def->boss.enabled)
{
    bossVictoryDelayMs_ = std::max(bossVictoryDelayMs_, 1200.0f);
    spdlog::info("[Brotato3D] [boss defeated]");
}
if (dropLoot && enemy.def && enemy.def->boss.enabled)
{
    // 视觉：50 chunk + 8 boss_chunk + Material 撒一圈 + ring + flash + timeScale
}
```

这样无论 dropLoot 与否，boss 死亡都会触发 victory 倒计时。

### 12. `feel-polish-plan.md` 加 "Implementation diverged" 段

**文件**：`docs/projects/brotato-3d/feel-polish-plan.md`

文档头部 Context 段下方加一段：
```markdown
## Implementation Notes（实现偏离 plan 的点）

最终落地的实现与本计划在以下几点有出入，后续阅读以代码为准：

- **没有按 R1–R6 拆分独立任务**：实际是一次性写完，文件结构按 plan 走（`Brotato3DDebris.hpp` + `Brotato3DDebrisSystem.cpp`）
- **Material 拾取物走统一 debris pool**：通过 `FDebrisRuntime::pickable` flag 区分装饰碎块和 Material box，而非 plan 描述的"独立 emission"。XP 球同样获得了真物理 body（plan 里只规划了伪物理弹跳）
- **三态 pickup 状态机简化**：Settling 状态在实现里仅作为计时器存在，无独立行为差异（详见 code review P2）
- **Kinematic body 加了"代理 Node" workaround**：因为 `Scene::RebuildMeshBuffer` 会把没有 PhysicsComponent 持有的 body 替换成 mesh body。绕过方法是为每个 kinematic body 创建一个 invisible 渲染 Node，挂 PhysicsComponent（mobility 标记 Dynamic 让 mesh rebuild 跳过）。这是引擎层缺陷，未来应在引擎层修
- **Wave 切换时 `ClearAllDebris(false)` 会一并清掉未拾取的 Material box**：玩家来不及捡的就丢了。这是产品决策（避免跨波累积破坏经济）
```

### 13. 删除 `EnableKinematicDebrisPush` 死分支

**文件**：`src/Application/Brotato3D/Brotato3DDebrisSystem.cpp`

只有当 P1 第 3 点的引擎层 issue 真有计划要修时才**保留**这个 flag。现状是 `constexpr true` 永远不会触发 false 分支，4 处 `if (!EnableKinematicDebrisPush)` 的早期 return 是死代码。

**两个选项二选一**：
- (a) 保守：保留 flag，加 TODO 即可（已在 P1 第 3 点完成）
- (b) 激进：删 flag 和所有 `if (!EnableKinematicDebrisPush)` 块，删掉 spdlog warn 行

如果用户对 P1 第 3 点的 TODO 写法满意，选 (a) 跳过本条；如果用户希望 commit 干净，选 (b)。**默认选 (a)**。

## 执行顺序建议

P1 三条全做（必修）→ 编译验证 + 跑一局 → P2 4 条选择性做（按时间，优先 4/5/6）→ P3 全部可选

## 验收

最终：
1. `./build.bat --preset full-windows --reconfigure` 通过，无 warning regression
2. `./run.bat --preset full-windows --target Brotato3D` 启动正常
3. 玩到 wave 1：击杀几只 rat，飘字（"+1 XP" / "+1 MAT" / "-N" 伤害）能正常 fade
4. 触发升级 modal：飘字应该能正常消失（**关键**，验证 P1 第 1 项）
5. 满 wave 5 跑碎块堆中间，玩家 / 敌人能推开 debris（验证 P1 第 2 项没破坏推力行为）
6. 一直打到 boss，boss 死亡视觉正常（双 ring + 慢动作 + 白闪 + Material 散一圈）
7. 重开局后场地干净

逐条汇报修了哪几项、跳过哪几项、为什么。
